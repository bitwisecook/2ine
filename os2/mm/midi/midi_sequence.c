/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard MIDI file (format 0 and 1) parser for the sequencer. */
#include "os2/mm/midi/midi_sequence.h"
#include <stdlib.h>
#include <string.h>

#define MIDI_SYSEX_MESSAGE_MAX (1024u * 1024u)
#define MIDI_SYSEX_TOTAL_MAX (32u * 1024u * 1024u)
#define MIDI_EVENT_MAX 262144u

/* Standard MIDI file layout: chunks of a 4-byte ID and a big-endian 32-bit length;
   the MThd chunk holds format, track count and division as big-endian 16-bit fields. */
enum
{
    SMF_CHUNK_HEADER_SIZE = 8,
    SMF_CHUNK_LENGTH = 4,
    SMF_HEADER_DATA_MIN = 6,
    SMF_HEADER_SIZE = SMF_CHUNK_HEADER_SIZE + SMF_HEADER_DATA_MIN,
    SMF_FORMAT = 8,
    SMF_TRACK_COUNT = 10,
    SMF_DIVISION = 12,
    /* Format 0 holds one track; format 1 holds simultaneous tracks. */
    SMF_FORMAT_SINGLE_TRACK = 0,
    SMF_FORMAT_MAX = 1,
    SMF_TRACK_LIMIT = 256,
    /* A negative (SMPTE) division is not supported. */
    SMF_DIVISION_SMPTE = 0x8000,
    /* Delta times and lengths are variable-length quantities of at most four bytes. */
    SMF_VLQ_MAX_BYTES = 4,
    SMF_VLQ_CONTINUE = 0x80,
    SMF_VLQ_VALUE_MASK = 0x7f,
    SMF_TEMPO_BYTES = 3,
    /* Microseconds per quarter note until the first tempo event: 120 beats per minute. */
    SMF_DEFAULT_TEMPO = 500000,
    MIDI_EVENTS_INITIAL_CAPACITY = 1024,
    /* Masks the type bit that separates program change (0xCn) from channel pressure (0xDn). */
    ONE_DATA_BYTE_TYPE_MASK = 0xe0
};

static const double MICROSECONDS_PER_SECOND = 1000000.0;
/* Playback continues briefly after the last event so that its notes can sound. */
static const double SEQUENCE_TAIL_SECONDS = 0.1;

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
    for (unsigned i = 0; i < SMF_VLQ_MAX_BYTES && *p < end; i++)
    {
        unsigned c = *(*p)++;
        *v = (*v << MIDI_DATA_BITS) | (c & SMF_VLQ_VALUE_MASK);
        if (!(c & SMF_VLQ_CONTINUE))
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
        size_t capacity = parser->capacity ? parser->capacity * 2 : MIDI_EVENTS_INITIAL_CAPACITY;
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
    LxMidiEvent event = {.tick = tick,
                         .status = MIDI_SYSEX,
                         .sysex = parser->pending,
                         .sysex_size = parser->pending_size};
    parser->pending = NULL;
    parser->pending_size = 0;
    return append_event(parser, event);
}

