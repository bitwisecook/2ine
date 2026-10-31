/* Public domain. No rights reserved. Wholly AI-authored. */
/* SDL3 owns stream conversion and device callbacks; callers own sample policy. */
#include "hal/audio.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
struct LxHostAudio
{
    SDL_AudioStream *stream;
    LxHostAudioCallback callback;
    void *data;
};
static void SDLCALL fill(void *data, SDL_AudioStream *stream, int needed, int total)
{
    (void)total;
    LxHostAudio *audio = data;
    float samples[1024];
    while (needed > 0)
    {
        int count = (needed + (int)sizeof(float) - 1) / (int)sizeof(float);
        if (count > 1024)
            count = 1024;
        SDL_memset(samples, 0, (size_t)count * sizeof(float));
        audio->callback(audio->data, samples, count, 48000);
        if (!SDL_PutAudioStreamData(stream, samples, count * (int)sizeof(float)))
            return;
        needed -= count * (int)sizeof(float);
    }
}
LxHostAudio *lx_host_audio_open(LxHostAudioCallback callback, void *data)
{
    if (!callback || !SDL_InitSubSystem(SDL_INIT_AUDIO))
        return NULL;
    LxHostAudio *audio = calloc(1, sizeof *audio);
    if (!audio)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    audio->callback = callback;
    audio->data = data;
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, 48000};
    audio->stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, fill, audio);
    if (!audio->stream || !SDL_ResumeAudioStreamDevice(audio->stream))
    {
        if (audio->stream)
            SDL_DestroyAudioStream(audio->stream);
        free(audio);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    return audio;
}
void lx_host_audio_lock(LxHostAudio *audio)
{
    SDL_LockAudioStream(audio->stream);
}
void lx_host_audio_unlock(LxHostAudio *audio)
{
    SDL_UnlockAudioStream(audio->stream);
}
void lx_host_audio_close(LxHostAudio *audio)
{
    if (!audio)
        return;
    /* Two 1024-sample periods let the last generated samples reach the device. */
    SDL_Delay(42);
    SDL_DestroyAudioStream(audio->stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(audio);
}
