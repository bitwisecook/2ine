/* Public domain. No rights reserved. Wholly AI-authored. */
/* Real SDL3 dummy audio consumption, software rendering and event translation. */
#include "hal/audio.h"
#include "hal/video.h"
#include "hal/font.h"
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
    CHECK(lx_host_font_count() == 12);
    CHECK(!lx_host_font_open(12, 16) && !lx_host_font_open(0, 0) && !lx_host_font_open(0, 513));
    for (unsigned i = 0; i < lx_host_font_count(); i++)
    {
        LxHostFont *font = lx_host_font_open(i, 16);
        LxHostFontMetrics metrics;
        CHECK(font && lx_host_font_metrics(font, &metrics));
        CHECK(metrics.family[0] && metrics.ascent > 0 && metrics.descent >= 0 && metrics.em == 16);
        CHECK(metrics.bold == ((i % 4) & 1) && metrics.italic == (((i % 4) >> 1) & 1));
        int wide = lx_host_font_advance(font, 'W'), narrow = lx_host_font_advance(font, 'i');
        CHECK(wide > 0 && narrow > 0 && (i < 8 ? wide > narrow : wide == narrow));
        LxHostGlyph glyph;
        CHECK(lx_host_font_glyph(font, 'A', &glyph));
        CHECK(glyph.width > 0 && glyph.height > 0 && glyph.coverage && glyph.advance > 0);
        unsigned coverage = 0, antialiased = 0;
        for (int pixel = 0; pixel < glyph.width * glyph.height; pixel++)
        {
            coverage += glyph.coverage[pixel];
            antialiased += glyph.coverage[pixel] > 0 && glyph.coverage[pixel] < 255;
        }
        CHECK(coverage && antialiased);
        lx_host_glyph_free(&glyph);
        CHECK(!glyph.coverage);
        lx_host_font_close(font);
    }
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
    uint64_t before_event = lx_host_video_ticks_ms();
    LxHostEvent event = {LX_HOST_EVENT_QUIT, 0, NULL};
    CHECK(lx_host_video_push(&event));
    int quit = 0;
    while (lx_host_video_poll(&event))
        if (event.type == LX_HOST_EVENT_QUIT)
        {
            CHECK(event.timestamp + 1 >= before_event);
            CHECK(event.timestamp <= lx_host_video_ticks_ms() + 1);
            quit = 1;
        }
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
    puts("FreeType faces/glyphs, SDL3 audio, pixels, presentation and events passed");
    return 0;
}
