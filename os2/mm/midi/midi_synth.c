/* Public domain. No rights reserved. Wholly AI-authored. */
/* SoundFont and native MT-32 synthesis share ordered MIDI events and host PCM. */
#include "os2/mm/midi/midi_synth.h"
#include "os2/mm/midi/midi_mt32.h"
#include "os2/mm/midi/midi_config.h"
#include "os2/mm/midi/midi_messages.h"
#if TWOINE_MIDI_EXTENDED
#include "os2/mm/midi/midi_banks.h"
#endif
#include "host/process.h"
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
enum
{
    /* Rate the synthesizers are created at; a render at another rate retunes them. */
    SYNTH_INITIAL_RATE = 48000,
    SYNTH_RATE_MIN = 4000,
    SYNTH_RATE_MAX = 192000,
    /* Stereo frames rendered per TinySoundFont call. */
    RENDER_BLOCK_FRAMES = 256,
    /* Frames rendered and discarded after a reset so that released voices die away. */
    RESET_FLUSH_FRAMES = 2048,
    /* SoundFont bank 128 holds the percussion kits. */
    SOUNDFONT_PERCUSSION_BANK = 128,
    SOUNDFONT_MAX_VOICES = 256,
    /* F0 7E <device> 09 <01-03> F7: GM System On, GM System Off or GM2 System On. */
    GM_SYSTEM_MESSAGE_SIZE = 6,
    GM_SYSTEM_ON = 1,
    GM2_SYSTEM_ON = 3
};

/* TinySoundFont output gain, leaving headroom for dense passages. */
static const float SOUNDFONT_GAIN_DB = -6.0f;

struct LxMidiSynth
{
#if TWOINE_MIDI_SYNTH
    tsf *font;
#endif
    LxMidiMt32 *mt32;
#if TWOINE_MIDI_EXTENDED
    LxMidiBanks *banks;
#endif
    unsigned rate;
};

#if TWOINE_MIDI_SYNTH && !TWOINE_MIDI_EXTENDED
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
            size_t length = strlen(executable) + sizeof "soundfonts/FluidR3_GM.sf2";
            char *path = malloc(length);
            if (path)
            {
                snprintf(path, length, "%ssoundfonts/FluidR3_GM.sf2", executable);
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
#if TWOINE_MIDI_EXTENDED
    if (synth->banks)
        return lx_midi_banks_reset(synth->banks);
#endif
#if TWOINE_MIDI_SYNTH
    tsf_reset(synth->font);
    float discarded[RESET_FLUSH_FRAMES * 2];
    tsf_render_float(synth->font, discarded, RESET_FLUSH_FRAMES, 0);
    for (unsigned channel = 0; channel < MIDI_CHANNELS; channel++)
        if (!tsf_channel_set_presetnumber(synth->font, channel, 0,
                                          channel == MIDI_PERCUSSION_CHANNEL))
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
    synth->rate = SYNTH_INITIAL_RATE;
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
#if TWOINE_MIDI_EXTENDED
    char *executable = lx_host_executable_path(), *bank_path = NULL;
    if (executable)
    {
        char *slash = strrchr(executable, '/'), *backslash = strrchr(executable, '\\');
        if (backslash && (!slash || backslash > slash))
            slash = backslash;
        if (slash)
        {
            slash[1] = 0;
            size_t size = strlen(executable) + sizeof "soundfonts/FluidR3_GM.sf2";
            bank_path = malloc(size);
            if (bank_path)
            {
                snprintf(bank_path, size, "%ssoundfonts/FluidR3_GM.sf2", executable);
                FILE *file = fopen(bank_path, "rb");
                if (file)
                    fclose(file);
                else
                {
                    free(bank_path);
                    bank_path = NULL;
                }
            }
        }
        free(executable);
    }
    synth->banks = lx_midi_banks_create(bank_path ? bank_path : TWOINE_SOUNDFONT_FILE, synth->rate);
    free(bank_path);
    if (!synth->banks)
    {
        fprintf(stderr, "2ine: Cannot load the selected sound bank or channel mapping.\n");
        free(synth);
        return NULL;
    }
    return synth;
#else
    synth->font = load_font();
    if (!synth->font)
    {
        fprintf(stderr, "2ine: General MIDI bank unavailable (TWOINE_SOUNDFONT/audio.conf).\n");
        free(synth);
        return NULL;
    }
    for (unsigned program = 0; program < MIDI_PROGRAMS; program++)
        if (tsf_get_presetindex(synth->font, 0, program) < 0)
        {
            fprintf(stderr, "2ine: SoundFont lacks General MIDI program %u.\n", program);
            lx_midi_synth_destroy(synth);
            return NULL;
        }
    if (tsf_get_presetindex(synth->font, SOUNDFONT_PERCUSSION_BANK, 0) < 0 ||
        !tsf_set_max_voices(synth->font, SOUNDFONT_MAX_VOICES))
    {
        fprintf(stderr, "2ine: SoundFont percussion or synthesis resources unavailable.\n");
        lx_midi_synth_destroy(synth);
        return NULL;
    }
    tsf_set_output(synth->font, TSF_STEREO_INTERLEAVED, synth->rate, SOUNDFONT_GAIN_DB);
    if (!lx_midi_synth_reset(synth))
    {
        lx_midi_synth_destroy(synth);
        return NULL;
    }
    return synth;
#endif
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
#if TWOINE_MIDI_EXTENDED
    lx_midi_banks_destroy(synth->banks);
#endif
#if TWOINE_MIDI_SYNTH
    if (synth->font)
        tsf_close(synth->font);
#endif
    free(synth);
}

