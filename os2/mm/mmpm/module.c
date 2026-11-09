/* Public domain. No rights reserved. Wholly AI-authored. */
/* DART queues retain PCM until playback completion. Callbacks run off the SDL thread. */
#include "os2/include/private/export.h"
#include "os2/include/mmioos2.h"
#include "os2/include/private/os2mep.h"
#include "os2/mm/mmpm/mci/mdm/mdm_video.h"
#include "emulator/audio_mixer.h"
#include "os2/mm/midi/midi_synth.h"
#include "emulator/features.h"
#include "os2/mm/midi/midi_sequence.h"
#include "os2/include/pmwin.h"
#include "host/sync.h"
#include <math.h>
#define DEVICE_COUNT 32

/* DART limits: buffers per device and bytes per buffer; MCI_MIXSETUP suggests
   three 4 KiB buffers. */
enum
{
    MIX_BUFFER_MAX = 64,
    MIX_BUFFER_BYTES_MAX = 4 * 1024 * 1024,
    MIX_SUGGESTED_BUFFER_SIZE = 4096,
    MIX_SUGGESTED_BUFFERS = 3
};

typedef struct Packet
{
    struct Packet *next;
    float *samples;
    ULONG count, record;
    double position;
} Packet;

typedef struct Voice
{
    double phase;
    float level;
    unsigned char channel, key;
} Voice;

typedef struct Device
{
    USHORT id;
    ULONG rate, bits, channels, callback, notify_window, notify_user;
    int kind, stopped, paused, notify, ending;
    float volume;
    Packet *head, *tail, *done, *done_tail;
    LxHostThread *thread;
    LxGuestMapping *buffers[MIX_BUFFER_MAX];
    unsigned buffer_count, pending;
    LxMidiSequence sequence;
    size_t next_event;
    double time;
    Voice voices[64];
    LxMidiSynth *synth;
} Device;

static Device devices[DEVICE_COUNT];
static USHORT next_id = 1;
static LxHostMonitor *monitor;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static int audio_registered;
#if TWOINE_PORTABLE_LOADER
static ULONG write_entry;
static int initialize_write_bridge(void);
#else
static LONG OS2API mixWrite(ULONG handle, PVOID records, ULONG count);
#endif
static void initialize(void)
{
    monitor = lx_host_monitor_create();
}

static Device *find_device(ULONG handle)
{
    for (unsigned i = 0; i < DEVICE_COUNT; i++)
        if (devices[i].id == handle && handle)
            return &devices[i];
    return NULL;
}

static void midi_event(Device *d, const LxMidiEvent *e)
{
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
    if (e->sysex)
        lx_midi_synth_sysex(d->synth, e->sysex, e->sysex_size);
    else if (!e->tempo)
        lx_midi_synth_event(d->synth, e->status, e->key, e->value);
    return;
#endif
    unsigned type = e->status & 0xf0, channel = e->status & 15;
    if (type == 0xb0 && (e->key == 120 || e->key == 123))
    {
        for (unsigned i = 0; i < 64; i++)
            if (d->voices[i].channel == channel)
                d->voices[i].level = 0;
    }
    else if (type == 0x80 || (type == 0x90 && !e->value))
    {
        for (unsigned i = 0; i < 64; i++)
            if (d->voices[i].channel == channel && d->voices[i].key == e->key)
                d->voices[i].level = 0;
    }
    else if (type == 0x90)
    {
        unsigned i;
        for (i = 0; i < 63 && d->voices[i].level; i++)
        {
        }
        d->voices[i] = (Voice){.channel = channel, .key = e->key, .level = e->value / 127.0f};
    }
}

