/* Public domain. No rights reserved. Wholly AI-authored. */
/* Digital-video MCI wire parameters follow the OS/2 Toolkit mcios2.h.
   Decoder workers transfer retained frames to the PM owner; they never render. */
#include "os2libs/os2native.h"
#include "os2libs/mdm_video.h"
#include "os2libs/video_decode.h"
#include "os2libs/pmwin/video_frames.h"
#include "host/clock.h"
#include "host/sync.h"
#include "emulator/audio_mixer.h"
#include "emulator/features.h"
#include <ctype.h>
#include <errno.h>

enum
{
    VIDEO_DEVICES = 16,
    VIDEO_FIRST_ID = 0x8000,
    V_OPEN = 1,
    V_CLOSE = 2,
    V_PLAY = 4,
    V_SEEK = 5,
    V_STOP = 6,
    V_PAUSE = 7,
    V_CAPS = 9,
    V_STATUS = 10,
    V_SET = 12,
    V_LOAD = 22,
    V_RESUME = 29,
    V_PUT = 44,
    V_WHERE = 51,
    V_WINDOW = 52,
    V_NOTIFY = 1,
    V_WAIT = 2,
    V_FROM = 4,
    V_TO = 8,
    V_FRAME_TITLE = 0x1,
    V_FRAME_SYSMENU = 0x2,
    V_FRAME_DIALOG_BORDER = 0x100,
    V_PM_REMOVE = 1,
    V_OPEN_ELEMENT = 0x100,
    V_OPEN_TYPE_ID = 0x1000,
    V_OPEN_MMIO = 0x4000,
    V_OPEN_PARENT = 0x1000000,
    V_FORMAT_MS = 1,
    V_FORMAT_FRAMES = 8,
    V_MODE_PAUSE = 2,
    V_MODE_NOT_READY = 1,
    V_MODE_PLAY = 3,
    V_MODE_STOP = 4,
    V_ERR_DEVICE = 5001,
    V_ERR_NOMEM = 5008,
    V_ERR_UNSUPPORTED = 5018,
    V_ERR_FILE = 5019,
    V_ERR_NOT_READY = 5020,
    V_ERR_RANGE = 5026,
    V_ERR_BUFFER = 5050,
    V_ERR_FLAG = 5054,
    V_ERR_IO = 5125
};

enum
{
    VIDEO_AUDIO_FRAMES = 96000
};

typedef struct VideoAudio
{
    float *samples;
    uint64_t *tags;
    uint64_t rendered, end, limit;
    int64_t origin;
    int paused;
    float volume;
} VideoAudio;

typedef struct VideoDevice
{
    USHORT id;
    LxVideoDecoder *decoder;
    LxVideoInfo info;
    void *target;
    LxHostThread *thread;
    int audio;
    VideoAudio sound;
    HWND window, frame, notify_window;
    RECTL destination;
    uint32_t mode, format, notify, user, error, canceled;
    float volume;
    char alias[128];
    int stop, closing;
    int64_t position, until;
    uint64_t started, paused_at;
} VideoDevice;

static VideoDevice devices[VIDEO_DEVICES];
static USHORT next_id = VIDEO_FIRST_ID;
static LxHostMonitor *monitor;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static int audio_registered;

static void initialize(void)
{
    monitor = lx_host_monitor_create();
}

static void reset_device(VideoDevice *device)
{
    lx_host_monitor_lock(monitor);
    memset(device, 0, sizeof *device);
    lx_host_monitor_unlock(monitor);
}

static VideoDevice *find(USHORT id)
{
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (devices[i].id == id && id && !devices[i].closing)
            return &devices[i];
    return NULL;
}

