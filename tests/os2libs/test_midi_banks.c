/* Public domain. No rights reserved. Wholly AI-authored. */
/* Real bank decoders and channel routing must produce finite PCM or exact mute. */
#define _POSIX_C_SOURCE 200809L
#include "os2libs/midi_synth.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 5 || setenv("TWOINE_SOUNDFONT", argv[1], 1) ||
        setenv("TWOINE_AUDIO_CONFIG", argv[2], 1))
        return 2;
    LxMidiSynth *synth = lx_midi_synth_create();
    if (!synth)
        return 3;
    unsigned channel = (unsigned)atoi(argv[3]) - 1;
    lx_midi_synth_event(synth, 0x90 | channel, 60, 110);
    float samples[8192] = {0};
    double energy = 0;
    for (int pass = 0; pass < 4; pass++)
    {
        memset(samples, 0, sizeof samples);
        lx_midi_synth_render_stereo(synth, samples, 4096, 48000, 1);
        for (unsigned i = 0; i < sizeof samples / sizeof samples[0]; i++)
        {
            if (!isfinite(samples[i]))
                return 4;
            energy += samples[i] * samples[i];
        }
    }
    lx_midi_synth_destroy(synth);
    int muted = !strcmp(argv[4], "muted");
    printf("%s channel %u: energy %.9g\n", argv[1], channel + 1, energy);
    return muted ? energy != 0 : energy < 0.000001;
}
