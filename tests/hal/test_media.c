/* Public domain. No rights reserved. Wholly AI-authored. */
/* Real SDL3 dummy audio consumption, software rendering and event translation. */
#include "hal/audio.h"
#include "hal/video.h"
#include "hal/host.h"
#include "runtime/audio.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, lx_host_video_error());    \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static unsigned samples_played;
static unsigned generator_calls, generator_live = 1;
static int policy_generator(void *unused, float *output, int count, int frequency)
{
    (void)unused;
    CHECK(frequency == 48000);
    for (int i = 0; i < count; i++)
        output[i] += 0.125f;
    __atomic_add_fetch(&generator_calls, 1, __ATOMIC_SEQ_CST);
    return __atomic_load_n(&generator_live, __ATOMIC_SEQ_CST);
}
static void generate(void *unused, float *output, int count, int frequency)
{
    (void)unused;
    CHECK(frequency == 48000 && count > 0 && count <= 1024);
    for (int i = 0; i < count; i++)
        output[i] = 0.125f;
    __atomic_add_fetch(&samples_played, (unsigned)count, __ATOMIC_SEQ_CST);
}
int main(void)
{
    LxHostAudio *audio = lx_host_audio_open(generate, NULL);
    CHECK(audio);
    for (unsigned i = 0; !__atomic_load_n(&samples_played, __ATOMIC_SEQ_CST) && i < 200; i++)
        lx_host_sleep_ms(5);
    CHECK(__atomic_load_n(&samples_played, __ATOMIC_SEQ_CST) > 0);
    int width, height;
    CHECK(lx_host_video_initialize(&width, &height) && width > 0 && height > 0);
    int identity = 42;
    LxHostWindow *window = lx_host_window_create("2ine HAL test", 0, 0, 16, 16, &identity);
    CHECK(window);
    CHECK(lx_host_window_data(window) == &identity);
    LxHostRect clip = {0, 0, 16, 16}, rectangle = {1, 2, 8, 8};
    CHECK(lx_host_window_fill(window, &clip, &rectangle, 64, 128, 192));
    uint8_t r, g, b;
    CHECK(lx_host_window_pixel(window, 2, 3, &r, &g, &b));
    CHECK(r == 64 && g == 128 && b == 192);
    CHECK(lx_host_window_present(window));
    lx_host_window_show(window);
    LxHostEvent event = {LX_HOST_EVENT_QUIT, 0, NULL};
    CHECK(lx_host_video_push(&event));
    int quit = 0;
    while (lx_host_video_poll(&event))
        if (event.type == LX_HOST_EVENT_QUIT)
            quit = 1;
    CHECK(quit);
    lx_host_window_destroy(window);
    lx_host_video_quit();
    unsigned before = __atomic_load_n(&samples_played, __ATOMIC_SEQ_CST);
    lx_host_sleep_ms(60);
    CHECK(__atomic_load_n(&samples_played, __ATOMIC_SEQ_CST) > before);
    lx_host_audio_close(audio);
    CHECK(lxRegisterAudioGenerator(policy_generator, NULL, 1) == 1);
    CHECK(lxRegisterAudioGenerator(policy_generator, NULL, 1) == 2);
    for (int i = 0; !__atomic_load_n(&generator_calls, __ATOMIC_SEQ_CST) && i < 200; i++)
        lx_host_sleep_ms(5);
    CHECK(__atomic_load_n(&generator_calls, __ATOMIC_SEQ_CST));
    __atomic_store_n(&generator_live, 0, __ATOMIC_SEQ_CST);
    int registered = 2;
    for (int i = 0; registered == 2 && i < 200; i++)
    {
        lx_host_sleep_ms(5);
        registered = lxRegisterAudioGenerator(policy_generator, NULL, 1);
    }
    CHECK(registered == 1);
    lxAudioShutdown();
    lxAudioShutdown();
    puts("SDL3 audio, pixels, presentation and event translation passed");
    return 0;
}