static int fill_audio(void *context, float *samples, int count, int frequency)
{
    (void)context;
    if (frequency != 48000)
        return 1;
    lx_host_monitor_lock(monitor);
    for (unsigned device = 0; device < VIDEO_DEVICES; device++)
    {
        VideoDevice *d = &devices[device];
        VideoAudio *sound = &d->sound;
        if (!d->audio || sound->paused)
            continue;
        for (int i = 0; i < count; i++)
        {
            if (sound->rendered >= sound->limit)
                continue;
            uint64_t at = sound->rendered++, slot = at % VIDEO_AUDIO_FRAMES;
            if (sound->tags[slot] == at)
            {
                samples[2 * i] += sound->samples[slot * 2] * sound->volume;
                samples[2 * i + 1] += sound->samples[slot * 2 + 1] * sound->volume;
                sound->tags[slot] = UINT64_MAX;
            }
        }
    }
    lx_host_monitor_unlock(monitor);
    return 1;
}

static int enqueue_audio(void *context, const float *samples, unsigned count, int64_t position)
{
    VideoDevice *d = context;
    unsigned done = 0;
    int64_t start = position - d->sound.origin;
    if (d->sound.limit != UINT64_MAX)
    {
        if (start >= (int64_t)d->sound.limit)
            return 1;
        if ((int64_t)count > (int64_t)d->sound.limit - start)
            count = (unsigned)((int64_t)d->sound.limit - start);
    }
    while (done < count)
    {
        lx_host_monitor_lock(monitor);
        int stopped = d->stop;
        if (stopped || !d->audio || !lx_pm_video_ready(d->target))
        {
            lx_host_monitor_unlock(monitor);
            return 0;
        }
        uint64_t rendered = d->sound.rendered;
        int64_t at = start + done;
        if (at < 0 || (uint64_t)at < rendered)
        {
            uint64_t discard = (uint64_t)((int64_t)rendered - at);
            if (discard > count - done)
                discard = count - done;
            done += (unsigned)discard;
            lx_host_monitor_unlock(monitor);
            continue;
        }
        if ((uint64_t)at >= rendered + VIDEO_AUDIO_FRAMES)
        {
            lx_host_monitor_unlock(monitor);
            lx_host_sleep_ms(2);
            continue;
        }
        uint64_t available = rendered + VIDEO_AUDIO_FRAMES - (uint64_t)at;
        unsigned amount = count - done;
        if (amount > available)
            amount = (unsigned)available;
        for (unsigned i = 0; i < amount; i++)
        {
            uint64_t index = (uint64_t)at + i, slot = index % VIDEO_AUDIO_FRAMES;
            d->sound.tags[slot] = index;
            d->sound.samples[slot * 2] = samples[(done + i) * 2];
            d->sound.samples[slot * 2 + 1] = samples[(done + i) * 2 + 1];
        }
        if ((uint64_t)at + amount > d->sound.end)
            d->sound.end = (uint64_t)at + amount;
        lx_host_monitor_unlock(monitor);
        done += amount;
    }
    return 1;
}

static int64_t media_clock(VideoDevice *d)
{
    if (d->audio)
    {
        return d->sound.origin / 48 + (int64_t)(d->sound.rendered / 48);
    }
    return (int64_t)lx_host_video_ticks_ms() - (int64_t)d->started;
}

static int start_audio(VideoDevice *d, int64_t from, int64_t to)
{
    if (!d->info.audio_present)
        return 1;
    d->sound = (VideoAudio){.paused = 1,
                            .origin = from * 48,
                            .volume = d->volume,
                            .limit = to >= 0 ? (uint64_t)(to - from) * 48 : UINT64_MAX};
    d->sound.samples = calloc((size_t)VIDEO_AUDIO_FRAMES * 2, sizeof(float));
    d->sound.tags = malloc(VIDEO_AUDIO_FRAMES * sizeof(uint64_t));
    if (!d->sound.samples || !d->sound.tags)
    {
        free(d->sound.samples);
        free(d->sound.tags);
        memset(&d->sound, 0, sizeof d->sound);
        return 0;
    }
    memset(d->sound.tags, 0xff, VIDEO_AUDIO_FRAMES * sizeof(uint64_t));
    if (!audio_registered)
    {
        if (!GProcessState.registerStereoAudioGenerator ||
            !GProcessState.registerStereoAudioGenerator(fill_audio, NULL, 1))
        {
            free(d->sound.samples);
            free(d->sound.tags);
            memset(&d->sound, 0, sizeof d->sound);
            return 0;
        }
        audio_registered = 1;
    }
    lx_host_monitor_lock(monitor);
    d->audio = 1;
    lx_host_monitor_unlock(monitor);
    lx_video_decoder_audio_sink(d->decoder, enqueue_audio, d);
    return 1;
}