void lx_midi_synth_event(LxMidiSynth *synth, unsigned status, unsigned key, unsigned value)
{
    if (!synth || status < MIDI_STATUS_BIT || status >= MIDI_SYSEX || key > MIDI_DATA_MAX ||
        value > MIDI_DATA_MAX)
        return;
    if (synth->mt32)
    {
        lx_midi_mt32_event(synth->mt32, status, key, value);
        return;
    }
#if TWOINE_MIDI_SYNTH
#if TWOINE_MIDI_EXTENDED
    if (synth->banks)
    {
        lx_midi_banks_event(synth->banks, status, key, value);
        return;
    }
#endif
    unsigned channel = status & MIDI_CHANNEL_MASK;
    switch (status & MIDI_STATUS_TYPE_MASK)
    {
        case MIDI_NOTE_OFF:
            tsf_channel_note_off(synth->font, channel, key);
            break;
        case MIDI_NOTE_ON:
            tsf_channel_note_on(synth->font, channel, key, value / (float)MIDI_DATA_MAX);
            break;
        case MIDI_CONTROL_CHANGE:
            tsf_channel_midi_control(synth->font, channel, key, value);
            break;
        case MIDI_PROGRAM_CHANGE:
            tsf_channel_set_presetnumber(synth->font, channel, key,
                                         channel == MIDI_PERCUSSION_CHANNEL);
            break;
        case MIDI_PITCH_BEND:
            tsf_channel_set_pitchwheel(synth->font, channel, key | value << MIDI_DATA_BITS);
            break;
    }
#endif
}

void lx_midi_synth_sysex(LxMidiSynth *synth, const unsigned char *message, size_t length)
{
    if (!synth || !message || length < 2 || message[0] != MIDI_SYSEX ||
        message[length - 1] != MIDI_SYSEX_END)
        return;
    if (synth->mt32)
    {
        lx_midi_mt32_sysex(synth->mt32, message, length);
        return;
    }
#if TWOINE_MIDI_EXTENDED
    if (synth->banks)
    {
        lx_midi_banks_sysex(synth->banks, message, length);
        return;
    }
#endif
    if (length == GM_SYSTEM_MESSAGE_SIZE && message[1] == MIDI_SYSEX_UNIVERSAL_NON_REAL_TIME &&
        message[3] == MIDI_SYSEX_GENERAL_MIDI && message[4] >= GM_SYSTEM_ON &&
        message[4] <= GM2_SYSTEM_ON)
        lx_midi_synth_reset(synth);
}

static void render(LxMidiSynth *synth, float *samples, unsigned count, unsigned rate, float volume,
                   int channels)
{
    if (!synth || !samples || !count || rate < SYNTH_RATE_MIN || rate > SYNTH_RATE_MAX ||
        !isfinite(volume))
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
#if TWOINE_MIDI_EXTENDED
    if (synth->banks)
    {
        lx_midi_banks_render(synth->banks, samples, count, rate, volume, channels);
        return;
    }
#endif
    if (rate != synth->rate)
    {
        synth->rate = rate;
        tsf_set_output(synth->font, TSF_STEREO_INTERLEAVED, rate, SOUNDFONT_GAIN_DB);
    }
    float rendered[RENDER_BLOCK_FRAMES * 2];
    for (unsigned offset = 0; offset < count;)
    {
        unsigned block = count - offset;
        if (block > RENDER_BLOCK_FRAMES)
            block = RENDER_BLOCK_FRAMES;
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