static void generate_midi(Device *d, float *samples, int count, int frequency)
{
    if (d->time >= d->sequence.duration)
        return;
    for (int n = 0; n < count && d->time < d->sequence.duration;)
    {
        while (d->next_event < d->sequence.count &&
               d->sequence.events[d->next_event].time <= d->time)
            midi_event(d, &d->sequence.events[d->next_event++]);
        double boundary = d->sequence.duration;
        if (d->next_event < d->sequence.count && d->sequence.events[d->next_event].time < boundary)
            boundary = d->sequence.events[d->next_event].time;
        int block = count - n;
        double remaining = ceil((boundary - d->time) * frequency);
        if (remaining < block)
            block = remaining > 1 ? (int)remaining : 1;
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
        lx_midi_synth_render_stereo(d->synth, samples + 2 * n, (unsigned)block, (unsigned)frequency,
                                    d->volume);
#else
        for (int frame = 0; frame < block; frame++)
        {
            float value = 0;
            for (unsigned v = 0; v < 64; v++)
                if (d->voices[v].level)
                {
                    Voice *voice = &d->voices[v];
                    double hz = 440.0 * pow(2.0, ((int)voice->key - 69) / 12.0);
                    value += (float)sin(voice->phase) * voice->level * 0.08f;
                    voice->phase += 6.283185307179586 * hz / frequency;
                    if (voice->phase > 6.283185307179586)
                        voice->phase -= 6.283185307179586;
                }
            samples[2 * (n + frame)] += value * d->volume;
            samples[2 * (n + frame) + 1] += value * d->volume;
        }
#endif
        n += block;
        d->time += (double)block / frequency;
    }
    if (d->time >= d->sequence.duration)
    {
        d->ending = 1;
        lx_host_monitor_wake_all(monitor);
    }
}

static int generate(void *unused, float *samples, int count, int frequency)
{
    (void)unused;
    lx_host_monitor_lock(monitor);
    for (unsigned i = 0; i < DEVICE_COUNT; i++)
    {
        Device *d = &devices[i];
        if (!d->id || d->stopped || d->paused)
            continue;
        if (d->kind == 2)
        {
            generate_midi(d, samples, count, frequency);
            continue;
        }
        for (int n = 0; n < count; n++)
        {
            if (d->kind == 1 && d->head)
            {
                Packet *p = d->head;
                size_t source = (size_t)p->position * 2;
                samples[2 * n] += p->samples[source] * d->volume;
                samples[2 * n + 1] += p->samples[source + 1] * d->volume;
                p->position += (double)d->rate / frequency;
                if (p->position >= p->count)
                {
                    d->head = p->next;
                    if (!d->head)
                        d->tail = NULL;
                    p->next = NULL;
                    if (d->done_tail)
                        d->done_tail->next = p;
                    else
                        d->done = p;
                    d->done_tail = p;
                    lx_host_monitor_wake_all(monitor);
                }
            }
        }
    }
    lx_host_monitor_unlock(monitor);
    return 1;
}

static void free_packets(Packet *p)
{
    while (p)
    {
        Packet *next = p->next;
        free(p->samples);
        free(p);
        p = next;
    }
}

static void *delivery(void *argument)
{
    Device *d = argument;
    uint8 tibspace[LXTIBSIZE] = {0};
    unsigned char stack_marker;
    GProcessState.initOs2Tib(tibspace, &stack_marker, 65536, 0xff00u + d->id);
    uint16 selector = GProcessState.setOs2Tib(tibspace);
    lx_host_monitor_lock(monitor);
    while (!d->stopped)
    {
        Packet *p = d->done;
        if (p)
        {
            d->done = p->next;
            if (!d->done)
                d->done_tail = NULL;
            if (d->pending)
                d->pending--;
            ULONG callback = d->callback;
            lx_host_monitor_unlock(monitor);
            if (callback)
                lx_execution_callback32_4(GProcessState.execution, callback, 0, p->record,
                                          MIX_WRITE_COMPLETE, 0);
            free(p->samples);
            free(p);
            lx_host_monitor_lock(monitor);
        }
        else if (d->ending)
        {
            d->ending = 0;
            ULONG window = d->notify_window, user = d->notify_user;
            int notify = d->notify;
            lx_host_monitor_unlock(monitor);
            if (notify && window)
                WinPostMsg(window, MM_MCINOTIFY,
                           (MPARAM)(uintptr_t)(d->id << 16 | MCI_NOTIFY_SUCCESSFUL),
                           (MPARAM)(uintptr_t)user);
            lx_host_monitor_lock(monitor);
        }
        else
            lx_host_monitor_wait(monitor, 100);
    }
    lx_host_monitor_unlock(monitor);
    GProcessState.deinitOs2Tib(selector);
    return NULL;
}

