/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_AUDIO_H
#define TWOINE_HOST_AUDIO_H
typedef struct LxHostAudio LxHostAudio;
#define LX_HOST_AUDIO_BLOCK_FRAMES 1024
/* Count is at most LX_HOST_AUDIO_BLOCK_FRAMES; multi-channel frames are interleaved. */
typedef void (*LxHostAudioCallback)(void *data, float *samples, int count, int frequency);
LxHostAudio *lx_host_audio_open(LxHostAudioCallback callback, void *data);
/* Supports mono (1) and stereo (2); lx_host_audio_open selects mono. */
LxHostAudio *lx_host_audio_open_channels(LxHostAudioCallback callback, void *data, int channels);
void lx_host_audio_lock(LxHostAudio *audio);
void lx_host_audio_unlock(LxHostAudio *audio);
void lx_host_audio_close(LxHostAudio *audio);
#endif
