/* Public domain. No rights reserved. Wholly AI-authored. */
/* Sine-wave fallback for builds without a MIDI synthesizer. */
#include "os2/mm/midi/midi_oscillator.h"
#include "os2/mm/midi/midi_sequence.h"
#include <math.h>

enum
{
    /* MIDI key 69 is A4 (440 Hz); twelve keys per octave. */
    MIDI_KEY_A4 = 69,
    KEYS_PER_OCTAVE = 12
};

static const double A4_HZ = 440.0;

static const double tau = 6.283185307179586;
/* Per-voice gain, low enough that a chord does not clip. */
static const float voice_gain = 0.08f;

void lx_midi_oscillator_event(LxMidiOscillator *oscillator, unsigned status, unsigned key,
                              unsigned value)
{
    LxMidiOscillatorVoice *voices = oscillator->voices;
    unsigned type = status & MIDI_STATUS_TYPE_MASK, channel = status & MIDI_CHANNEL_MASK;
    if (type == MIDI_CONTROL_CHANGE && (key == MIDI_ALL_SOUND_OFF || key == MIDI_ALL_NOTES_OFF))
    {
        for (unsigned i = 0; i < MIDI_OSCILLATOR_VOICES; i++)
            if (voices[i].channel == channel)
                voices[i].level = 0;
    }
    else if (type == MIDI_NOTE_OFF || (type == MIDI_NOTE_ON && !value))
    {
        for (unsigned i = 0; i < MIDI_OSCILLATOR_VOICES; i++)
            if (voices[i].channel == channel && voices[i].key == key)
                voices[i].level = 0;
    }
    else if (type == MIDI_NOTE_ON)
    {
        unsigned i;
        for (i = 0; i < MIDI_OSCILLATOR_VOICES - 1 && voices[i].level; i++)
        {
        }
        voices[i] = (LxMidiOscillatorVoice){.channel = (unsigned char)channel,
                                            .key = (unsigned char)key,
                                            .level = value / (float)MIDI_DATA_MAX};
    }
}

void lx_midi_oscillator_render_stereo(LxMidiOscillator *oscillator, float *samples, unsigned count,
                                      unsigned rate, float volume)
{
    for (unsigned frame = 0; frame < count; frame++)
    {
        float value = 0;
        for (unsigned v = 0; v < MIDI_OSCILLATOR_VOICES; v++)
            if (oscillator->voices[v].level)
            {
                LxMidiOscillatorVoice *voice = &oscillator->voices[v];
                double hz =
                    A4_HZ * pow(2.0, ((int)voice->key - MIDI_KEY_A4) / (double)KEYS_PER_OCTAVE);
                value += (float)sin(voice->phase) * voice->level * voice_gain;
                voice->phase += tau * hz / rate;
                if (voice->phase > tau)
                    voice->phase -= tau;
            }
        samples[2 * frame] += value * volume;
        samples[2 * frame + 1] += value * volume;
    }
}
