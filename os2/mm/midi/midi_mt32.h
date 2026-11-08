/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_MIDI_MT32_H
#define TWOINE_MIDI_MT32_H
#include <stddef.h>
typedef struct LxMidiMt32 LxMidiMt32;
/* Instances preserve native MT-32 MIDI/SysEx semantics. The caller serializes access. */
/* ROM paths: TWOINE_MT32_CONTROL_ROM and TWOINE_MT32_PCM_ROM; no ROM is bundled. */
int lx_midi_mt32_available(void);
LxMidiMt32 *lx_midi_mt32_create(unsigned rate);
void lx_midi_mt32_destroy(LxMidiMt32 *synth);
int lx_midi_mt32_reset(LxMidiMt32 *synth);
int lx_midi_mt32_event(LxMidiMt32 *synth, unsigned status, unsigned key, unsigned value);
int lx_midi_mt32_sysex(LxMidiMt32 *synth, const unsigned char *message, size_t length);
/* Add a stereo-to-mono mix at the creation sample rate, without allocating. */
void lx_midi_mt32_render(LxMidiMt32 *synth, float *samples, unsigned count, float volume);
/* Add count interleaved stereo frames at the creation sample rate. */
void lx_midi_mt32_render_stereo(LxMidiMt32 *synth, float *samples, unsigned count, float volume);
#endif