static void release_frame(void *context)
{
    LxDecodedVideoFrame *frame = context;
    lx_video_frame_release(frame);
    free(frame);
}

static void *play_worker(void *context)
{
    VideoDevice *d = context;
    int result = 0;
    int64_t last_end = d->position;
    for (;;)
    {
        lx_host_monitor_lock(monitor);
        while (!d->stop && d->mode == V_MODE_PAUSE && lx_pm_video_ready(d->target))
            lx_host_monitor_wait(monitor, 20);
        int stopped = d->stop;
        lx_host_monitor_unlock(monitor);
        if (stopped)
            break;
        if (!lx_pm_video_ready(d->target))
        {
            result = -1;
            break;
        }
        LxDecodedVideoFrame *frame = calloc(1, sizeof *frame);
        if (!frame)
        {
            result = -1;
            break;
        }
        int decoded = lx_video_decoder_next(d->decoder, frame);
        if (decoded <= 0)
        {
            free(frame);
            result = decoded;
            break;
        }
        lx_host_monitor_lock(monitor);
        while (!d->stop && lx_pm_video_ready(d->target))
        {
            if (d->mode == V_MODE_PAUSE)
            {
                lx_host_monitor_wait(monitor, 20);
                continue;
            }
            int64_t now = media_clock(d);
            int64_t due = frame->milliseconds;
            if (due <= now)
                break;
            uint64_t wait = (uint64_t)(due - now);
            lx_host_monitor_wait(monitor, wait > 20 ? 20 : (uint32_t)wait);
        }
        int present = !d->stop && (d->until < 0 || frame->milliseconds < d->until);
        void *target = d->target;
        RECTL destination = d->destination;
        if (present)
        {
            d->position = frame->milliseconds;
            last_end = frame->milliseconds + frame->duration_ms;
        }
        lx_host_monitor_unlock(monitor);
        if (!present)
        {
            release_frame(frame);
            break;
        }
        if (!lx_pm_video_submit(target, &frame->image, &destination, frame, release_frame))
        {
            result = -1;
            break;
        }
    }
    lx_host_monitor_lock(monitor);
    if (!result && !d->stop)
    {
        int64_t end = d->until >= 0 && d->until < last_end ? d->until : last_end;
        if (d->audio)
        {
            int64_t audio_end = d->sound.origin / 48 + (int64_t)((d->sound.end + 47) / 48);
            if (d->until >= 0 && audio_end > d->until)
                audio_end = d->until;
            if (audio_end > end)
                end = audio_end;
        }
        while (!d->stop && lx_pm_video_ready(d->target) &&
               (d->mode == V_MODE_PAUSE || media_clock(d) < end))
            lx_host_monitor_wait(monitor, 10);
        if (!d->stop && !lx_pm_video_ready(d->target))
            result = -1;
        if (!d->stop)
            d->position = end;
    }
    unsigned notify = d->notify;
    uint32_t completion = d->stop ? d->canceled : result < 0 ? 3 : 0;
    HWND window = d->notify_window;
    USHORT id = d->id;
    uint32_t user = d->user;
    d->error = result < 0 ? V_ERR_IO : 0;
    d->mode = V_MODE_STOP;
    if (d->audio)
    {
        d->sound.paused = 1;
    }
    lx_host_monitor_wake_all(monitor);
    lx_host_monitor_unlock(monitor);
    if (notify && window)
        WinPostMsg(window, 0x500, (MPARAM)(uintptr_t)(((ULONG)id << 16) | completion),
                   (MPARAM)(uintptr_t)user);
    return NULL;
}

