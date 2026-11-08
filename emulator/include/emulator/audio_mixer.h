/* Derived from original 2ine; zlib-style license. See ../../../LICENSE.txt. */
#ifndef TWOINE_EMULATOR_AUDIO_MIXER_H
#define TWOINE_EMULATOR_AUDIO_MIXER_H
typedef int (*LxAudioGeneratorFn)(void *data, float *stream, int length, int frequency);
/* Mono callbacks add length frames; stereo callbacks add interleaved L/R pairs. */
int lxRegisterStereoAudioGenerator(LxAudioGeneratorFn function, void *data, int singleton);
int lxRegisterAudioGenerator(LxAudioGeneratorFn function, void *data, int singleton);
/* Call outside a generator. Returns after any in-flight callback has finished. */
void lxUnregisterAudioGenerator(LxAudioGeneratorFn function, void *data);
void lxAudioShutdown(void);
#endif
