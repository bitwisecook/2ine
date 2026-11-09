/* Public domain. No rights reserved. Wholly AI-authored. */
/* Sine-wave fallback for builds without a MIDI synthesizer: one tone per note. */
#ifndef TWOINE_MIDI_OSCILLATOR_H
#define TWOINE_MIDI_OSCILLATOR_H

enum
{
    MIDI_OSCILLATOR_VOICES = 64
};

typedef struct LxMidiOscillatorVoice
{
    double phase;
    float level;
    unsigned char channel, key;
} LxMidiOscillatorVoice;

/* A zero-filled oscillator is silent and ready. The caller serializes use. */
typedef struct LxMidiOscillator
{
    LxMidiOscillatorVoice voices[MIDI_OSCILLATOR_VOICES];
} LxMidiOscillator;

/* Note on/off and all-sound/all-notes-off; other messages are ignored. When
   every voice sounds, a new note replaces the last voice. */
void lx_midi_oscillator_event(LxMidiOscillator *oscillator, unsigned status, unsigned key,
                              unsigned value);
/* Adds count interleaved stereo frames (the same signal on both channels). */
void lx_midi_oscillator_render_stereo(LxMidiOscillator *oscillator, float *samples, unsigned count,
                                      unsigned rate, float volume);
#endif