static void stop_reason(VideoDevice *d, unsigned reason)
{
    lx_host_monitor_lock(monitor);
    d->stop = 1;
    d->canceled = reason;
    lx_host_monitor_wake_all(monitor);
    LxHostThread *thread = d->thread;
    d->thread = NULL;
    lx_host_monitor_unlock(monitor);
    if (thread)
        lx_host_thread_join(thread, NULL);
    lx_host_monitor_lock(monitor);
    d->audio = 0;
    free(d->sound.samples);
    free(d->sound.tags);
    memset(&d->sound, 0, sizeof d->sound);
    lx_host_monitor_unlock(monitor);
    lx_video_decoder_audio_sink(d->decoder, NULL, NULL);
    lx_host_monitor_lock(monitor);
    d->mode = d->decoder ? V_MODE_STOP : V_MODE_NOT_READY;
    lx_host_monitor_unlock(monitor);
}

static void stop(VideoDevice *d)
{
    stop_reason(d, 2);
}

static uint32_t position_units(VideoDevice *d, int64_t milliseconds)
{
    if (milliseconds < 0)
        return 0;
    uint64_t value =
        d->format == V_FORMAT_FRAMES
            ? (uint64_t)milliseconds * d->info.rate_numerator / (1000ull * d->info.rate_denominator)
            : (uint64_t)milliseconds;
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static int64_t milliseconds(VideoDevice *d, ULONG value)
{
    uint64_t result = value;
    if (d->format == V_FORMAT_FRAMES)
    {
        result *= 1000;
        if (result > UINT64_MAX / d->info.rate_denominator)
            return -1;
        result = result * d->info.rate_denominator / d->info.rate_numerator;
    }
    return result > UINT32_MAX ? -1 : (int64_t)result;
}

static int video_name(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    static const char *extensions[] = {".avi",  ".mov", ".mpg", ".mpeg", ".mp4", ".mkv",
                                       ".webm", ".gif", ".fli", ".flc",  NULL};
    for (unsigned i = 0; dot && extensions[i]; i++)
        if (!strcasecmp(dot, extensions[i]))
            return 1;
    return 0;
}

static int set_window(VideoDevice *d, HWND window)
{
    void *target = lx_pm_video_target(window);
    if (!target)
        return 0;
    lx_pm_video_release(d->target);
    d->target = target;
    d->window = window;
    RECTL bounds;
    if (WinQueryWindowRect(window, &bounds))
        d->destination = bounds;
    return 1;
}

static int create_window(VideoDevice *d)
{
    HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
    if (!hab)
        return 0;
    WinRegisterClass(hab, "2ineDigitalVideo", WinDefWindowProc, CS_SIZEREDRAW, 0);
    ULONG flags = V_FRAME_TITLE | V_FRAME_SYSMENU | V_FRAME_DIALOG_BORDER;
    HWND client = 0;
    d->frame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flags, "2ineDigitalVideo",
                                  "Digital Video", WS_VISIBLE, 0, d->id, &client);
    if (!d->frame)
        return 0;
    RECTL rect = {0, 0, (LONG)d->info.width, (LONG)d->info.height};
    WinCalcFrameRect(d->frame, &rect, FALSE);
    WinSetWindowPos(d->frame, HWND_TOP, 100, 100, rect.xRight - rect.xLeft,
                    rect.yTop - rect.yBottom, SWP_MOVE | SWP_SIZE | SWP_SHOW);
    if (set_window(d, client))
        return 1;
    WinDestroyWindow(d->frame);
    d->frame = 0;
    return 0;
}

static ULONG load_element(ULONG address, LxVideoDecoder **decoder, LxVideoInfo *info)
{
    char *name = lx_execution_copy_string(GProcessState.execution, address, 4096);
    if (!name)
        return V_ERR_BUFFER;
    uint32_t error = 0;
    char *path = GProcessState.makeUnixPath(name, &error);
    free(name);
    if (!path)
        return V_ERR_FILE;
    *decoder = lx_video_decoder_open_audio(path, info, lxRuntimeFeatureEnabled(LX_FEATURE_PCM));
    free(path);
    return *decoder ? 0 : V_ERR_NOT_READY;
}

