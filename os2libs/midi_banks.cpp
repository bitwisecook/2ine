/* Public domain. No rights reserved. Wholly AI-authored. */
/* Ordered MIDI routing to FluidSynth banks and LiquidSFZ instruments. */
#include "midi_banks.h"
extern "C"
{
#include "midi_config.h"
}
#include <fluidsynth.h>
#include <liquidsfz.hh>
#include <array>
#include <cctype>
#include <cerrno>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

struct LxMidiBanks
{
    fluid_settings_t *settings = nullptr;
    fluid_synth_t *fluid = nullptr;
    unsigned rate = 48000;
    std::string profile;

    struct Channel
    {
        int target = 0, program = -1, bank = -1, percussion = -1, muted = 0;
        int font = -1, selectedProgram = 0, msb = 0, lsb = 0;
        std::unique_ptr<LiquidSFZ::Synth> sfz;
    };

    std::array<Channel, 16> channels;

    ~LxMidiBanks()
    {
        if (fluid)
            delete_fluid_synth(fluid);
        if (settings)
            delete_fluid_settings(settings);
    }
};

static std::string configured(const char *key, const char *fallback = "")
{
    char *value = lx_midi_config_value(key);
    std::string result = value ? value : fallback;
    free(value);
    return result;
}

static bool sfz_file(const std::string &path)
{
    if (path.size() < 4)
        return false;
    std::string suffix = path.substr(path.size() - 4);
    for (char &letter : suffix)
        letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    return suffix == ".sfz";
}

static void select_program(LxMidiBanks *s, unsigned input)
{
    auto &c = s->channels[input];
    if (c.sfz || c.font < 0)
        return;
    int drum = c.percussion >= 0 ? c.percussion
                                 : c.target == 9 || (s->profile == "xg" && c.msb >= 126) ||
                                       (s->profile == "gm2" && c.msb == 120);
    int bank = c.bank;
    if (bank < 0)
    {
        if (drum)
            bank = 128;
        else if (s->profile == "gs")
            bank = c.msb;
        else if (s->profile == "xg")
            bank = c.msb == 127 ? 128 : c.lsb;
        else if (s->profile == "gm2")
            bank = c.msb == 120 ? 128 : c.lsb;
        else
            bank = 0;
    }
    int program = c.program >= 0 ? c.program : c.selectedProgram;
    fluid_synth_set_channel_type(s->fluid, c.target,
                                 drum ? CHANNEL_TYPE_DRUM : CHANNEL_TYPE_MELODIC);
    if (fluid_synth_program_select(s->fluid, c.target, c.font, bank, program) != FLUID_OK)
        if (fluid_synth_program_select(s->fluid, c.target, c.font, drum ? 128 : 0, program) !=
            FLUID_OK)
            fluid_synth_program_select(s->fluid, c.target, c.font, drum ? 128 : 0, 0);
}

