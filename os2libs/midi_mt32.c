/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native Roland MT-32 emulation through Munt's stable C interface. */
#include "os2libs/midi_mt32.h"
#include "os2libs/midi_config.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if TWOINE_MT32_SYNTH
#include <mt32emu/mt32emu.h>
struct LxMidiMt32
{
    mt32emu_context context;
};
int lx_midi_mt32_available(void)
{
    return 1;
}
void lx_midi_mt32_destroy(LxMidiMt32 *synth)
{
    if (synth)
    {
        if (synth->context)
            mt32emu_free_context(synth->context);
        free(synth);
    }
}
int lx_midi_mt32_reset(LxMidiMt32 *synth)
{
    if (!synth)
        return 0;
    mt32emu_close_synth(synth->context);
    return mt32emu_open_synth(synth->context) == MT32EMU_RC_OK;
}
static char *rom_path(const char *environment, const char *key)
{
    const char *selected = getenv(environment);
    if (!selected || !*selected)
        return lx_midi_config_value(key);
    size_t size = strlen(selected) + 1;
    char *path = malloc(size);
    if (path)
        memcpy(path, selected, size);
    return path;
}
LxMidiMt32 *lx_midi_mt32_create(unsigned rate)
{
    char *control = rom_path("TWOINE_MT32_CONTROL_ROM", "mt32_control_rom");
    char *pcm = rom_path("TWOINE_MT32_PCM_ROM", "mt32_pcm_rom");
    if (!control || !*control || !pcm || !*pcm)
    {
        fprintf(stderr, "2ine: MT-32 requires TWOINE_MT32_CONTROL_ROM and TWOINE_MT32_PCM_ROM.\n");
        free(control);
        free(pcm);
        return NULL;
    }
    if (rate < 4000 || rate > 192000)
    {
        free(control);
        free(pcm);
        return NULL;
    }
    LxMidiMt32 *synth = calloc(1, sizeof *synth);
    if (!synth)
    {
        free(control);
        free(pcm);
        return NULL;
    }
    mt32emu_report_handler_i handler = {0};
    synth->context = mt32emu_create_context(handler, NULL);
    if (!synth->context)
    {
        free(control);
        free(pcm);
        lx_midi_mt32_destroy(synth);
        return NULL;
    }
    mt32emu_return_code control_result = mt32emu_add_rom_file(synth->context, control);
    mt32emu_return_code pcm_result = mt32emu_add_rom_file(synth->context, pcm);
    free(control);
    free(pcm);
    if (control_result != MT32EMU_RC_ADDED_CONTROL_ROM || pcm_result != MT32EMU_RC_ADDED_PCM_ROM)
    {
        fprintf(stderr, "2ine: MT-32 ROM validation failed (control=%d, PCM=%d).\n", control_result,
                pcm_result);
        lx_midi_mt32_destroy(synth);
        return NULL;
    }
    mt32emu_set_stereo_output_samplerate(synth->context, rate);
    mt32emu_return_code opened = mt32emu_open_synth(synth->context);
    if (opened != MT32EMU_RC_OK)
    {
        fprintf(stderr, "2ine: MT-32 ROM pair could not initialize the synthesizer (%d).\n",
                opened);
        lx_midi_mt32_destroy(synth);
        return NULL;
    }
    return synth;
}
int lx_midi_mt32_event(LxMidiMt32 *synth, unsigned status, unsigned key, unsigned value)
{
    if (!synth || status < 0x80 || status >= 0xf0 || key > 127 || value > 127)
        return 0;
    mt32emu_play_msg_now(synth->context, status | key << 8 | value << 16);
    return 1;
}
int lx_midi_mt32_sysex(LxMidiMt32 *synth, const unsigned char *message, size_t length)
{
    if (!synth || !message || length < 2 || length > UINT32_MAX || message[0] != 0xf0 ||
        message[length - 1] != 0xf7)
        return 0;
    mt32emu_play_sysex_now(synth->context, message, (mt32emu_bit32u)length);
    return 1;
}
static void render(LxMidiMt32 *synth, float *samples, unsigned count, float volume, int channels)
{
    if (!synth || !samples || !isfinite(volume))
        return;
    float stereo[512];
    for (unsigned offset = 0; offset < count;)
    {
        unsigned frames = count - offset;
        if (frames > 256)
            frames = 256;
        mt32emu_render_float(synth->context, stereo, frames);
        for (unsigned i = 0; i < frames; i++)
            if (channels == 1)
                samples[offset + i] += (stereo[2 * i] + stereo[2 * i + 1]) * (0.5f * volume);
            else
            {
                samples[2 * (offset + i)] += stereo[2 * i] * volume;
                samples[2 * (offset + i) + 1] += stereo[2 * i + 1] * volume;
            }
        offset += frames;
    }
}
#else
int lx_midi_mt32_available(void)
{
    return 0;
}
LxMidiMt32 *lx_midi_mt32_create(unsigned rate)
{
    (void)rate;
    fprintf(stderr, "2ine: MT-32 synthesis was not enabled in this build (TWOINE_MT32_SYNTH).\n");
    return NULL;
}
void lx_midi_mt32_destroy(LxMidiMt32 *synth)
{
    (void)synth;
}
int lx_midi_mt32_reset(LxMidiMt32 *synth)
{
    (void)synth;
    return 0;
}
int lx_midi_mt32_event(LxMidiMt32 *synth, unsigned status, unsigned key, unsigned value)
{
    (void)synth;
    (void)status;
    (void)key;
    (void)value;
    return 0;
}
int lx_midi_mt32_sysex(LxMidiMt32 *synth, const unsigned char *message, size_t length)
{
    (void)synth;
    (void)message;
    (void)length;
    return 0;
}
static void render(LxMidiMt32 *synth, float *samples, unsigned count, float volume, int channels)
{
    (void)channels;
    (void)synth;
    (void)samples;
    (void)count;
    (void)volume;
}
#endif

void lx_midi_mt32_render(LxMidiMt32 *synth, float *samples, unsigned count, float volume)
{
    render(synth, samples, count, volume, 1);
}
void lx_midi_mt32_render_stereo(LxMidiMt32 *synth, float *samples, unsigned count, float volume)
{
    render(synth, samples, count, volume, 2);
}
