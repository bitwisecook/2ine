/* Public domain. No rights reserved. Wholly AI-authored. */
/* Sampled General MIDI program coverage, percussion and channel controls. */
#include "os2libs/midi_synth.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define SAMPLE_COUNT 12000
static float first[SAMPLE_COUNT], second[SAMPLE_COUNT];
static double render(LxMidiSynth *synth, float *samples, float volume)
{
    memset(samples, 0, SAMPLE_COUNT * sizeof *samples);
    lx_midi_synth_render(synth, samples, SAMPLE_COUNT, 48000, volume);
    double energy = 0;
    for (unsigned i = 0; i < SAMPLE_COUNT; i++)
    {
        CHECK(isfinite(samples[i]));
        energy += samples[i] * samples[i];
    }
    return energy;
}
static double note(LxMidiSynth *synth, unsigned channel, unsigned program, unsigned key,
                   float *samples)
{
    CHECK(lx_midi_synth_reset(synth));
    lx_midi_synth_event(synth, 0xc0 | channel, program, 0);
    lx_midi_synth_event(synth, 0x90 | channel, key, 100);
    return render(synth, samples, 1);
}
static void stereo_contracts(LxMidiSynth *synth)
{
    float stereo[SAMPLE_COUNT * 2];
    double energy[2][2] = {{0}};
    for (unsigned side = 0; side < 2; side++)
    {
        CHECK(lx_midi_synth_reset(synth));
        lx_midi_synth_event(synth, 0xc0, 80, 0);
        lx_midi_synth_event(synth, 0xb0, 10, side ? 127 : 0);
        lx_midi_synth_event(synth, 0x90, 60, 100);
        memset(stereo, 0, sizeof stereo);
        lx_midi_synth_render_stereo(synth, stereo, SAMPLE_COUNT, 48000, 1);
        for (unsigned i = 0; i < SAMPLE_COUNT; i++)
            for (unsigned channel = 0; channel < 2; channel++)
            {
                float value = stereo[i * 2 + channel];
                CHECK(isfinite(value));
                energy[side][channel] += value * value;
            }
    }
    CHECK(energy[0][0] > 0.000001 && energy[1][1] > 0.000001);
    CHECK(energy[0][0] > energy[0][1] * 100);
    CHECK(energy[1][1] > energy[1][0] * 100);
    CHECK(lx_midi_synth_reset(synth));
    lx_midi_synth_event(synth, 0x90, 60, 100);
    memset(stereo, 0, sizeof stereo);
    lx_midi_synth_render_stereo(synth, stereo, SAMPLE_COUNT, 48000, 0);
    for (unsigned i = 0; i < SAMPLE_COUNT * 2; i++)
        CHECK(stereo[i] == 0);
    puts("Sampled MIDI stereo panning and stereo volume passed");
}
int main(void)
{
#if !TWOINE_MIDI_SYNTH
    puts("sampled MIDI profile disabled");
    return 77;
#else
    LxMidiSynth *synth = lx_midi_synth_create();
    CHECK(synth);
    stereo_contracts(synth);
    for (unsigned program = 0; program < 128; program++)
    {
        double energy = note(synth, 0, program, 60, second);
        if (energy <= 0.000001)
            fprintf(stderr, "silent program %u\n", program);
        CHECK(energy > 0.000001);
    }
    CHECK(note(synth, 0, 0, 60, first) > 0.000001);
    CHECK(note(synth, 0, 24, 60, second) > 0.000001);
    CHECK(memcmp(first, second, sizeof first));
    CHECK(note(synth, 9, 0, 35, first) > 0.000001);
    CHECK(note(synth, 9, 0, 38, second) > 0.000001);
    CHECK(memcmp(first, second, sizeof first));
    CHECK(lx_midi_synth_reset(synth));
    lx_midi_synth_event(synth, 0xb0, 7, 0);
    lx_midi_synth_event(synth, 0x90, 60, 100);
    CHECK(render(synth, first, 1) == 0);
    CHECK(lx_midi_synth_reset(synth));
    lx_midi_synth_event(synth, 0xb0, 11, 0);
    lx_midi_synth_event(synth, 0x90, 60, 100);
    CHECK(render(synth, first, 1) == 0);
    CHECK(note(synth, 0, 80, 69, first) > 0.000001);
    CHECK(lx_midi_synth_reset(synth));
    lx_midi_synth_event(synth, 0xc0, 80, 0);
    lx_midi_synth_event(synth, 0xe0, 127, 127);
    lx_midi_synth_event(synth, 0x90, 69, 100);
    CHECK(render(synth, second, 1) > 0.000001);
    CHECK(memcmp(first, second, sizeof first));
    lx_midi_synth_event(synth, 0xb0, 120, 0);
    render(synth, second, 1); /* retire the sample envelope's short release */
    CHECK(render(synth, second, 1) == 0);
    CHECK(lx_midi_synth_reset(synth));
    lx_midi_synth_event(synth, 0x90, 60, 100);
    CHECK(render(synth, first, 0) == 0);
    CHECK(lx_midi_synth_reset(synth));
    lx_midi_synth_event(synth, 0x90, 60, 100);
    render(synth, first, 1);
    lx_midi_synth_event(synth, 0xb0, 64, 127);
    lx_midi_synth_event(synth, 0x80, 60, 0);
    CHECK(render(synth, first, 1) > 0.000001);
    lx_midi_synth_event(synth, 0xb0, 64, 0);
    for (unsigned i = 0; i < 20; i++)
        render(synth, first, 1);
    CHECK(render(synth, first, 1) == 0);
    lx_midi_synth_destroy(synth);
    puts("128 sampled GM programs, percussion, volume, expression, sustain and pitch bend passed");
    return 0;
#endif
}
