/* Derived from original 2ine; zlib-style license. See ../LICENSE.txt. */
/* OS/2 audio generators share a stream. The HAL owns device conversion/lifetime. */
#include "runtime/audio.h"
#include "hal/audio.h"
#include "hal/host_sync.h"
#include <stdlib.h>
typedef struct Generator
{
    LxAudioGeneratorFn function;
    void *data;
    struct Generator *next;
} Generator;
static Generator *generators;
static LxHostAudio *device;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMutex *control;
static void initialize(void)
{
    control = lx_host_mutex_create(0, 0);
}
static void mix(void *unused, float *stream, int samples, int frequency)
{
    (void)unused;
    Generator **link = &generators;
    while (*link)
    {
        Generator *generator = *link;
        if (generator->function(generator->data, stream, samples, frequency))
            link = &generator->next;
        else
        {
            *link = generator->next;
            free(generator);
        }
    }
}
int lxRegisterAudioGenerator(LxAudioGeneratorFn function, void *data, int singleton)
{
    if (!function)
        return 0;
    lx_host_once(&once, initialize);
    if (!control || lx_host_mutex_lock(control, UINT32_MAX) != LX_HOST_LOCK_OK)
        return 0;
    if (!device)
    {
        device = lx_host_audio_open(mix, NULL);
        if (!device)
        {
            lx_host_mutex_unlock(control);
            return 0;
        }
    }
    Generator *generator = malloc(sizeof *generator);
    if (!generator)
    {
        lx_host_mutex_unlock(control);
        return 0;
    }
    generator->function = function;
    generator->data = data;
    lx_host_audio_lock(device);
    if (singleton)
    {
        for (Generator *item = generators; item; item = item->next)
            if (item->function == function)
            {
                lx_host_audio_unlock(device);
                free(generator);
                lx_host_mutex_unlock(control);
                return 2;
            }
    }
    generator->next = generators;
    generators = generator;
    lx_host_audio_unlock(device);
    lx_host_mutex_unlock(control);
    return 1;
}
void lxAudioShutdown(void)
{
    lx_host_once(&once, initialize);
    if (!control || lx_host_mutex_lock(control, UINT32_MAX) != LX_HOST_LOCK_OK)
        return;
    lx_host_audio_close(device);
    device = NULL;
    while (generators)
    {
        Generator *next = generators->next;
        free(generators);
        generators = next;
    }
    lx_host_mutex_unlock(control);
}
