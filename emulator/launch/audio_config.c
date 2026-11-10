/* Public domain. No rights reserved. Wholly AI-authored. */
/* The audio.conf projection of a ball's MIDI settings for the runtime. */
#include "ball/store.h"
#include "emulator/launch/audio_config.h"
#include "environment/audio.h"

enum
{
    MIDI_CHANNELS = 16
};

int audio_config(TbBall *c, const char *build, cJSON *env)
{
    cJSON *defaults = NULL;
    const cJSON *settings = get(c->manifest, "audio");
    if (!settings)
        settings = defaults = audio_defaults();
    if (!audio_validate(c, settings))
    {
        cJSON_Delete(defaults);
        return 0;
    }
    char *bank = audio_bank_path(c, field(settings, "bank"), build);
    if (bank && strpbrk(bank, "\r\n"))
        tb_ball_fail(c, "A sound-bank path must fit on one line");
    char *text = bank ? tb_ball_format(c, "schema=2\nsoundfont=%s\nmidi_profile=%s\n", bank,
                                       field(settings, "profile"))
                      : NULL;
    const cJSON *channels = get(settings, "channels");
    for (int i = 0; text && i < MIDI_CHANNELS; i++)
    {
        const cJSON *channel = cJSON_GetArrayItem(channels, i);
        const char *id = field(channel, "bank");
        char *path = *id ? audio_bank_path(c, id, build) : bank;
        if (!path || strpbrk(path, "\r\n"))
        {
            if (!c->error)
                tb_ball_fail(c, "A sound-bank path must fit on one line");
            break;
        }
        text = tb_ball_format(
            c, "%schannel_%d=%d,%d,%d,%d,%d\nchannel_%d_soundfont=%s\n", text, i + 1,
            get(channel, "target")->valueint, get(channel, "program")->valueint,
            get(channel, "bankNumber")->valueint, get(channel, "percussion")->valueint,
            cJSON_IsTrue(get(channel, "muted")), i + 1, path);
    }
    cJSON_Delete(defaults);
    char *path = tb_ball_join(c, c->ball, "audio.conf");
    if (c->error || !text || !tb_atomic_write(path, text, strlen(text)))
        return c->error ? 0 : tb_ball_fail(c, "Cannot write ball audio configuration");
    put(env, "TWOINE_AUDIO_CONFIG", cJSON_CreateString(path));
    put(env, "TWOINE_SOUNDFONT", cJSON_CreateString(bank));
    return 1;
}