static ULONG open_device(ULONG flags, unsigned char *p)
{
    if (flags & ~((ULONG)V_NOTIFY | V_WAIT | V_OPEN_ELEMENT | V_OPEN_TYPE_ID | 0x200 | 0x2000 |
                  0x8000 | V_OPEN_PARENT))
        return V_ERR_UNSUPPORTED;
    VideoDevice *d = NULL;
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (!devices[i].id)
        {
            d = &devices[i];
            break;
        }
    if (!d || !next_id)
        return V_ERR_NOMEM;
    LxVideoInfo info = {.rate_numerator = 25, .rate_denominator = 1};
    LxVideoDecoder *decoder = NULL;
    if (flags & V_OPEN_ELEMENT)
    {
        ULONG error = load_element(lx_guest_load_u32(p + 12), &decoder, &info);
        if (error)
            return error;
    }
    reset_device(d);
    d->id = next_id++;
    d->decoder = decoder;
    d->info = info;
    d->mode = decoder ? V_MODE_STOP : V_MODE_NOT_READY;
    d->format = V_FORMAT_MS;
    d->volume = 1;
    d->until = -1;
    d->destination = (RECTL){0, 0, (LONG)info.width, (LONG)info.height};
    if ((flags & V_OPEN_PARENT) && !set_window(d, lx_guest_load_u32(p + 20)))
    {
        lx_video_decoder_close(decoder);
        reset_device(d);
        return V_ERR_NOT_READY;
    }
    if (flags & 0x200)
    {
        char *alias =
            lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(p + 16), 128);
        if (!alias || !*alias)
        {
            free(alias);
            lx_video_decoder_close(decoder);
            lx_pm_video_release(d->target);
            reset_device(d);
            return V_ERR_BUFFER;
        }
        snprintf(d->alias, sizeof d->alias, "%s", alias);
        free(alias);
    }
    lx_guest_store_u16(p + 4, d->id);
    return 0;
}