extern "C" LxMidiBanks *lx_midi_banks_create(const char *default_bank, unsigned rate)
{
    try
    {
        auto s = std::make_unique<LxMidiBanks>();
        s->rate = rate;
        s->profile = configured("midi_profile", "gm");
        if (s->profile != "gm" && s->profile != "gm2" && s->profile != "gs" && s->profile != "xg" &&
            s->profile != "mt32")
        {
            fprintf(stderr, "2ine: Unknown MIDI mapping profile.\n");
            return nullptr;
        }
        s->settings = new_fluid_settings();
        if (!s->settings)
            return nullptr;
        fluid_settings_setnum(s->settings, "synth.sample-rate", rate);
        fluid_settings_setnum(s->settings, "synth.gain", 0.5);
        fluid_settings_setint(s->settings, "synth.reverb.active", 0);
        fluid_settings_setint(s->settings, "synth.chorus.active", 0);
        fluid_settings_setint(s->settings, "synth.polyphony", 256);
        fluid_settings_setint(s->settings, "synth.threadsafe-api", 0);
        fluid_settings_setint(s->settings, "synth.cpu-cores", 1);
        fluid_settings_setstr(s->settings, "synth.midi-bank-select",
                              s->profile == "gm2"  ? "mma"
                              : s->profile == "gs" ? "gs"
                              : s->profile == "xg" ? "xg"
                                                   : "gm");
        s->fluid = new_fluid_synth(s->settings);
        if (!s->fluid)
            return nullptr;
        std::unordered_map<std::string, int> loaded;
        const char *override = getenv("TWOINE_SOUNDFONT");
        std::string main = override && *override ? override : configured("soundfont", default_bank);
        for (unsigned i = 0; i < 16; i++)
        {
            auto &c = s->channels[i];
            char key[64];
            snprintf(key, sizeof key, "channel_%u", i + 1);
            std::string mapping = configured(key);
            int target = static_cast<int>(i) + 1;
            if (s->profile == "mt32")
            {
                if (i >= 1 && i <= 8)
                    target = static_cast<int>(i);
                c.muted = i == 0 || i > 9;
            }
            if (!mapping.empty())
            {
                int *fields[] = {&target, &c.program, &c.bank, &c.percussion, &c.muted};
                const int minimum[] = {1, -1, -1, -1, 0}, maximum[] = {16, 127, 16383, 1, 1};
                const char *position = mapping.c_str();
                for (unsigned field = 0; field < 5; field++)
                {
                    char *end = nullptr;
                    errno = 0;
                    long value = std::strtol(position, &end, 10);
                    if (errno || end == position || value < minimum[field] ||
                        value > maximum[field] || (field < 4 ? *end != ',' : *end != 0))
                    {
                        fprintf(stderr,
                                "2ine: Invalid channel_%u mapping in audio configuration.\n",
                                i + 1);
                        return nullptr;
                    }
                    *fields[field] = static_cast<int>(value);
                    position = end + (field < 4);
                }
            }
            c.target = target - 1;
            snprintf(key, sizeof key, "channel_%u_soundfont", i + 1);
            std::string path = configured(key, main.c_str());
            if (sfz_file(path))
            {
                c.sfz = std::make_unique<LiquidSFZ::Synth>();
                c.sfz->set_sample_rate(rate);
                c.sfz->set_max_voices(64);
                c.sfz->set_log_level(LiquidSFZ::Log::ERROR);
                if (!c.sfz->load(path))
                    return nullptr;
            }
            else
            {
                auto existing = loaded.find(path);
                if (existing != loaded.end())
                    c.font = existing->second;
                else
                {
                    c.font = fluid_synth_sfload(s->fluid, path.c_str(), 0);
                    if (c.font < 0)
                        return nullptr;
                    loaded.emplace(path, c.font);
                }
            }
        }
        lx_midi_banks_reset(s.get());
        return s.release();
    }
    catch (const std::exception &error)
    {
        fprintf(stderr, "2ine: sound-bank initialization: %s\n", error.what());
        return nullptr;
    }
}

extern "C" void lx_midi_banks_destroy(LxMidiBanks *banks)
{
    delete banks;
}

extern "C" int lx_midi_banks_reset(LxMidiBanks *s)
{
    if (!s)
        return 0;
    for (int channel = 0; channel < 16; channel++)
        fluid_synth_all_sounds_off(s->fluid, channel);
    fluid_synth_system_reset(s->fluid);
    float discarded[512];
    for (int i = 0; i < 8; i++)
        fluid_synth_write_float(s->fluid, 256, discarded, 0, 2, discarded, 1, 2);
    for (unsigned i = 0; i < 16; i++)
    {
        auto &c = s->channels[i];
        c.selectedProgram = c.msb = c.lsb = 0;
        if (c.sfz)
            c.sfz->system_reset();
        select_program(s, i);
    }
    return 1;
}