static ULONG start_audio(Device *d)
{
    if (!audio_registered)
    {
        if (!GProcessState.registerStereoAudioGenerator(generate, NULL, 1))
            return MCIERR_DEVICE_NOT_READY;
        audio_registered = 1;
    }
    if (!d->thread)
    {
        lx_host_monitor_lock(monitor);
        d->stopped = 0;
        lx_host_monitor_unlock(monitor);
        d->thread = lx_host_thread_create(delivery, d, NULL);
        if (!d->thread)
            return MCIERR_OUT_OF_MEMORY;
        __atomic_store_n(&GProcessState.ne_threads_started, 1, __ATOMIC_RELEASE);
    }
    return 0;
}

static LONG queue_buffers(ULONG handle, ULONG address, ULONG count)
{
    if (!count || count > MIX_BUFFER_MAX)
        return MCIERR_INVALID_BUFFER;
    lx_host_monitor_lock(monitor);
    Device *d = find_device(handle);
    if (!d || d->kind != 1 || !d->rate)
    {
        lx_host_monitor_unlock(monitor);
        return MCIERR_INVALID_DEVICE_ID;
    }
    if (d->stopped || count > MIX_BUFFER_MAX - d->pending)
    {
        lx_host_monitor_unlock(monitor);
        return MCIERR_DEVICE_NOT_READY;
    }
    Packet *head = NULL, *tail = NULL;
    unsigned submitted = 0;
    for (ULONG i = 0; i < count; i++)
    {
        unsigned char record[MCI_MIX_BUFFER_SIZE];
        if (!lx_execution_read(GProcessState.execution, address + i * MCI_MIX_BUFFER_SIZE, record,
                               MCI_MIX_BUFFER_SIZE))
            goto fail;
        ULONG length = lx_guest_load_u32(record + MCI_MIX_BUFFER_BUFFER_LENGTH),
              data = lx_guest_load_u32(record + MCI_MIX_BUFFER_BUFFER);
        if (length > MIX_BUFFER_BYTES_MAX)
            goto fail;
        if (!length)
            continue;
        unsigned bytes_per_frame = d->channels * (d->bits / 8);
        ULONG frames = length / bytes_per_frame;
        if (!frames)
            continue;
        unsigned char *pcm = malloc(length);
        Packet *p = calloc(1, sizeof *p);
        if (!pcm || !p)
        {
            free(pcm);
            free(p);
            goto fail;
        }
        p->samples = malloc(frames * 2 * sizeof(float));
        p->count = frames;
        p->record = address + i * MCI_MIX_BUFFER_SIZE;
        if (!p->samples || !lx_execution_read(GProcessState.execution, data, pcm, length))
        {
            free(p->samples);
            free(p);
            free(pcm);
            goto fail;
        }
        for (ULONG frame = 0; frame < frames; frame++)
        {
            for (ULONG c = 0; c < 2; c++)
            {
                ULONG off = (frame * d->channels + (d->channels == 1 ? 0 : c)) * (d->bits / 8);
                p->samples[frame * 2 + c] =
                    d->bits == 8 ? ((int)pcm[off] - 128) / 128.0f
                                 : (int16_t)(pcm[off] | pcm[off + 1] << 8) / 32768.0f;
            }
        }
        free(pcm);
        if (tail)
            tail->next = p;
        else
            head = p;
        tail = p;
        submitted++;
    }
    d->pending += submitted;
    if (head)
    {
        if (d->tail)
            d->tail->next = head;
        else
            d->head = head;
        d->tail = tail;
    }
    lx_host_monitor_unlock(monitor);
    return 0;
fail:
    free_packets(head);
    lx_host_monitor_unlock(monitor);
    return MCIERR_INVALID_BUFFER;
}

