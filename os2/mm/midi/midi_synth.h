/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_MIDI_SYNTH_H
#define TWOINE_MIDI_SYNTH_H
#include <stddef.h>
typedef struct LxMidiSynth LxMidiSynth;
/* The caller serializes each instance. GM needs a complete bank; MT-32 needs ROMs. */
LxMidiSynth *lx_midi_synth_create(void);
void lx_midi_synth_destroy(LxMidiSynth *synth);
int lx_midi_synth_reset(LxMidiSynth *synth);
void lx_midi_synth_event(LxMidiSynth *synth, unsigned status, unsigned key, unsigned value);
/* SysEx is a complete framed F0...F7 message. */
void lx_midi_synth_sysex(LxMidiSynth *synth, const unsigned char *message, size_t length);
void lx_midi_synth_render(LxMidiSynth *synth, float *samples, unsigned count, unsigned rate,
                          float volume);
/* Add count interleaved stereo frames, preserving SoundFont/Munt channel separation. */
void lx_midi_synth_render_stereo(LxMidiSynth *synth, float *samples, unsigned count, unsigned rate,
                                 float volume);
#endif