extern "C" void lx_midi_banks_event(LxMidiBanks *s, unsigned status, unsigned key, unsigned value)
{
    if (!s || status < 0x80 || status >= 0xf0 || key > 127 || value > 127)
        return;
    unsigned input = status & 15;
    auto &c = s->channels[input];
    if (c.muted)
        return;
    if (c.sfz)
    {
        switch (status & 0xf0)
        {
            case 0x80:
                c.sfz->add_event_note_off(0, c.target, key);
                break;
            case 0x90:
                c.sfz->add_event_note_on(0, c.target, key, value);
                break;
            case 0xb0:
                c.sfz->add_event_cc(0, c.target, key, value);
                break;
            case 0xe0:
                c.sfz->add_event_pitch_bend(0, c.target, key | value << 7);
                break;
        }
        return;
    }
    switch (status & 0xf0)
    {
        case 0x80:
            fluid_synth_noteoff(s->fluid, c.target, key);
            break;
        case 0x90:
            if (value)
                fluid_synth_noteon(s->fluid, c.target, key, value);
            else
                fluid_synth_noteoff(s->fluid, c.target, key);
            break;
        case 0xa0:
            fluid_synth_key_pressure(s->fluid, c.target, key, value);
            break;
        case 0xb0:
            if (key == 0)
            {
                c.msb = value;
                select_program(s, input);
            }
            else if (key == 32)
            {
                c.lsb = value;
                select_program(s, input);
            }
            else
                fluid_synth_cc(s->fluid, c.target, key, value);
            break;
        case 0xc0:
            c.selectedProgram = key;
            select_program(s, input);
            break;
        case 0xd0:
            fluid_synth_channel_pressure(s->fluid, c.target, key);
            break;
        case 0xe0:
            fluid_synth_pitch_bend(s->fluid, c.target, key | value << 7);
            break;
    }
}

extern "C" void lx_midi_banks_sysex(LxMidiBanks *s, const unsigned char *message, size_t length)
{
    if (!s || length < 2)
        return;
    fluid_synth_sysex(s->fluid, reinterpret_cast<const char *>(message + 1),
                      static_cast<int>(length - 2), nullptr, nullptr, nullptr, 0);
    // Preserve user routing after device reset messages.
    if ((length == 6 && message[1] == 0x7e && message[3] == 9) ||
        (length == 11 && message[1] == 0x41 && message[5] == 0x40 && message[7] == 0x7f) ||
        (length == 9 && message[1] == 0x43 && message[3] == 0x4c && message[6] == 0x7e))
        lx_midi_banks_reset(s);
}

extern "C" void lx_midi_banks_render(LxMidiBanks *s, float *samples, unsigned count, unsigned rate,
                                     float volume, int channels)
{
    if (!s)
        return;
    if (rate != s->rate)
    {
        s->rate = rate;
        /* Pinned FluidSynth 2.4 retains this API for a live PCM rate change. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        fluid_synth_set_sample_rate(s->fluid, rate);
#pragma GCC diagnostic pop
        for (auto &c : s->channels)
            if (c.sfz)
                c.sfz->set_sample_rate(rate);
    }
    std::array<float, 256> left, right, sfzLeft, sfzRight;
    for (unsigned offset = 0; offset < count;)
    {
        unsigned block = count - offset;
        if (block > left.size())
            block = left.size();
        fluid_synth_write_float(s->fluid, block, left.data(), 0, 1, right.data(), 0, 1);
        for (auto &c : s->channels)
            if (c.sfz)
            {
                float *outputs[] = {sfzLeft.data(), sfzRight.data()};
                c.sfz->process(outputs, block);
                for (unsigned i = 0; i < block; i++)
                {
                    left[i] += sfzLeft[i];
                    right[i] += sfzRight[i];
                }
            }
        for (unsigned i = 0; i < block; i++)
            if (channels == 1)
                samples[offset + i] += (left[i] + right[i]) * (0.5f * volume);
            else
            {
                samples[2 * (offset + i)] += left[i] * volume;
                samples[2 * (offset + i) + 1] += right[i] * volume;
            }
        offset += block;
    }
}