#if !TWOINE_PORTABLE_LOADER
static LONG OS2API mixWrite(ULONG handle, PVOID records, ULONG count)
{
    return queue_buffers(handle, (ULONG)(uintptr_t)records, count);
}
#endif
#if TWOINE_PORTABLE_LOADER
static size_t parameter_size(USHORT message, ULONG flags)
{
    switch (message)
    {
        case MCI_OPEN:
            return flags & MCI_DGV_OPEN_PARENT ? MCI_DGV_OPEN_PARMS_SIZE : MCI_OPEN_PARMS_SIZE;
        case MCI_MIXSETUP:
            return MCI_MIXSETUP_PARMS_SIZE;
        case MCI_BUFFER:
            return MCI_BUFFER_PARMS_SIZE;
        case MCI_SET:
            return MCI_SET_PARMS_SIZE;
        case MCI_PLAY:
            return flags & MCI_DGV_PLAY_SPEED ? MCI_DGV_PLAY_PARMS_SIZE : MCI_PLAY_PARMS_SIZE;
        case MCI_SEEK:
            return MCI_SEEK_PARMS_SIZE;
        case MCI_LOAD:
            return MCI_LOAD_PARMS_SIZE;
        case MCI_GETDEVCAPS:
            return MCI_GETDEVCAPS_PARMS_SIZE;
        case MCI_STATUS:
            return MCI_STATUS_PARMS_SIZE;
        case MCI_PUT:
        case MCI_WHERE:
            return MCI_VID_RECT_PARMS_SIZE;
        case MCI_WINDOW:
            return MCI_VID_WINDOW_PARMS_SIZE;
        default:
            return MCI_GENERIC_PARMS_SIZE;
    }
}
#endif
ULONG mciSendCommand(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user)
{
    TRACE_NATIVE("mciSendCommand(%u,%u,0x%x,%p,%u)", id, message, (unsigned)flags, parameters,
                 user);
    if (!parameters)
        return MCIERR_INVALID_BUFFER;
    int video_handled = 0;
    ULONG video_result = lx_mci_video_command(id, message, flags, parameters, user, &video_handled);
    if (video_handled)
        return video_result;
    unsigned char *p = parameters;
    lx_host_once(&once, initialize);
    if (message == MCI_OPEN)
    {
        int kind = 0;
        if ((flags & MCI_OPEN_TYPE_ID) &&
            lx_guest_load_u32(p + MCI_OPEN_PARMS_DEVICE_TYPE) == MCI_DEVTYPE_AUDIO_AMPMIX)
            kind = 1;
        else if (flags & MCI_OPEN_MMIO)
            kind = 2;
        if (!kind)
            return MCIERR_DEVICE_NOT_READY;
        if (!lxRuntimeFeatureEnabled(kind == 1 ? LX_FEATURE_PCM : LX_FEATURE_MIDI))
        {
            fprintf(stderr,
                    "2ine: %s playback is disabled in this ball's Graphics & Media settings\n",
                    kind == 1 ? "PCM audio" : "MIDI");
            return MCIERR_DEVICE_NOT_READY;
        }
        lx_host_monitor_lock(monitor);
        Device *d = NULL;
        for (unsigned i = 0; i < DEVICE_COUNT; i++)
            if (!devices[i].id)
            {
                d = &devices[i];
                break;
            }
        if (!d || !next_id || next_id >= 0x8000)
        {
            lx_host_monitor_unlock(monitor);
            return MCIERR_OUT_OF_MEMORY;
        }
        memset(d, 0, sizeof *d);
        d->id = next_id++;
        d->kind = kind;
        d->volume = 1;
        d->stopped = 1;
        if (kind == 2)
        {
            size_t size = 0;
            /* With MCI_OPEN_MMIO the element name is an open HMMIO. */
            unsigned char *data =
                lx_mmio_copy(lx_guest_load_u32(p + MCI_OPEN_PARMS_ELEMENT_NAME), &size);
            int ok = data && lx_midi_sequence_parse(&d->sequence, data, size);
            free(data);
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
            if (ok)
            {
                d->synth = lx_midi_synth_create();
                if (!d->synth)
                {
                    lx_midi_sequence_clear(&d->sequence);
                    memset(d, 0, sizeof *d);
                    lx_host_monitor_unlock(monitor);
                    return MCIERR_DEVICE_NOT_READY;
                }
            }
#else
            fprintf(stderr, "2ine: basic oscillator MIDI fallback selected at build time.\n");
#endif
            if (!ok)
            {
                lx_midi_sequence_clear(&d->sequence);
                memset(d, 0, sizeof *d);
                lx_host_monitor_unlock(monitor);
                return MCIERR_INVALID_BUFFER;
            }
        }
        lx_guest_store_u16(p + MCI_OPEN_PARMS_DEVICE_ID, d->id);
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    Device *d = find_device(id);
    if (!d)
        return MCIERR_INVALID_DEVICE_ID;
    if (message == MCI_MIXSETUP)
    {
        if (flags & MCI_MIXSETUP_DEINIT)
            return mciSendCommand(id, MCI_STOP, 0, p, 0);
        if (!(flags & MCI_MIXSETUP_INIT))
            return MCIERR_UNSUPPORTED_FUNCTION;
        ULONG bits = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_BITS_PER_SAMPLE),
              channels = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_CHANNELS),
              rate = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_SAMPLES_PER_SEC);
        if (d->kind != 1 || (bits != 8 && bits != 16) || !channels || channels > 2 || rate < 4000 ||
            rate > 192000 ||
            lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_FORMAT_TAG) != MCI_WAVE_FORMAT_PCM ||
            lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_FORMAT_MODE) != MCI_PLAY)
            return MCIERR_INVALID_BUFFER;
        lx_host_monitor_lock(monitor);
        d->bits = bits;
        d->channels = channels;
        d->rate = rate;
        d->callback = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_MIX_EVENT);
        lx_host_monitor_unlock(monitor);
