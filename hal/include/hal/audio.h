/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_AUDIO_H
#define TWOINE_HOST_AUDIO_H
typedef struct LxHostAudio LxHostAudio;
typedef void (*LxHostAudioCallback)(void *data, float *samples, int count, int frequency);
LxHostAudio *lx_host_audio_open(LxHostAudioCallback callback, void *data);
void lx_host_audio_lock(LxHostAudio *audio);
void lx_host_audio_unlock(LxHostAudio *audio);
void lx_host_audio_close(LxHostAudio *audio);
#endif