static int sysex_packet(MidiParser *parser, uint32_t tick, unsigned status,
                        const unsigned char *bytes, size_t size)
{
    if (status == MIDI_SYSEX)
    {
        if (parser->pending_size)
            return 0;
        const unsigned char begin = MIDI_SYSEX;
        if (!append_fragment(parser, &begin, 1))
            return 0;
    }
    if (parser->pending_size)
    {
        for (size_t i = 0; i < size; i++)
            if (bytes[i] >= MIDI_STATUS_BIT && !(bytes[i] == MIDI_SYSEX_END && i + 1 == size))
                return 0;
        if (!append_fragment(parser, bytes, size))
            return 0;
        return size && bytes[size - 1] == MIDI_SYSEX_END ? complete_sysex(parser, tick) : 1;
    }
    /* An unpaired F7 is an escape: its status byte is not transmitted.
       Preserve complete framed messages, rejecting unsupported raw MIDI bytes. */
    size_t offset = 0;
    while (offset < size)
    {
        if (bytes[offset] != MIDI_SYSEX)
            return 0;
        size_t end = offset + 1;
        while (end < size && bytes[end] < MIDI_STATUS_BIT)
            end++;
        if (end == size || bytes[end] != MIDI_SYSEX_END)
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
    if (size < SMF_HEADER_SIZE || memcmp(bytes, "MThd", 4) ||
        big32(bytes + SMF_CHUNK_LENGTH) < SMF_HEADER_DATA_MIN ||
        big32(bytes + SMF_CHUNK_LENGTH) > size - SMF_CHUNK_HEADER_SIZE || bytes[SMF_FORMAT] ||
        bytes[SMF_FORMAT + 1] > SMF_FORMAT_MAX)
        return 0;
    unsigned division = (bytes[SMF_DIVISION] << 8) | bytes[SMF_DIVISION + 1];
    if (!division || division & SMF_DIVISION_SMPTE)
        return 0;
    unsigned expected_tracks = (bytes[SMF_TRACK_COUNT] << 8) | bytes[SMF_TRACK_COUNT + 1],
             tracks = 0;
    if (!expected_tracks || expected_tracks > SMF_TRACK_LIMIT ||
        (bytes[SMF_FORMAT + 1] == SMF_FORMAT_SINGLE_TRACK && expected_tracks != 1))
        return 0;
    size_t offset = SMF_CHUNK_HEADER_SIZE + big32(bytes + SMF_CHUNK_LENGTH);
    uint32_t end_tick = 0;
    while (offset <= size && size - offset >= SMF_CHUNK_HEADER_SIZE)
    {
        if (++tracks > expected_tracks)
            return 0;
        uint32_t length = big32(bytes + offset + SMF_CHUNK_LENGTH);
        if (length > size - offset - SMF_CHUNK_HEADER_SIZE || memcmp(bytes + offset, "MTrk", 4))
            return 0;
        const unsigned char *p = bytes + offset + SMF_CHUNK_HEADER_SIZE, *end = p + length;
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
            /* A data byte where a status is due repeats the running status. */
            if (status & MIDI_STATUS_BIT)
            {
                p++;
                running = status < MIDI_SYSEX ? status : 0;
            }
            else
                status = running;
            if (!status)
                return 0;
            LxMidiEvent event = {.tick = tick, .status = status};
            if (status == MIDI_META)
            {
                if (p == end)
                    return 0;
                unsigned meta = *p++;
                uint32_t n;
                if (!vlq(&p, end, &n) || n > (size_t)(end - p))
                    return 0;
                if (meta == MIDI_META_TEMPO)
                {
                    if (n < SMF_TEMPO_BYTES)
                        return 0;
                    event.tempo = (p[0] << 16) | (p[1] << 8) | p[2];
                    if (!event.tempo)
                        return 0;
                }
                p += n;
                if (!event.tempo)
                    continue;
            }
            else if (status == MIDI_SYSEX || status == MIDI_SYSEX_END)
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
                if (status >= MIDI_SYSEX || parser->pending_size)
                    return 0;
                /* Program change and channel pressure carry one data byte. */
                unsigned n = (status & ONE_DATA_BYTE_TYPE_MASK) == MIDI_PROGRAM_CHANGE ? 1 : 2;
                if (n > (size_t)(end - p))
                    return 0;
                event.key = *p++;
                if (n == 2)
                    event.value = *p++;
                if ((event.key | event.value) & MIDI_STATUS_BIT)
                    return 0;
                unsigned type = status & MIDI_STATUS_TYPE_MASK;
                if (type != MIDI_NOTE_OFF && type != MIDI_NOTE_ON && type != MIDI_CONTROL_CHANGE &&
                    type != MIDI_PROGRAM_CHANGE && type != MIDI_PITCH_BEND)
                    continue;
            }
            if (!append_event(parser, event))
                return 0;
        }
        if (parser->pending_size)
            return 0;
        offset += SMF_CHUNK_HEADER_SIZE + length;
    }
    if (tracks != expected_tracks || offset != size)
        return 0;
    if (sequence->count > 1)
        qsort(sequence->events, sequence->count, sizeof *sequence->events, compare_event);
    uint32_t tick = 0, tempo = SMF_DEFAULT_TEMPO;
    double time = 0;
    for (size_t i = 0; i < sequence->count; i++)
    {
        LxMidiEvent *event = &sequence->events[i];
        time += (event->tick - tick) * (double)tempo / (division * MICROSECONDS_PER_SECOND);
        tick = event->tick;
        event->time = time;
        if (event->tempo)
            tempo = event->tempo;
    }
    sequence->duration = time +
                         (end_tick - tick) * (double)tempo / (division * MICROSECONDS_PER_SECOND) +
                         SEQUENCE_TAIL_SECONDS;
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
