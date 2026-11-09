/* Public domain. No rights reserved. Wholly AI-authored. */
/* Digital-video MCI wire parameters follow the OS/2 Toolkit mcios2.h.
   Decoder workers transfer retained frames to the PM owner; they never render. */
#include "os2/include/private/export.h"
#include "os2/include/private/os2mep.h"
#include "os2/mm/mmpm/mci/mdm/mdm_video.h"
#include "os2/mm/video/decode/video_decode.h"
#include "os2/include/private/pmp.h"
#include "host/clock.h"
#include "host/sync.h"
#include "emulator/audio_mixer.h"
#include "emulator/features.h"
#include <ctype.h>
#include <errno.h>

/* Video device IDs start above the waveaudio and sequencer IDs. */
enum
{
    VIDEO_DEVICES = 16,
    VIDEO_FIRST_ID = 0x8000
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
        while (!d->stop && d->mode == MCI_MODE_PAUSE && lx_pm_video_ready(d->target))
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
            if (d->mode == MCI_MODE_PAUSE)
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
               (d->mode == MCI_MODE_PAUSE || media_clock(d) < end))
            lx_host_monitor_wait(monitor, 10);
        if (!d->stop && !lx_pm_video_ready(d->target))
            result = -1;
        if (!d->stop)
            d->position = end;
    }
    unsigned notify = d->notify;
    uint32_t completion = d->stop      ? d->canceled
                          : result < 0 ? MCI_NOTIFY_ERROR
                                       : MCI_NOTIFY_SUCCESSFUL;
    HWND window = d->notify_window;
    USHORT id = d->id;
    uint32_t user = d->user;
    d->error = result < 0 ? MCIERR_FILE_IO : 0;
    d->mode = MCI_MODE_STOP;
    if (d->audio)
    {
        d->sound.paused = 1;
    }
    lx_host_monitor_wake_all(monitor);
    lx_host_monitor_unlock(monitor);
    if (notify && window)
        WinPostMsg(window, MM_MCINOTIFY, (MPARAM)(uintptr_t)(((ULONG)id << 16) | completion),
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
    d->mode = d->decoder ? MCI_MODE_STOP : MCI_MODE_NOT_READY;
    lx_host_monitor_unlock(monitor);
}

static void stop(VideoDevice *d)
{
    stop_reason(d, MCI_NOTIFY_ABORTED);
}

static uint32_t position_units(VideoDevice *d, int64_t milliseconds)
{
    if (milliseconds < 0)
        return 0;
    uint64_t value =
        d->format == MCI_FORMAT_FRAMES
            ? (uint64_t)milliseconds * d->info.rate_numerator / (1000ull * d->info.rate_denominator)
            : (uint64_t)milliseconds;
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static int64_t milliseconds(VideoDevice *d, ULONG value)
{
    uint64_t result = value;
    if (d->format == MCI_FORMAT_FRAMES)
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
    ULONG flags = FCF_TITLEBAR | FCF_SYSMENU | FCF_DLGBORDER;
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
        return MCIERR_INVALID_BUFFER;
    uint32_t error = 0;
    char *path = GProcessState.makeUnixPath(name, &error);
    free(name);
    if (!path)
        return MCIERR_FILE_NOT_FOUND;
    *decoder = lx_video_decoder_open_audio(path, info, lxRuntimeFeatureEnabled(LX_FEATURE_PCM));
    free(path);
    return *decoder ? 0 : MCIERR_DEVICE_NOT_READY;
}

static ULONG open_device(ULONG flags, unsigned char *p)
{
    if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_OPEN_ELEMENT | MCI_OPEN_TYPE_ID |
                  MCI_OPEN_ALIAS | MCI_OPEN_SHAREABLE | MCI_READONLY | MCI_DGV_OPEN_PARENT))
        return MCIERR_UNSUPPORTED_FUNCTION;
    VideoDevice *d = NULL;
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (!devices[i].id)
        {
            d = &devices[i];
            break;
        }
    if (!d || !next_id)
        return MCIERR_OUT_OF_MEMORY;
    LxVideoInfo info = {.rate_numerator = 25, .rate_denominator = 1};
    LxVideoDecoder *decoder = NULL;
    if (flags & MCI_OPEN_ELEMENT)
    {
        ULONG error =
            load_element(lx_guest_load_u32(p + MCI_OPEN_PARMS_ELEMENT_NAME), &decoder, &info);
        if (error)
            return error;
    }
    reset_device(d);
    d->id = next_id++;
    d->decoder = decoder;
    d->info = info;
    d->mode = decoder ? MCI_MODE_STOP : MCI_MODE_NOT_READY;
    d->format = MCI_FORMAT_MILLISECONDS;
    d->volume = 1;
    d->until = -1;
    d->destination = (RECTL){0, 0, (LONG)info.width, (LONG)info.height};
    if ((flags & MCI_DGV_OPEN_PARENT) &&
        !set_window(d, lx_guest_load_u32(p + MCI_DGV_OPEN_PARMS_PARENT)))
    {
        lx_video_decoder_close(decoder);
        reset_device(d);
        return MCIERR_DEVICE_NOT_READY;
    }
    if (flags & MCI_OPEN_ALIAS)
    {
        char *alias = lx_execution_copy_string(GProcessState.execution,
                                               lx_guest_load_u32(p + MCI_OPEN_PARMS_ALIAS), 128);
        if (!alias || !*alias)
        {
            free(alias);
            lx_video_decoder_close(decoder);
            lx_pm_video_release(d->target);
            reset_device(d);
            return MCIERR_INVALID_BUFFER;
        }
        snprintf(d->alias, sizeof d->alias, "%s", alias);
        free(alias);
    }
    lx_guest_store_u16(p + MCI_OPEN_PARMS_DEVICE_ID, d->id);
    return 0;
}