static ULONG video_command(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user,
                           int *handled)
{
    *handled = 0;
    unsigned char *p = parameters;
    if (message == V_OPEN && p)
    {
        ULONG type = lx_guest_load_u32(p + 8);
        int is_video = (flags & V_OPEN_TYPE_ID) && type == 12;
        char *text = NULL;
        if (!(flags & V_OPEN_TYPE_ID) && type)
        {
            text = lx_execution_copy_string(GProcessState.execution, type, 128);
            is_video =
                text && (!strcasecmp(text, "digitalvideo") || !strcasecmp(text, "digitalvideo01"));
            free(text);
        }
        if (!type && (flags & V_OPEN_ELEMENT))
        {
            text =
                lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(p + 12), 4096);
            is_video = video_name(text);
            free(text);
        }
        if (!is_video)
            return 0;
        *handled = 1;
        if (!lxRuntimeFeatureEnabled(LX_FEATURE_VIDEO))
        {
            fprintf(stderr,
                    "2ine: video playback is disabled in this ball's Graphics & Media settings\n");
            return V_ERR_NOT_READY;
        }
        lx_host_once(&once, initialize);
        return monitor ? open_device(flags, p) : V_ERR_NOMEM;
    }
    VideoDevice *d = find(id);
    if (!d)
        return 0;
    *handled = 1;
    if (!p)
        return V_ERR_BUFFER;
    if ((message == V_CLOSE || message == V_STOP || message == V_PAUSE || message == V_RESUME) &&
        (flags & ~((ULONG)V_NOTIFY | V_WAIT)))
        return V_ERR_UNSUPPORTED;
    if (message == V_CLOSE)
    {
        d->closing = 1;
        stop(d);
        lx_pm_video_release(d->target);
        if (d->frame)
            WinDestroyWindow(d->frame);
        lx_video_decoder_close(d->decoder);
        reset_device(d);
        return 0;
    }
    if (message == V_STOP)
    {
        stop(d);
        return 0;
    }
    if (message == V_LOAD)
    {
        if (flags & ~((ULONG)V_NOTIFY | V_WAIT))
            return V_ERR_UNSUPPORTED;
        LxVideoInfo info;
        LxVideoDecoder *decoder = NULL;
        ULONG error = load_element(lx_guest_load_u32(p + 4), &decoder, &info);
        if (error)
            return error;
        stop(d);
        lx_video_decoder_close(d->decoder);
        d->decoder = decoder;
        d->info = info;
        d->position = 0;
        d->mode = V_MODE_STOP;
        if (!d->window)
            d->destination = (RECTL){0, 0, (LONG)info.width, (LONG)info.height};
        return 0;
    }
    if (message == V_WINDOW)
    {
        if (flags & ~((ULONG)V_NOTIFY | V_WAIT | 0x100))
            return V_ERR_UNSUPPORTED;
        stop(d);
        return (flags & 0x100) && set_window(d, lx_guest_load_u32(p + 4)) ? 0 : V_ERR_BUFFER;
    }
    if (message == V_PUT || message == V_WHERE)
    {
        if ((flags & ~((ULONG)V_NOTIFY | V_WAIT | 0x300)) || !(flags & 0x200))
            return V_ERR_UNSUPPORTED;
        if (message == V_PUT)
        {
            LONG x = (LONG)lx_guest_load_u32(p + 4), y = (LONG)lx_guest_load_u32(p + 8);
            LONG width = (LONG)lx_guest_load_u32(p + 12), height = (LONG)lx_guest_load_u32(p + 16);
            int64_t right = (int64_t)x + width, top = (int64_t)y + height;
            if (width <= 0 || height <= 0 || right > INT32_MAX || top > INT32_MAX)
                return V_ERR_RANGE;
            RECTL rect = {x, y, (LONG)right, (LONG)top};
            lx_host_monitor_lock(monitor);
            d->destination = rect;
            lx_host_monitor_unlock(monitor);
        }
        else
        {
            lx_guest_store_u32(p + 4, d->destination.xLeft);
            lx_guest_store_u32(p + 8, d->destination.yBottom);
            lx_guest_store_u32(p + 12, d->destination.xRight - d->destination.xLeft);
            lx_guest_store_u32(p + 16, d->destination.yTop - d->destination.yBottom);
        }
        return 0;
    }
    if (message == V_SET && (flags & 0x80000))
    {
        if ((flags & ~((ULONG)V_NOTIFY | V_WAIT | 0x80000 | 0x1000)) || lx_guest_load_u32(p + 12) ||
            lx_guest_load_u32(p + 20))
            return V_ERR_UNSUPPORTED;
        ULONG level = lx_guest_load_u32(p + 16);
        if (level > 100)
            return V_ERR_RANGE;
        lx_host_monitor_lock(monitor);
        d->volume = level / 100.0f;
        d->sound.volume = d->volume;
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == V_SET)
    {
        if ((flags & ~((ULONG)V_NOTIFY | V_WAIT | 0x10000)) || !(flags & 0x10000))
            return V_ERR_UNSUPPORTED;
        ULONG format = lx_guest_load_u32(p + 4);
        if (format != V_FORMAT_MS && format != V_FORMAT_FRAMES)
            return V_ERR_FLAG;
        lx_host_monitor_lock(monitor);
        d->format = format;
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == V_STATUS || message == V_CAPS)
    {
        if ((flags & ~((ULONG)V_NOTIFY | V_WAIT | 0x300)) ||
            (message == V_STATUS && (flags & 0x300) != 0x100) ||
            (message == V_CAPS && (flags & 0x300) != 0x100 && (flags & 0x300) != 0x200))
            return V_ERR_FLAG;
        ULONG item = lx_guest_load_u32(p + 8), value = 0;
        if (message == V_CAPS && (flags & 0x100))
        {
            switch (lx_guest_load_u16(p + 12))
            {
                case V_OPEN:
                case V_CLOSE:
                case V_PLAY:
                case V_SEEK:
                case V_STOP:
                case V_PAUSE:
                case V_CAPS:
                case V_STATUS:
                case V_SET:
                case V_LOAD:
                case V_RESUME:
                case V_PUT:
                case V_WHERE:
                case V_WINDOW:
                    value = 1;
                    break;
            }
            lx_guest_store_u32(p + 4, value);
            return 0;
        }
        lx_host_monitor_lock(monitor);
        if (message == V_STATUS)
        {
            switch (item)
            {
                case 2:
                    value = position_units(d, d->info.duration_ms);
                    break;
                case 3:
                    value = d->mode;
                    break;
                case 5:
                    value = position_units(d, d->position);
                    break;
                case 7:
                case 9:
                case 14:
                    value = d->decoder != NULL;
                    break;
                case 8:
                    value = (ULONG)(d->volume * 100.0f + 0.5f);
                    break;
                case 10:
                    value = d->format;
                    break;
                case 13:
                    value = d->info.audio_present;
                    break;
                case 0x7000:
                    value = d->window;
                    break;
                case 0x700b:
                    value = d->info.width;
                    break;
                case 0x700c:
                    value = d->info.height;
                    break;
                default:
                    lx_host_monitor_unlock(monitor);
                    return V_ERR_UNSUPPORTED;
            }
        }
        else
        {
            switch (item)
            {
                case 3:
                case 8:
                case 12:
                case 15:
                case 0x7000:
                case 0x7001:
                    value = 1;
                    break;
                case 1:
                case 2:
                case 4:
                case 5:
                case 6:
                case 7:
                case 9:
                case 16:
                case 18:
                case 0x7007:
                case 0x7008:
                case 0x7009:
                    value = 0;
                    break;
                case 0x7006:
                    value = 1;
                    break;
                case 10:
                    value = 12;
                    break;
                case 11:
                    value = d->info.audio_present;
                    break;
                case 0x7002:
                case 0x7004:
                    value = d->info.width;
                    break;
                case 0x7003:
                case 0x7005:
                    value = d->info.height;
                    break;
                default:
                    lx_host_monitor_unlock(monitor);
                    return V_ERR_UNSUPPORTED;
            }
        }
        lx_host_monitor_unlock(monitor);
        lx_guest_store_u32(p + 4, value);
        return 0;
    }
    if (message == V_PAUSE || message == V_RESUME)
    {
        lx_host_monitor_lock(monitor);
        if (message == V_PAUSE && d->mode == V_MODE_PLAY)
        {
            d->mode = V_MODE_PAUSE;
            d->paused_at = lx_host_video_ticks_ms();
            if (d->audio)
            {
                d->sound.paused = 1;
            }
        }
        else if (message == V_RESUME && d->mode == V_MODE_PAUSE)
        {
            d->started += lx_host_video_ticks_ms() - d->paused_at;
            d->mode = V_MODE_PLAY;
            if (d->audio)
            {
                d->sound.paused = 0;
            }
            lx_host_monitor_wake_all(monitor);
        }
        else
        {
            lx_host_monitor_unlock(monitor);
            return V_ERR_NOT_READY;
        }
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == V_SEEK)
    {
        if ((flags & ~((ULONG)V_NOTIFY | V_WAIT | 0x300)) || (flags & 0x300) == 0x300)
            return V_ERR_UNSUPPORTED;
        if (!d->decoder)
            return V_ERR_NOT_READY;
        stop(d);
        int64_t at = flags & 0x100   ? 0
                     : flags & 0x200 ? d->info.duration_ms
                                     : milliseconds(d, lx_guest_load_u32(p + 4));
        if (at < 0 || (d->info.duration_ms > 0 && at > d->info.duration_ms))
            return V_ERR_RANGE;
        if (!lx_video_decoder_seek(d->decoder, at))
            return V_ERR_IO;
        d->position = at;
        return 0;
    }
    if (message == V_PLAY)
    {
        if (flags & ~((ULONG)V_NOTIFY | V_WAIT | V_FROM | V_TO))
            return V_ERR_UNSUPPORTED;
        if (!d->decoder)
            return V_ERR_NOT_READY;
        stop_reason(d, 1);
        if (!d->target && !create_window(d))
            return V_ERR_NOT_READY;
        int64_t from = flags & V_FROM ? milliseconds(d, lx_guest_load_u32(p + 4)) : d->position;
        int64_t to = flags & V_TO              ? milliseconds(d, lx_guest_load_u32(p + 8))
                     : d->info.duration_ms > 0 ? d->info.duration_ms
                                               : -1;
        if (from < 0 || ((flags & V_TO) && to < 0) || (to >= 0 && to <= from) ||
            (d->info.duration_ms > 0 && (from > d->info.duration_ms || to > d->info.duration_ms)))
            return V_ERR_RANGE;
        if (!lx_video_decoder_seek(d->decoder, from))
            return V_ERR_IO;
        if (!start_audio(d, from, to))
            return V_ERR_NOT_READY;
        lx_host_monitor_lock(monitor);
        d->position = from;
        d->until = to;
        d->stop = 0;
        d->error = 0;
        d->mode = V_MODE_PLAY;
        d->notify = flags & V_NOTIFY;
        d->notify_window = lx_guest_load_u32(p);
        d->user = user;
        d->started = lx_host_video_ticks_ms() - (uint64_t)from;
        d->thread = lx_host_thread_create(play_worker, d, NULL);
        if (d->audio && d->thread)
        {
            d->sound.paused = 0;
        }
        if (!d->thread)
        {
            d->mode = V_MODE_STOP;
            lx_host_monitor_unlock(monitor);
            stop(d);
            return V_ERR_NOMEM;
        }
        lx_host_monitor_unlock(monitor);
        if (flags & V_WAIT)
        {
            HAB hab = WinQueryAnchorBlock(d->window);
            for (;;)
            {
                lx_host_monitor_lock(monitor);
                int playing = d->mode != V_MODE_STOP && d->id == id;
                lx_host_monitor_unlock(monitor);
                lx_pm_video_pump_current();
                if (!playing)
                    break;
                QMSG message;
                while (WinPeekMsg(hab, &message, 0, 0, 0, V_PM_REMOVE))
                    WinDispatchMsg(hab, &message);
                lx_host_sleep_ms(2);
            }
            stop(d);
            return lx_pm_video_ready(d->target) ? d->error : V_ERR_IO;
        }
        return 0;
    }
    return V_ERR_UNSUPPORTED;
}

ULONG lx_mci_video_command(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user,
                           int *handled)
{
    ULONG result = video_command(id, message, flags, parameters, user, handled);
    if (*handled && !result && (flags & V_NOTIFY) && message != V_PLAY && parameters)
    {
        ULONG window = lx_guest_load_u32(parameters);
        USHORT device = message == V_OPEN ? lx_guest_load_u16((uint8 *)parameters + 4) : id;
        if (window)
            WinPostMsg(window, 0x500, (MPARAM)(uintptr_t)((ULONG)device << 16),
                       (MPARAM)(uintptr_t)user);
    }
    return result;
}

void lx_mci_video_shutdown(void)
{
    if (!monitor)
        return;
    if (audio_registered)
    {
        lxUnregisterAudioGenerator(fill_audio, NULL);
        audio_registered = 0;
    }
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (devices[i].id)
        {
            VideoDevice *d = &devices[i];
            d->closing = 1;
            stop(d);
            lx_pm_video_release(d->target);
            if (d->frame)
                WinDestroyWindow(d->frame);
            lx_video_decoder_close(d->decoder);
            reset_device(d);
        }
}

#include "os2libs/mdm_video_string.h"
