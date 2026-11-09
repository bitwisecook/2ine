/* Public domain. No rights reserved. Wholly AI-authored. */
/* Immutable scheduled SMF events; the sequence owns every SysEx payload. */
#ifndef TWOINE_MIDI_SEQUENCE_H
#define TWOINE_MIDI_SEQUENCE_H
#include <stddef.h>
#include <stdint.h>

/* MIDI status bytes, meta events and channel-mode controllers. */
enum
{
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_CONTROL_CHANGE = 0xb0,
    MIDI_PROGRAM_CHANGE = 0xc0,
    MIDI_PITCH_BEND = 0xe0,
    MIDI_SYSEX = 0xf0,
    MIDI_SYSEX_END = 0xf7,
    MIDI_META = 0xff,
    MIDI_META_TEMPO = 0x51,
    MIDI_ALL_SOUND_OFF = 120,
    MIDI_ALL_NOTES_OFF = 123
};

typedef struct LxMidiEvent
{
    uint32_t tick, order;
    double time;
    uint32_t tempo;
    unsigned char status, key, value;
    unsigned char *sysex;
    size_t sysex_size;
} LxMidiEvent;

typedef struct LxMidiSequence
{
    LxMidiEvent *events;
    size_t count;
    double duration;
} LxMidiSequence;

/* F0/F7 packets are assembled into framed messages and scheduled at completion.
   Standalone escapes may contain complete framed SysEx messages. Other raw
   escapes reject. A message is at most 1 MiB; aggregate payload is at most 32 MiB.
   Parsing failure leaves an empty sequence. Clear a prior sequence before reuse. */
int lx_midi_sequence_parse(LxMidiSequence *sequence, const unsigned char *bytes, size_t size);
void lx_midi_sequence_clear(LxMidiSequence *sequence);
#endif
