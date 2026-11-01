/* Public domain. No rights reserved. Wholly AI-authored. */
/* Immutable scheduled SMF events; the sequence owns every SysEx payload. */
#ifndef TWOINE_MIDI_SEQUENCE_H
#define TWOINE_MIDI_SEQUENCE_H
#include <stddef.h>
#include <stdint.h>
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
