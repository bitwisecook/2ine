/* Public domain. No rights reserved. Wholly AI-authored. */
/* Optional MT-32 build, missing/invalid ROM failure and explicit real-ROM smoke. */
#define _POSIX_C_SOURCE 200809L
#include "os2libs/midi_mt32.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static int smoke(void)
{
    LxMidiMt32 *synth = lx_midi_mt32_create(48000);
    CHECK(synth);
    CHECK(lx_midi_mt32_event(synth, 0x91, 60, 100)); /* Native Part 1 on channel 2. */
    float samples[48000] = {0};
    lx_midi_mt32_render(synth, samples, 48000, 1);
    double energy = 0;
    for (unsigned i = 0; i < 48000; i++)
    {
        CHECK(isfinite(samples[i]));
        energy += samples[i] * samples[i];
    }
    CHECK(energy > 0.000001);
    CHECK(lx_midi_mt32_reset(synth));
    memset(samples, 0, sizeof samples);
    lx_midi_mt32_render(synth, samples, 48000, 0);
    for (unsigned i = 0; i < 48000; i++)
        CHECK(samples[i] == 0);
    CHECK(!lx_midi_mt32_event(synth, 0x7f, 60, 100));
    CHECK(!lx_midi_mt32_sysex(synth, (const unsigned char *)"bad", 3));
    lx_midi_mt32_destroy(synth);
    puts("MT-32 real-ROM audio smoke passed");
    return 0;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--rom-smoke"))
        return smoke();
    CHECK(argc == 1);
    CHECK(!lx_midi_mt32_reset(NULL));
    CHECK(!lx_midi_mt32_event(NULL, 0x91, 60, 100));
    CHECK(!lx_midi_mt32_sysex(NULL, NULL, 0));
    lx_midi_mt32_destroy(NULL);
    float output = 0.5f;
    lx_midi_mt32_render(NULL, &output, 1, 1);
    CHECK(output == 0.5f);
    CHECK(!setenv("TWOINE_AUDIO_CONFIG", "/missing/twoine-audio-config", 1));
    CHECK(!unsetenv("TWOINE_MT32_CONTROL_ROM"));
    CHECK(!unsetenv("TWOINE_MT32_PCM_ROM"));
    CHECK(!lx_midi_mt32_create(48000));
    CHECK(lx_midi_mt32_available() == TWOINE_MT32_SYNTH);
    char name[] = "/tmp/twoine-mt32-invalid-XXXXXX";
    int descriptor = mkstemp(name);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "invalid ROM", 11) == 11 && !close(descriptor));
    CHECK(!setenv("TWOINE_MT32_CONTROL_ROM", name, 1));
    CHECK(!setenv("TWOINE_MT32_PCM_ROM", name, 1));
    CHECK(!lx_midi_mt32_create(48000));
    CHECK(!unlink(name));
    puts(
        "MT-32 build and missing/invalid ROM failure checks passed; real audio requires supplied ROMs");
    return 0;
}
