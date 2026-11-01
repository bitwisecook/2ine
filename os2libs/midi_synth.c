/* Public domain. No rights reserved. Wholly AI-authored. */
/* SoundFont and native MT-32 synthesis share ordered MIDI events and host PCM. */
#include "os2libs/midi_synth.h"
#include "os2libs/midi_mt32.h"
#include "os2libs/midi_config.h"
#include "hal/host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if TWOINE_MIDI_SYNTH
#if defined(__GNUC__)
#pragma GCC visibility push(hidden)
#endif
#define TSF_IMPLEMENTATION
#include <tsf.h>
#if defined(__GNUC__)
#pragma GCC visibility pop
#endif
#endif
struct LxMidiSynth
{
#if TWOINE_MIDI_SYNTH
    tsf *font;
#endif
    LxMidiMt32 *mt32;
    unsigned rate;
};
#if TWOINE_MIDI_SYNTH
static tsf *open_font(const char *path)
{
    const char *trace = getenv("TWOINE_TRACE_AUDIO");
    if (trace && *trace && strcmp(trace, "0"))
        fprintf(stderr, "2ine: MIDI GM bank %s\n", path);
    return tsf_load_filename(path);
}
static tsf *load_font(void)
{
    const char *override = getenv("TWOINE_SOUNDFONT");
    if (override && *override)
        return open_font(override);
    char *selected = lx_midi_config_value("soundfont");
    if (selected)
    {
        tsf *font = open_font(selected);
        free(selected);
        return font;
    }
    char *executable = lx_host_executable_path();
    if (executable)
    {
        char *slash = strrchr(executable, '/'), *backslash = strrchr(executable, '\\');
        if (backslash && (!slash || backslash > slash))
            slash = backslash;
        if (slash)
        {
            slash[1] = 0;
            size_t length = strlen(executable) + sizeof "soundfonts/GeneralUser-GS.sf2";
            char *path = malloc(length);
            if (path)
            {
                snprintf(path, length, "%ssoundfonts/GeneralUser-GS.sf2", executable);
                tsf *font = open_font(path);
                free(path);
                if (font)
                {
                    free(executable);
                    return font;
                }
            }
        }
        free(executable);
    }
    return open_font(TWOINE_SOUNDFONT_FILE);
}
#endif
int lx_midi_synth_reset(LxMidiSynth *synth)
{
    if (!synth)
        return 0;
    if (synth->mt32)
        return lx_midi_mt32_reset(synth->mt32);
#if TWOINE_MIDI_SYNTH
    tsf_reset(synth->font);
    float discarded[4096];
    tsf_render_float(synth->font, discarded, 2048, 0);
    for (unsigned channel = 0; channel < 16; channel++)
        if (!tsf_channel_set_presetnumber(synth->font, channel, 0, channel == 9))
            return 0;
    return 1;
#else
    return 0;
#endif
}
LxMidiSynth *lx_midi_synth_create(void)
{
    const char *mode = getenv("TWOINE_MIDI_BACKEND");
    char *configured = !mode || !*mode ? lx_midi_config_value("midi_backend") : NULL;
    if (!mode || !*mode)
        mode = configured ? configured : "gm";
    int mt32 = !strcmp(mode, "mt32");
    if (!mt32 && strcmp(mode, "gm"))
    {
        fprintf(stderr, "2ine: Unknown MIDI backend '%s'; select gm or mt32.\n", mode);
        free(configured);
        return NULL;
    }
    free(configured);
    LxMidiSynth *synth = calloc(1, sizeof *synth);
    if (!synth)
        return NULL;
    synth->rate = 48000;
    if (mt32)
    {
        synth->mt32 = lx_midi_mt32_create(synth->rate);
        if (!synth->mt32)
        {
            free(synth);
            return NULL;
        }
        return synth;
    }
#if TWOINE_MIDI_SYNTH
    synth->font = load_font();
    if (!synth->font)
    {
        fprintf(stderr, "2ine: General MIDI bank unavailable (TWOINE_SOUNDFONT/audio.conf).\n");
        free(synth);
        return NULL;
    }
    for (unsigned program = 0; program < 128; program++)
        if (tsf_get_presetindex(synth->font, 0, program) < 0)
        {
            fprintf(stderr, "2ine: SoundFont lacks General MIDI program %u.\n", program);
            lx_midi_synth_destroy(synth);
            return NULL;
        }
    if (tsf_get_presetindex(synth->font, 128, 0) < 0 || !tsf_set_max_voices(synth->font, 256))
    {
        fprintf(stderr, "2ine: SoundFont percussion or synthesis resources unavailable.\n");
        lx_midi_synth_destroy(synth);
        return NULL;
    }
    tsf_set_output(synth->font, TSF_STEREO_INTERLEAVED, synth->rate, -6.0f);
    if (!lx_midi_synth_reset(synth))
    {
        lx_midi_synth_destroy(synth);
        return NULL;
    }
    return synth;
#else
    free(synth);
    return NULL;
#endif
}
void lx_midi_synth_destroy(LxMidiSynth *synth)
{
    if (!synth)
        return;
    lx_midi_mt32_destroy(synth->mt32);
#if TWOINE_MIDI_SYNTH
    if (synth->font)
        tsf_close(synth->font);
#endif
    free(synth);
}
void lx_midi_synth_event(LxMidiSynth *synth, unsigned status, unsigned key, unsigned value)
{
    if (!synth || status < 0x80 || status >= 0xf0 || key > 127 || value > 127)
        return;
    if (synth->mt32)
    {
        lx_midi_mt32_event(synth->mt32, status, key, value);
        return;
    }
#if TWOINE_MIDI_SYNTH
    unsigned channel = status & 15;
    switch (status & 0xf0)
    {
        case 0x80:
            tsf_channel_note_off(synth->font, channel, key);
            break;
        case 0x90:
            tsf_channel_note_on(synth->font, channel, key, value / 127.0f);
            break;
        case 0xb0:
            tsf_channel_midi_control(synth->font, channel, key, value);
            break;
        case 0xc0:
            tsf_channel_set_presetnumber(synth->font, channel, key, channel == 9);
            break;
        case 0xe0:
            tsf_channel_set_pitchwheel(synth->font, channel, key | value << 7);
            break;
    }
#endif
}
void lx_midi_synth_sysex(LxMidiSynth *synth, const unsigned char *message, size_t length)
{
    if (!synth || !message || length < 2 || message[0] != 0xf0 || message[length - 1] != 0xf7)
        return;
    if (synth->mt32)
    {
        lx_midi_mt32_sysex(synth->mt32, message, length);
        return;
    }
    if (length == 6 && message[1] == 0x7e && message[3] == 9 && message[4] >= 1 && message[4] <= 3)
        lx_midi_synth_reset(synth);
}
static void render(LxMidiSynth *synth, float *samples, unsigned count, unsigned rate, float volume,
                   int channels)
{
    if (!synth || !samples || !count || rate < 4000 || rate > 192000 || !isfinite(volume))
        return;
    if (synth->mt32)
    {
        if (rate == synth->rate)
        {
            if (channels == 2)
                lx_midi_mt32_render_stereo(synth->mt32, samples, count, volume);
            else
                lx_midi_mt32_render(synth->mt32, samples, count, volume);
        }
        return;
    }
#if TWOINE_MIDI_SYNTH
    if (rate != synth->rate)
    {
        synth->rate = rate;
        tsf_set_output(synth->font, TSF_STEREO_INTERLEAVED, rate, -6.0f);
    }
    float rendered[512];
    for (unsigned offset = 0; offset < count;)
    {
        unsigned block = count - offset;
        if (block > 256)
            block = 256;
        tsf_render_float(synth->font, rendered, (int)block, 0);
        for (unsigned i = 0; i < block; i++)
            if (channels == 1)
                samples[offset + i] += (rendered[2 * i] + rendered[2 * i + 1]) * (0.5f * volume);
            else
            {
                samples[2 * (offset + i)] += rendered[2 * i] * volume;
                samples[2 * (offset + i) + 1] += rendered[2 * i + 1] * volume;
            }
        offset += block;
    }
#else
    (void)channels;
    (void)volume;
#endif
}

void lx_midi_synth_render(LxMidiSynth *synth, float *samples, unsigned count, unsigned rate,
                          float volume)
{
    render(synth, samples, count, rate, volume, 1);
}
void lx_midi_synth_render_stereo(LxMidiSynth *synth, float *samples, unsigned count, unsigned rate,
                                 float volume)
{
    render(synth, samples, count, rate, volume, 2);
}
