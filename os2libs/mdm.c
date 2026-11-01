/* Public domain. No rights reserved. Wholly AI-authored. */
/* DART queues retain PCM until playback completion. Callbacks run off the SDL thread. */
#include "os2native.h"
#include "mdm.h"
#include "mmio.h"
#include "os2libs/midi_synth.h"
#include "os2libs/midi_sequence.h"
#include "pmwin.h"
#include "hal/host_sync.h"
#include "hal/host.h"
#include <math.h>
#define DEVICE_COUNT 32
#define MCIERR_INVALID_DEVICE_ID 5001
#define MCIERR_UNSUPPORTED_FUNCTION 5018
#define MCIERR_DEVICE_NOT_READY 5020
#define MCIERR_INVALID_BUFFER 5050
#define MCIERR_OUT_OF_MEMORY 5008
#define MCI_OPEN 1
#define MCI_CLOSE 2
#define MCI_PLAY 4
#define MCI_STOP 6
#define MCI_PAUSE 7
#define MCI_SET 12
#define MCI_RESUME 29
#define MCI_BUFFER 62
#define MCI_MIXSETUP 63
#define MCI_OPEN_TYPE_ID 0x1000
#define MCI_OPEN_MMIO 0x4000
#define MCI_SET_VOLUME 0x80000
#define MCI_ALLOCATE_MEMORY 0x40000
#define MCI_DEALLOCATE_MEMORY 0x80000
#define MCI_MIXSETUP_INIT 0x10000
#define MCI_MIXSETUP_DEINIT 0x20000
#define MIX_WRITE_COMPLETE 2

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
    LxGuestMapping *buffers[64];
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
#define MIDI_SYSEX_MESSAGE_MAX (1024u * 1024u)
#define MIDI_SYSEX_TOTAL_MAX (32u * 1024u * 1024u)
#define MIDI_EVENT_MAX 262144u
typedef struct MidiParser
{
    LxMidiSequence *sequence;
    size_t capacity, payload_total;
    unsigned char *pending;
    size_t pending_size;
} MidiParser;
void lx_midi_sequence_clear(LxMidiSequence *sequence)
{
    if (!sequence)
        return;
    for (size_t i = 0; i < sequence->count; i++)
        free(sequence->events[i].sysex);
    free(sequence->events);
    memset(sequence, 0, sizeof *sequence);
}
static uint32_t big32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static int vlq(const unsigned char **p, const unsigned char *end, uint32_t *v)
{
    *v = 0;
    for (unsigned i = 0; i < 4 && *p < end; i++)
    {
        unsigned c = *(*p)++;
        *v = (*v << 7) | (c & 127);
        if (!(c & 128))
            return 1;
    }
    return 0;
}
static int compare_event(const void *a, const void *b)
{
    uint32_t x = ((const LxMidiEvent *)a)->tick, y = ((const LxMidiEvent *)b)->tick;
    if (x != y)
        return x < y ? -1 : 1;
    uint32_t ax = ((const LxMidiEvent *)a)->order, by = ((const LxMidiEvent *)b)->order;
    return ax < by ? -1 : ax > by;
}
/* Ownership of event.sysex transfers on success and is released on failure. */
static int append_event(MidiParser *parser, LxMidiEvent event)
{
    LxMidiSequence *sequence = parser->sequence;
    if (event.sysex_size > MIDI_SYSEX_TOTAL_MAX - parser->payload_total)
        goto fail;
    if (sequence->count == parser->capacity)
    {
        if (parser->capacity >= MIDI_EVENT_MAX)
            goto fail;
        size_t capacity = parser->capacity ? parser->capacity * 2 : 1024;
        LxMidiEvent *events = realloc(sequence->events, capacity * sizeof *events);
        if (!events)
            goto fail;
        sequence->events = events;
        parser->capacity = capacity;
    }
    event.order = (uint32_t)sequence->count;
    sequence->events[sequence->count++] = event;
    parser->payload_total += event.sysex_size;
    return 1;
fail:
    free(event.sysex);
    return 0;
}
static int append_fragment(MidiParser *parser, const unsigned char *bytes, size_t size)
{
    if (size > MIDI_SYSEX_MESSAGE_MAX - parser->pending_size)
        return 0;
    unsigned char *message = realloc(parser->pending, parser->pending_size + size);
    if (!message && parser->pending_size + size)
        return 0;
    parser->pending = message;
    if (size)
        memcpy(message + parser->pending_size, bytes, size);
    parser->pending_size += size;
    return 1;
}
static int complete_sysex(MidiParser *parser, uint32_t tick)
{
    LxMidiEvent event = {
        .tick = tick, .status = 0xf0, .sysex = parser->pending, .sysex_size = parser->pending_size};
    parser->pending = NULL;
    parser->pending_size = 0;
    return append_event(parser, event);
}
static int sysex_packet(MidiParser *parser, uint32_t tick, unsigned status,
                        const unsigned char *bytes, size_t size)
{
    if (status == 0xf0)
    {
        if (parser->pending_size)
            return 0;
        const unsigned char begin = 0xf0;
        if (!append_fragment(parser, &begin, 1))
            return 0;
    }
    if (parser->pending_size)
    {
        for (size_t i = 0; i < size; i++)
            if (bytes[i] >= 0x80 && !(bytes[i] == 0xf7 && i + 1 == size))
                return 0;
        if (!append_fragment(parser, bytes, size))
            return 0;
        return size && bytes[size - 1] == 0xf7 ? complete_sysex(parser, tick) : 1;
    }
    /* An unpaired F7 is an escape: its status byte is not transmitted.
       Preserve complete framed messages, rejecting unsupported raw MIDI bytes. */
    size_t offset = 0;
    while (offset < size)
    {
        if (bytes[offset] != 0xf0)
            return 0;
        size_t end = offset + 1;
        while (end < size && bytes[end] < 0x80)
            end++;
        if (end == size || bytes[end] != 0xf7)
            return 0;
        size_t length = end - offset + 1;
        if (!append_fragment(parser, bytes + offset, length) || !complete_sysex(parser, tick))
            return 0;
        offset = end + 1;
    }
    return 1;
}
static int parse_midi_contents(MidiParser *parser, const unsigned char *bytes, size_t size)
{
    LxMidiSequence *sequence = parser->sequence;
    if (size < 14 || memcmp(bytes, "MThd", 4) || big32(bytes + 4) < 6 ||
        big32(bytes + 4) > size - 8 || bytes[8] || bytes[9] > 1)
        return 0;
    unsigned division = (bytes[12] << 8) | bytes[13];
    if (!division || division & 0x8000)
        return 0;
    unsigned expected_tracks = (bytes[10] << 8) | bytes[11], tracks = 0;
    if (!expected_tracks || expected_tracks > 256 || (!bytes[9] && expected_tracks != 1))
        return 0;
    size_t offset = 8 + big32(bytes + 4);
    uint32_t end_tick = 0;
    while (offset <= size && size - offset >= 8)
    {
        if (++tracks > expected_tracks)
            return 0;
        uint32_t length = big32(bytes + offset + 4);
        if (length > size - offset - 8 || memcmp(bytes + offset, "MTrk", 4))
            return 0;
        const unsigned char *p = bytes + offset + 8, *end = p + length;
        uint32_t tick = 0;
        unsigned running = 0;
        while (p < end)
        {
            uint32_t delta;
            if (!vlq(&p, end, &delta) || tick > UINT32_MAX - delta || p == end)
                return 0;
            tick += delta;
            if (tick > end_tick)
                end_tick = tick;
            unsigned status = *p;
            if (status & 128)
            {
                p++;
                running = status < 0xf0 ? status : 0;
            }
            else
                status = running;
            if (!status)
                return 0;
            LxMidiEvent event = {.tick = tick, .status = status};
            if (status == 0xff)
            {
                if (p == end)
                    return 0;
                unsigned meta = *p++;
                uint32_t n;
                if (!vlq(&p, end, &n) || n > (size_t)(end - p))
                    return 0;
                if (meta == 0x51)
                {
                    if (n < 3)
                        return 0;
                    event.tempo = (p[0] << 16) | (p[1] << 8) | p[2];
                    if (!event.tempo)
                        return 0;
                }
                p += n;
                if (!event.tempo)
                    continue;
            }
            else if (status == 0xf0 || status == 0xf7)
            {
                uint32_t n;
                if (!vlq(&p, end, &n) || n > (size_t)(end - p) ||
                    !sysex_packet(parser, tick, status, p, n))
                    return 0;
                p += n;
                continue;
            }
            else
            {
                if (status >= 0xf0 || parser->pending_size)
                    return 0;
                unsigned n = (status & 0xe0) == 0xc0 ? 1 : 2;
                if (n > (size_t)(end - p))
                    return 0;
                event.key = *p++;
                if (n == 2)
                    event.value = *p++;
                if ((event.key | event.value) & 128)
                    return 0;
                unsigned type = status & 0xf0;
                if (type != 0x80 && type != 0x90 && type != 0xb0 && type != 0xc0 && type != 0xe0)
                    continue;
            }
            if (!append_event(parser, event))
                return 0;
        }
        if (parser->pending_size)
            return 0;
        offset += 8 + length;
    }
    if (tracks != expected_tracks || offset != size)
        return 0;
    if (sequence->count > 1)
        qsort(sequence->events, sequence->count, sizeof *sequence->events, compare_event);
    uint32_t tick = 0, tempo = 500000;
    double time = 0;
    for (size_t i = 0; i < sequence->count; i++)
    {
        LxMidiEvent *event = &sequence->events[i];
        time += (event->tick - tick) * (double)tempo / (division * 1000000.0);
        tick = event->tick;
        event->time = time;
        if (event->tempo)
            tempo = event->tempo;
    }
    sequence->duration = time + (end_tick - tick) * (double)tempo / (division * 1000000.0) + 0.1;
    return 1;
}
int lx_midi_sequence_parse(LxMidiSequence *sequence, const unsigned char *bytes, size_t size)
{
    if (!sequence)
        return 0;
    memset(sequence, 0, sizeof *sequence);
    MidiParser parser = {.sequence = sequence};
    int result = bytes && parse_midi_contents(&parser, bytes, size);
    free(parser.pending);
    if (!result)
        lx_midi_sequence_clear(sequence);
    return result;
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
    GLoaderState.initOs2Tib(tibspace, &stack_marker, 65536, 0xff00u + d->id);
    uint16 selector = GLoaderState.setOs2Tib(tibspace);
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
                lx_execution_callback32_4(GLoaderState.execution, callback, 0, p->record,
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
                WinPostMsg(window, 0x500, (MPARAM)(uintptr_t)(d->id << 16),
                           (MPARAM)(uintptr_t)user);
            lx_host_monitor_lock(monitor);
        }
        else
            lx_host_monitor_wait(monitor, 100);
    }
    lx_host_monitor_unlock(monitor);
    GLoaderState.deinitOs2Tib(selector);
    return NULL;
}
static ULONG start_audio(Device *d)
{
    if (!audio_registered)
    {
        if (!GLoaderState.registerStereoAudioGenerator(generate, NULL, 1))
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
        __atomic_store_n(&GLoaderState.ne_threads_started, 1, __ATOMIC_RELEASE);
    }
    return 0;
}
static LONG queue_buffers(ULONG handle, ULONG address, ULONG count)
{
    if (!count || count > 64)
        return MCIERR_INVALID_BUFFER;
    lx_host_monitor_lock(monitor);
    Device *d = find_device(handle);
    if (!d || d->kind != 1 || !d->rate)
    {
        lx_host_monitor_unlock(monitor);
        return MCIERR_INVALID_DEVICE_ID;
    }
    if (d->stopped || count > 64 - d->pending)
    {
        lx_host_monitor_unlock(monitor);
        return MCIERR_DEVICE_NOT_READY;
    }
    Packet *head = NULL, *tail = NULL;
    unsigned submitted = 0;
    for (ULONG i = 0; i < count; i++)
    {
        unsigned char record[32];
        if (!lx_execution_read(GLoaderState.execution, address + i * 32, record, 32))
            goto fail;
        ULONG length = lx_guest_load_u32(record + 8), data = lx_guest_load_u32(record + 4);
        if (length > 4 * 1024 * 1024)
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
        p->record = address + i * 32;
        if (!p->samples || !lx_execution_read(GLoaderState.execution, data, pcm, length))
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
static size_t parameter_size(USHORT message)
{
    switch (message)
    {
        case MCI_OPEN:
            return 20;
        case MCI_MIXSETUP:
            return 56;
        case MCI_BUFFER:
            return 32;
        case MCI_SET:
            return 32;
        case MCI_PLAY:
            return 12;
        default:
            return 4;
    }
}
#endif
ULONG mciSendCommand(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user)
{
    TRACE_NATIVE("mciSendCommand(%u,%u,0x%x,%p,%u)", id, message, (unsigned)flags, parameters,
                 user);
    if (!parameters)
        return MCIERR_INVALID_BUFFER;
    unsigned char *p = parameters;
    lx_host_once(&once, initialize);
    if (message == MCI_OPEN)
    {
        int kind = 0;
        if ((flags & MCI_OPEN_TYPE_ID) && lx_guest_load_u32(p + 8) == 9)
            kind = 1;
        else if (flags & MCI_OPEN_MMIO)
            kind = 2;
        if (!kind)
            return MCIERR_DEVICE_NOT_READY;
        lx_host_monitor_lock(monitor);
        Device *d = NULL;
        for (unsigned i = 0; i < DEVICE_COUNT; i++)
            if (!devices[i].id)
            {
                d = &devices[i];
                break;
            }
        if (!d || !next_id)
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
            unsigned char *data = lx_mmio_copy(lx_guest_load_u32(p + 12), &size);
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
        lx_guest_store_u16(p + 4, d->id);
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
        ULONG bits = lx_guest_load_u32(p + 4), channels = lx_guest_load_u32(p + 16),
              rate = lx_guest_load_u32(p + 12);
        if (d->kind != 1 || (bits != 8 && bits != 16) || !channels || channels > 2 || rate < 4000 ||
            rate > 192000 || lx_guest_load_u32(p + 8) != 1 || lx_guest_load_u32(p + 20) != MCI_PLAY)
            return MCIERR_INVALID_BUFFER;
        lx_host_monitor_lock(monitor);
        d->bits = bits;
        d->channels = channels;
        d->rate = rate;
        d->callback = lx_guest_load_u32(p + 40);
        lx_host_monitor_unlock(monitor);
#if TWOINE_PORTABLE_LOADER
        if (!write_entry && !initialize_write_bridge())
            return MCIERR_OUT_OF_MEMORY;
        lx_guest_store_u32(p + 32, write_entry);
#else
        lx_guest_store_u32(p + 32, (ULONG)(uintptr_t)mixWrite);
#endif
        lx_guest_store_u32(p + 28, d->id);
        lx_guest_store_u32(p + 48, 4096);
        lx_guest_store_u32(p + 52, 3);
        return start_audio(d);
    }
    if (message == MCI_BUFFER)
    {
        ULONG count = lx_guest_load_u32(p + 8), size = lx_guest_load_u32(p + 12),
              list = lx_guest_load_u32(p + 28);
        if (!count || count > 64 || !size || size > 4 * 1024 * 1024)
            return MCIERR_INVALID_BUFFER;
        if (flags & MCI_ALLOCATE_MEMORY)
        {
            if (d->buffer_count)
                return MCIERR_INVALID_BUFFER;
            unsigned char records[64 * 32] = {0};
            for (ULONG i = 0; i < count; i++)
            {
                d->buffers[i] =
                    lx_execution_map(GLoaderState.execution, 0, (size + 4095) & ~4095u, 3, 0);
                if (!d->buffers[i])
                {
                    for (ULONG j = 0; j < i; j++)
                        lx_execution_unmap(GLoaderState.execution, d->buffers[j]);
                    memset(d->buffers, 0, sizeof d->buffers);
                    return MCIERR_OUT_OF_MEMORY;
                }
                lx_guest_store_u32(records + i * 32, 32);
                lx_guest_store_u32(records + i * 32 + 4,
                                   lx_execution_mapping_address(d->buffers[i]));
                lx_guest_store_u32(records + i * 32 + 8, size);
            }
            if (!lx_execution_write(GLoaderState.execution, list, records, count * 32))
            {
                for (ULONG i = 0; i < count; i++)
                    lx_execution_unmap(GLoaderState.execution, d->buffers[i]);
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
                lx_execution_unmap(GLoaderState.execution, d->buffers[i]);
            memset(d->buffers, 0, sizeof d->buffers);
            d->buffer_count = 0;
            return 0;
        }
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
    if (message == MCI_SET && (flags & MCI_SET_VOLUME))
    {
        ULONG level = lx_guest_load_u32(p + 16);
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
        d->notify = flags & 1;
        d->notify_window = lx_guest_load_u32(p);
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
                lx_execution_unmap(GLoaderState.execution, d->buffers[i]);
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
    (void)window;
    (void)user;
    TRACE_NATIVE("mciSendString(%s)", command ? command : "<null>");
    if (output && length)
        output[0] = 0;
    return MCIERR_DEVICE_NOT_READY;
}
ULONG mciGetErrorString(ULONG error, PSZ output, USHORT length)
{
    if (!output || !length)
        return 0;
    snprintf(output, length, "MCI error %lu%s", (unsigned long)error,
             error == MCIERR_DEVICE_NOT_READY ? ": device unavailable" : "");
    return 1;
}
#include "os2libs/generated/mdm-lx.h"