static ULONG video_command(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user,
                           int *handled)
{
    *handled = 0;
    unsigned char *p = parameters;
    if (message == MCI_OPEN && p)
    {
        ULONG type = lx_guest_load_u32(p + MCI_OPEN_PARMS_DEVICE_TYPE);
        int is_video = (flags & MCI_OPEN_TYPE_ID) && type == MCI_DEVTYPE_DIGITAL_VIDEO;
        char *text = NULL;
        if (!(flags & MCI_OPEN_TYPE_ID) && type)
        {
            text = lx_execution_copy_string(GProcessState.execution, type, 128);
            is_video =
                text && (!strcasecmp(text, "digitalvideo") || !strcasecmp(text, "digitalvideo01"));
            free(text);
        }
        if (!type && (flags & MCI_OPEN_ELEMENT))
        {
            text = lx_execution_copy_string(
                GProcessState.execution, lx_guest_load_u32(p + MCI_OPEN_PARMS_ELEMENT_NAME), 4096);
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
            return MCIERR_DEVICE_NOT_READY;
        }
        lx_host_once(&once, initialize);
        return monitor ? open_device(flags, p) : MCIERR_OUT_OF_MEMORY;
    }
    VideoDevice *d = find(id);
    if (!d)
        return 0;
    *handled = 1;
    if (!p)
        return MCIERR_INVALID_BUFFER;
    if ((message == MCI_CLOSE || message == MCI_STOP || message == MCI_PAUSE ||
         message == MCI_RESUME) &&
        (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT)))
        return MCIERR_UNSUPPORTED_FUNCTION;
    if (message == MCI_CLOSE)
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
    if (message == MCI_STOP)
    {
        stop(d);
        return 0;
    }
    if (message == MCI_LOAD)
    {
        if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT))
            return MCIERR_UNSUPPORTED_FUNCTION;
        LxVideoInfo info;
        LxVideoDecoder *decoder = NULL;
        ULONG error =
            load_element(lx_guest_load_u32(p + MCI_LOAD_PARMS_ELEMENT_NAME), &decoder, &info);
        if (error)
            return error;
        stop(d);
        lx_video_decoder_close(d->decoder);
        d->decoder = decoder;
        d->info = info;
        d->position = 0;
        d->mode = MCI_MODE_STOP;
        if (!d->window)
            d->destination = (RECTL){0, 0, (LONG)info.width, (LONG)info.height};
        return 0;
    }
    if (message == MCI_WINDOW)
    {
        if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_DGV_WINDOW_HWND))
            return MCIERR_UNSUPPORTED_FUNCTION;
        stop(d);
        return (flags & MCI_DGV_WINDOW_HWND) &&
                       set_window(d, lx_guest_load_u32(p + MCI_VID_WINDOW_PARMS_HWND_DEST))
                   ? 0
                   : MCIERR_INVALID_BUFFER;
    }
    if (message == MCI_PUT || message == MCI_WHERE)
    {
        /* MCI_WHERE is checked with the MCI_PUT flag values. */
        if ((flags &
             ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_DGV_PUT_RECT | MCI_DGV_PUT_DESTINATION)) ||
            !(flags & MCI_DGV_PUT_DESTINATION))
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (message == MCI_PUT)
        {
            LONG x = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_X),
                 y = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_Y);
            LONG width = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_WIDTH),
                 height = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_HEIGHT);
            int64_t right = (int64_t)x + width, top = (int64_t)y + height;
            if (width <= 0 || height <= 0 || right > INT32_MAX || top > INT32_MAX)
                return MCIERR_OUTOFRANGE;
            RECTL rect = {x, y, (LONG)right, (LONG)top};
            lx_host_monitor_lock(monitor);
            d->destination = rect;
            lx_host_monitor_unlock(monitor);
        }
        else
        {
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_X, d->destination.xLeft);
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_Y, d->destination.yBottom);
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_WIDTH,
                               d->destination.xRight - d->destination.xLeft);
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_HEIGHT,
                               d->destination.yTop - d->destination.yBottom);
        }
        return 0;
    }
    if (message == MCI_SET && (flags & MCI_SET_VOLUME))
    {
        if ((flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_SET_VOLUME | MCI_SET_AUDIO)) ||
            lx_guest_load_u32(p + MCI_SET_PARMS_AUDIO) != MCI_SET_AUDIO_ALL ||
            lx_guest_load_u32(p + MCI_SET_PARMS_OVER))
            return MCIERR_UNSUPPORTED_FUNCTION;
        ULONG level = lx_guest_load_u32(p + MCI_SET_PARMS_LEVEL);
        if (level > 100)
            return MCIERR_OUTOFRANGE;
        lx_host_monitor_lock(monitor);
        d->volume = level / 100.0f;
        d->sound.volume = d->volume;
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == MCI_SET)
    {
        if ((flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_SET_TIME_FORMAT)) ||
            !(flags & MCI_SET_TIME_FORMAT))
            return MCIERR_UNSUPPORTED_FUNCTION;
        ULONG format = lx_guest_load_u32(p + MCI_SET_PARMS_TIME_FORMAT);
        if (format != MCI_FORMAT_MILLISECONDS && format != MCI_FORMAT_FRAMES)
            return MCIERR_INVALID_FLAG;
        lx_host_monitor_lock(monitor);
        d->format = format;
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == MCI_STATUS || message == MCI_GETDEVCAPS)
    {
        /* MCI_STATUS_ITEM and MCI_GETDEVCAPS_MESSAGE share a value, as do the
           items and return fields of both parameter blocks. */
        ULONG kind = flags & (MCI_GETDEVCAPS_MESSAGE | MCI_GETDEVCAPS_ITEM);
        if ((flags &
             ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_GETDEVCAPS_MESSAGE | MCI_GETDEVCAPS_ITEM)) ||
            (message == MCI_STATUS && kind != MCI_STATUS_ITEM) ||
            (message == MCI_GETDEVCAPS && kind != MCI_GETDEVCAPS_MESSAGE &&
             kind != MCI_GETDEVCAPS_ITEM))
            return MCIERR_INVALID_FLAG;
        ULONG item = lx_guest_load_u32(p + MCI_STATUS_PARMS_ITEM), value = 0;
        if (message == MCI_GETDEVCAPS && (flags & MCI_GETDEVCAPS_MESSAGE))
        {
            switch (lx_guest_load_u16(p + MCI_GETDEVCAPS_PARMS_MESSAGE))
            {
                case MCI_OPEN:
                case MCI_CLOSE:
                case MCI_PLAY:
                case MCI_SEEK:
                case MCI_STOP:
                case MCI_PAUSE:
                case MCI_GETDEVCAPS:
                case MCI_STATUS:
                case MCI_SET:
                case MCI_LOAD:
                case MCI_RESUME:
                case MCI_PUT:
                case MCI_WHERE:
                case MCI_WINDOW:
                    value = 1;
                    break;
            }
            lx_guest_store_u32(p + MCI_GETDEVCAPS_PARMS_RETURN, value);
            return 0;
        }
        lx_host_monitor_lock(monitor);
        if (message == MCI_STATUS)
        {
            switch (item)
            {
                case MCI_STATUS_LENGTH:
                    value = position_units(d, d->info.duration_ms);
                    break;
                case MCI_STATUS_MODE:
                    value = d->mode;
                    break;
                case MCI_STATUS_POSITION:
                    value = position_units(d, d->position);
                    break;
                case MCI_STATUS_MEDIA_PRESENT:
                case MCI_STATUS_READY:
                case MCI_STATUS_VIDEO:
                    value = d->decoder != NULL;
                    break;
                case MCI_STATUS_VOLUME:
                    value = (ULONG)(d->volume * 100.0f + 0.5f);
                    break;
                case MCI_STATUS_TIME_FORMAT:
                    value = d->format;
                    break;
                case MCI_STATUS_AUDIO:
                    value = d->info.audio_present;
                    break;
                case MCI_DGV_STATUS_HWND:
                    value = d->window;
                    break;
                case MCI_DGV_STATUS_VIDEO_X_EXTENT:
                    value = d->info.width;
                    break;
                case MCI_DGV_STATUS_VIDEO_Y_EXTENT:
                    value = d->info.height;
                    break;
                default:
                    lx_host_monitor_unlock(monitor);
                    return MCIERR_UNSUPPORTED_FUNCTION;
            }
        }
        else
        {
            switch (item)
            {
                case MCI_GETDEVCAPS_CAN_PLAY:
                case MCI_GETDEVCAPS_CAN_SETVOLUME:
                case MCI_GETDEVCAPS_HAS_VIDEO:
                case MCI_GETDEVCAPS_USES_FILES:
                case MCI_DGV_GETDEVCAPS_CAN_STRETCH:
                case MCI_DGV_GETDEVCAPS_CAN_DISTORT:
                    value = 1;
                    break;
                case MCI_GETDEVCAPS_CAN_EJECT:
                case MCI_GETDEVCAPS_CAN_LOCKEJECT:
                case MCI_GETDEVCAPS_CAN_PROCESS_INTERNAL:
                case MCI_GETDEVCAPS_CAN_RECORD:
                case MCI_GETDEVCAPS_CAN_RECORD_INSERT:
                case MCI_GETDEVCAPS_CAN_SAVE:
                case MCI_GETDEVCAPS_CAN_STREAM:
                case MCI_GETDEVCAPS_HAS_IMAGE:
                case MCI_GETDEVCAPS_CAN_CLOSE_DOOR:
                case MCI_VID_GETDEVCAPS_CAN_FREEZE:
                case MCI_DGV_GETDEVCAPS_OVERLAY_GRAPHICS:
                case MCI_DGV_GETDEVCAPS_CAN_REVERSE:
                    value = 0;
                    break;
                case MCI_DGV_GETDEVCAPS_MAX_WINDOWS:
                    value = 1;
                    break;
                case MCI_GETDEVCAPS_DEVICE_TYPE:
                    value = MCI_DEVTYPE_DIGITAL_VIDEO;
                    break;
                case MCI_GETDEVCAPS_HAS_AUDIO:
                    value = d->info.audio_present;
                    break;
                case MCI_DGV_GETDEVCAPS_VIDEO_X_EXTENT:
                case MCI_DGV_GETDEVCAPS_IMAGE_X_EXTENT:
                    value = d->info.width;
                    break;
                case MCI_DGV_GETDEVCAPS_VIDEO_Y_EXTENT:
                case MCI_DGV_GETDEVCAPS_IMAGE_Y_EXTENT:
                    value = d->info.height;
                    break;
                default:
                    lx_host_monitor_unlock(monitor);
                    return MCIERR_UNSUPPORTED_FUNCTION;
            }
        }
        lx_host_monitor_unlock(monitor);
        lx_guest_store_u32(p + MCI_STATUS_PARMS_RETURN, value);
        return 0;
    }
    if (message == MCI_PAUSE || message == MCI_RESUME)
    {
        lx_host_monitor_lock(monitor);
        if (message == MCI_PAUSE && d->mode == MCI_MODE_PLAY)
        {
            d->mode = MCI_MODE_PAUSE;
            d->paused_at = lx_host_video_ticks_ms();
            if (d->audio)
            {
                d->sound.paused = 1;
            }
        }
        else if (message == MCI_RESUME && d->mode == MCI_MODE_PAUSE)
        {
            d->started += lx_host_video_ticks_ms() - d->paused_at;
            d->mode = MCI_MODE_PLAY;
            if (d->audio)
            {
                d->sound.paused = 0;
            }
            lx_host_monitor_wake_all(monitor);
        }
        else
        {
            lx_host_monitor_unlock(monitor);
            return MCIERR_DEVICE_NOT_READY;
        }
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == MCI_SEEK)
    {
        if ((flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_TO_START | MCI_TO_END)) ||
            (flags & (MCI_TO_START | MCI_TO_END)) == (MCI_TO_START | MCI_TO_END))
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (!d->decoder)
            return MCIERR_DEVICE_NOT_READY;
        stop(d);
        int64_t at = flags & MCI_TO_START ? 0
                     : flags & MCI_TO_END
                         ? d->info.duration_ms
                         : milliseconds(d, lx_guest_load_u32(p + MCI_SEEK_PARMS_TO));
        if (at < 0 || (d->info.duration_ms > 0 && at > d->info.duration_ms))
            return MCIERR_OUTOFRANGE;
        if (!lx_video_decoder_seek(d->decoder, at))
            return MCIERR_FILE_IO;
        d->position = at;
        return 0;
    }
    if (message == MCI_PLAY)
    {
        if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_FROM | MCI_TO))
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (!d->decoder)
            return MCIERR_DEVICE_NOT_READY;
        stop_reason(d, MCI_NOTIFY_SUPERSEDED);
        if (!d->target && !create_window(d))
            return MCIERR_DEVICE_NOT_READY;
        int64_t from = flags & MCI_FROM
                           ? milliseconds(d, lx_guest_load_u32(p + MCI_PLAY_PARMS_FROM))
                           : d->position;
        int64_t to = flags & MCI_TO ? milliseconds(d, lx_guest_load_u32(p + MCI_PLAY_PARMS_TO))
                     : d->info.duration_ms > 0 ? d->info.duration_ms
                                               : -1;
        if (from < 0 || ((flags & MCI_TO) && to < 0) || (to >= 0 && to <= from) ||
            (d->info.duration_ms > 0 && (from > d->info.duration_ms || to > d->info.duration_ms)))
            return MCIERR_OUTOFRANGE;
        if (!lx_video_decoder_seek(d->decoder, from))
            return MCIERR_FILE_IO;
        if (!start_audio(d, from, to))
            return MCIERR_DEVICE_NOT_READY;
        lx_host_monitor_lock(monitor);
        d->position = from;
        d->until = to;
        d->stop = 0;
        d->error = 0;
        d->mode = MCI_MODE_PLAY;
        d->notify = flags & MCI_NOTIFY;
        d->notify_window = lx_guest_load_u32(p + MCI_PARMS_CALLBACK);
        d->user = user;
        d->started = lx_host_video_ticks_ms() - (uint64_t)from;
        d->thread = lx_host_thread_create(play_worker, d, NULL);
        if (d->audio && d->thread)
        {
            d->sound.paused = 0;
        }
        if (!d->thread)
        {
            d->mode = MCI_MODE_STOP;
            lx_host_monitor_unlock(monitor);
            stop(d);
            return MCIERR_OUT_OF_MEMORY;
        }
        lx_host_monitor_unlock(monitor);
        if (flags & MCI_WAIT)
        {
            HAB hab = WinQueryAnchorBlock(d->window);
            for (;;)
            {
                lx_host_monitor_lock(monitor);
                int playing = d->mode != MCI_MODE_STOP && d->id == id;
                lx_host_monitor_unlock(monitor);
                lx_pm_video_pump_current();
                if (!playing)
                    break;
                QMSG message;
                while (WinPeekMsg(hab, &message, 0, 0, 0, PM_REMOVE))
                    WinDispatchMsg(hab, &message);
                lx_host_sleep_ms(2);
            }
            stop(d);
            return lx_pm_video_ready(d->target) ? d->error : MCIERR_FILE_IO;
        }
        return 0;
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}

ULONG lx_mci_video_command(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user,
                           int *handled)
{
    ULONG result = video_command(id, message, flags, parameters, user, handled);
    if (*handled && !result && (flags & MCI_NOTIFY) && message != MCI_PLAY && parameters)
    {
        ULONG window = lx_guest_load_u32((uint8 *)parameters + MCI_PARMS_CALLBACK);
        USHORT device = message == MCI_OPEN
                            ? lx_guest_load_u16((uint8 *)parameters + MCI_OPEN_PARMS_DEVICE_ID)
                            : id;
        if (window)
            WinPostMsg(window, MM_MCINOTIFY,
                       (MPARAM)(uintptr_t)((ULONG)device << 16 | MCI_NOTIFY_SUCCESSFUL),
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

#include "os2/mm/mmpm/mci/mdm/mdm_video_string.h"
