/* Public domain. No rights reserved. Wholly AI-authored. */
/* Controlled dummy-video clipboard fixtures never access a desktop clipboard. */
#include "host/clipboard.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static unsigned deferred;

static void *provider(void *context, const char *mime, size_t *size)
{
    (void)context;
    CHECK(!strcmp(mime, "text/rtf"));
    const char data[] = "{\\rtf1\\bin3 a\0b}";
    void *copy = malloc(sizeof data - 1);
    CHECK(copy);
    memcpy(copy, data, sizeof data - 1);
    *size = sizeof data - 1;
    deferred++;
    return copy;
}

static int worker(void *context)
{
    int *success = context;
    const LxHostClipboardItem item = {"text/plain;charset=utf-8", "late", 4};
    *success = lx_host_clipboard_offer(&item, 1, NULL, NULL, NULL);
    return 0;
}

int main(void)
{
    CHECK(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(!strcmp(SDL_GetCurrentVideoDriver(), "dummy"));
    const char text[] = "fixture\n";
    LxHostClipboardItem entries[] = {{"text/plain;charset=utf-8", text, sizeof text - 1},
                                     {"text/rtf", NULL, 0}};
    LxHostClipboardState state;
    CHECK(lx_host_clipboard_offer(entries, 2, provider, NULL, &state) && state.owned);
    CHECK(deferred == 0 && lx_host_clipboard_has("text/rtf"));
    size_t size;
    void *copy = lx_host_clipboard_read("text/plain;charset=utf-8", &size);
    CHECK(copy && size == sizeof text - 1 && !memcmp(copy, text, size));
    free(copy);
    copy = lx_host_clipboard_read("text/rtf", &size);
    CHECK(copy && size == 16 && deferred == 1 && ((char *)copy)[13] == 0);
    free(copy);
    CHECK(lx_host_clipboard_materialize());
    copy = lx_host_clipboard_read("text/rtf", &size);
    CHECK(copy && deferred == 1);
    free(copy);
    const unsigned char cp850[] = {'c', 'a', 'f', 0x82, '\r', '\n'};
    copy = lx_host_clipboard_text_utf8(850, cp850, sizeof cp850, &size);
    CHECK(copy && size == 6 && !memcmp(copy, "caf\xc3\xa9\n", size));
    size_t length;
    void *back = lx_host_clipboard_text_os2(850, copy, size, &length);
    CHECK(back && length == sizeof cp850 && !memcmp(back, cp850, length));
    free(copy);
    free(back);
    const unsigned char euro[] = {0x80};
    copy = lx_host_clipboard_text_utf8(1004, euro, 1, &size);
    CHECK(copy && size == 3 && !memcmp(copy, "\xe2\x82\xac", 3));
    free(copy);
    const unsigned char block[] = {0xdb};
    copy = lx_host_clipboard_text_utf8(437, block, 1, &size);
    CHECK(copy && size == 3 && !memcmp(copy, "\xe2\x96\x88", 3));
    back = lx_host_clipboard_text_os2(437, copy, size, &length);
    CHECK(back && length == 1 && !memcmp(back, block, 1));
    free(copy);
    free(back);
    CHECK(!lx_host_clipboard_text_os2(437, "\xc0\xaf", 2, &size));
    CHECK(!lx_host_clipboard_text_utf8(932, "x", 1, &size));
    const char html[] = "<p>caf\xc3\xa9</p>";
    copy = lx_host_clipboard_html_wrap(html, sizeof html - 1, &size);
    CHECK(copy);
    back = lx_host_clipboard_html_extract(copy, size, &length);
    CHECK(back && length == sizeof html - 1 + strlen("<!--StartFragment--><!--EndFragment-->"));
    CHECK(strstr(back, html));
    free(back);
    memcpy((char *)copy + 23, "9999999999", 10);
    CHECK(!lx_host_clipboard_html_extract(copy, size, &length));
    free(copy);
    unsigned char rgba[] = {255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0, 16, 32, 48, 255};
    const char *mimes[] = {"image/png", "image/bmp"};
    for (unsigned i = 0; i < 2; i++)
    {
        copy = lx_host_clipboard_encode_image(mimes[i], rgba, 2, 2, &size);
        CHECK(copy);
        unsigned width, height;
        back = lx_host_clipboard_decode_image(mimes[i], copy, size, &width, &height);
        CHECK(back && width == 2 && height == 2 && !memcmp(back, rgba, sizeof rgba));
        CHECK(!lx_host_clipboard_decode_image(mimes[i], copy, 20, &width, &height));
        free(copy);
        free(back);
    }
    /* A blocked main thread cannot cause an unbounded worker wait. A queued
       operation cancelled by timeout must not publish when pumping resumes. */
    int success = -1;
    SDL_Thread *thread = SDL_CreateThread(worker, "clipboard-timeout", &success);
    CHECK(thread);
    SDL_WaitThread(thread, NULL);
    CHECK(success == 0);
    SDL_PumpEvents();
    copy = lx_host_clipboard_read("text/plain;charset=utf-8", &size);
    CHECK(copy && size == sizeof text - 1 && !memcmp(copy, text, size));
    free(copy);
    CHECK(lx_host_clipboard_offer(NULL, 0, NULL, NULL, &state) && state.owned);
    SDL_Quit();
    return 0;
}
