/* Derived from original 2ine; zlib-style license. See ../LICENSE.txt. */
#ifndef TWOINE_AUDIO_H
#define TWOINE_AUDIO_H
typedef int (*LxAudioGeneratorFn)(void *data, float *stream, int length, int frequency);
/* Mono callbacks add length frames; stereo callbacks add interleaved L/R pairs. */
int lxRegisterStereoAudioGenerator(LxAudioGeneratorFn function, void *data, int singleton);
int lxRegisterAudioGenerator(LxAudioGeneratorFn function, void *data, int singleton);
void lxAudioShutdown(void);
#endif