#if TWOINE_PORTABLE_LOADER
        if (!write_entry && !initialize_write_bridge())
            return MCIERR_OUT_OF_MEMORY;
        lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_MIX_WRITE, write_entry);
#else
        lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_MIX_WRITE, (ULONG)(uintptr_t)mixWrite);
#endif
        lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_MIX_HANDLE, d->id);
        lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_BUFFER_SIZE, MIX_SUGGESTED_BUFFER_SIZE);
        lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_NUM_BUFFERS, MIX_SUGGESTED_BUFFERS);
        return start_audio(d);
    }
    if (message == MCI_BUFFER)
    {
        ULONG count = lx_guest_load_u32(p + MCI_BUFFER_PARMS_NUM_BUFFERS),
              size = lx_guest_load_u32(p + MCI_BUFFER_PARMS_BUFFER_SIZE),
              list = lx_guest_load_u32(p + MCI_BUFFER_PARMS_BUFFER_LIST);
        if (!count || count > MIX_BUFFER_MAX || !size || size > MIX_BUFFER_BYTES_MAX)
            return MCIERR_INVALID_BUFFER;
        if (flags & MCI_ALLOCATE_MEMORY)
        {
            if (d->buffer_count)
                return MCIERR_INVALID_BUFFER;
            unsigned char records[MIX_BUFFER_MAX * MCI_MIX_BUFFER_SIZE] = {0};
            for (ULONG i = 0; i < count; i++)
            {
                d->buffers[i] = lx_execution_map(GProcessState.execution, 0, (size + 4095) & ~4095u,
                                                 LX_GUEST_READ | LX_GUEST_WRITE, 0);
                if (!d->buffers[i])
                {
                    for (ULONG j = 0; j < i; j++)
                        lx_execution_unmap(GProcessState.execution, d->buffers[j]);
                    memset(d->buffers, 0, sizeof d->buffers);
                    return MCIERR_OUT_OF_MEMORY;
                }
                unsigned char *record = records + i * MCI_MIX_BUFFER_SIZE;
                lx_guest_store_u32(record + MCI_MIX_BUFFER_STRUCT_LENGTH, MCI_MIX_BUFFER_SIZE);
                lx_guest_store_u32(record + MCI_MIX_BUFFER_BUFFER,
                                   lx_execution_mapping_address(d->buffers[i]));
                lx_guest_store_u32(record + MCI_MIX_BUFFER_BUFFER_LENGTH, size);
            }
            if (!lx_execution_write(GProcessState.execution, list, records,
                                    count * MCI_MIX_BUFFER_SIZE))
            {
                for (ULONG i = 0; i < count; i++)
                    lx_execution_unmap(GProcessState.execution, d->buffers[i]);
                memset(d->buffers, 0, sizeof d->buffers);
                return MCIERR_INVALID_BUFFER;
            }
            d->buffer_count = count;
            return 0;
        }
        if (flags & MCI_DEALLOCATE_MEMORY)
        {
            /* Retire the stream before releasing caller-visible buffers. */
            mciSendCommand(id, MCI_STOP, 0, p, 0);
            for (unsigned i = 0; i < d->buffer_count; i++)
                lx_execution_unmap(GProcessState.execution, d->buffers[i]);
            memset(d->buffers, 0, sizeof d->buffers);
            d->buffer_count = 0;
            return 0;
        }
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
    if (message == MCI_SET && (flags & MCI_SET_VOLUME))
    {
        ULONG level = lx_guest_load_u32(p + MCI_SET_PARMS_LEVEL);
        if (level > 100)
            return MCIERR_INVALID_BUFFER;
        lx_host_monitor_lock(monitor);
        d->volume = level / 100.0f;
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == MCI_PLAY && d->kind == 2)
    {
        lx_host_monitor_lock(monitor);
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
        if (!lx_midi_synth_reset(d->synth))
        {
            lx_host_monitor_unlock(monitor);
            return MCIERR_OUT_OF_MEMORY;
        }
#endif
        d->time = 0;
        d->next_event = 0;
        memset(d->voices, 0, sizeof d->voices);
        d->paused = 0;
        d->notify = flags & MCI_NOTIFY;
        d->notify_window = lx_guest_load_u32(p + MCI_PARMS_CALLBACK);
        d->notify_user = user;
        d->ending = 0;
        lx_host_monitor_unlock(monitor);
        return start_audio(d);
    }
    if (message == MCI_PAUSE || message == MCI_RESUME)
    {
        lx_host_monitor_lock(monitor);
        d->paused = message == MCI_PAUSE;
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    if (message == MCI_STOP || message == MCI_CLOSE)
    {
        lx_host_monitor_lock(monitor);
        d->stopped = 1;
        d->ending = 0;
        lx_host_monitor_wake_all(monitor);
        lx_host_monitor_unlock(monitor);
        if (d->thread)
        {
            lx_host_thread_join(d->thread, NULL);
            d->thread = NULL;
        }
        lx_host_monitor_lock(monitor);
        free_packets(d->head);
        free_packets(d->done);
        d->head = d->tail = d->done = d->done_tail = NULL;
        d->pending = 0;
        if (message == MCI_CLOSE)
        {
            for (unsigned i = 0; i < d->buffer_count; i++)
                lx_execution_unmap(GProcessState.execution, d->buffers[i]);
            lx_midi_sequence_clear(&d->sequence);
            lx_midi_synth_destroy(d->synth);
            memset(d, 0, sizeof *d);
        }
        lx_host_monitor_unlock(monitor);
        return 0;
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}

ULONG mciSendString(PSZ command, PSZ output, USHORT length, ULONG window, USHORT user)
{
    TRACE_NATIVE("mciSendString(%s)", command ? command : "<null>");
    if (output && length)
        output[0] = 0;
    int video_handled = 0;
    ULONG result = lx_mci_video_string(command, output, length, window, user, &video_handled);
    return video_handled ? result : MCIERR_DEVICE_NOT_READY;
}

ULONG mciGetErrorString(ULONG error, PSZ output, USHORT length)
{
    if (!output || !length)
        return 0;
    snprintf(output, length, "MCI error %lu%s", (unsigned long)error,
             error == MCIERR_DEVICE_NOT_READY ? ": device unavailable" : "");
    return 1;
}

static void shutdown_mdm(void)
{
    lx_mci_video_shutdown();
    if (audio_registered)
    {
        lxUnregisterAudioGenerator(generate, NULL);
        audio_registered = 0;
    }
    if (monitor)
        for (unsigned i = 0; i < DEVICE_COUNT; i++)
            if (devices[i].id)
            {
                unsigned char parameters[MCI_GENERIC_PARMS_SIZE] = {0};
                mciSendCommand(devices[i].id, MCI_CLOSE, 0, parameters, 0);
            }
}

#define LX_NATIVE_MODULE_SHUTDOWN() shutdown_mdm()
#include "os2/mm/mmpm/generated/mmpm-lx.h"
