/* Public domain. No rights reserved. Wholly AI-authored. */
/* X11-driven fullscreen presentation and logical pointer-coordinate contract. */
#include "hal/video.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #expression,                   \
                    lx_host_video_error());                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    int width, height;
    CHECK(lx_host_video_initialize(&width, &height));
    LxHostWindow *window =
        lx_host_window_create("2ine fullscreen contract", 37, 53, 160, 120, NULL);
    CHECK(window);
    LxHostRect clip = {0, 0, 160, 120};
    LxHostRect left = {0, 0, 80, 120}, right = {80, 0, 80, 120};
    CHECK(lx_host_window_fill(window, &clip, &left, 224, 32, 64));
    CHECK(lx_host_window_fill(window, &clip, &right, 32, 192, 96));
    lx_host_window_show(window);
    CHECK(lx_host_window_focus(window));
    int previous = -1, running = 1;
    uint64_t deadline = lx_host_video_ticks_ms() + 30000;
    while (running && lx_host_video_ticks_ms() < deadline)
    {
        LxHostEvent event;
        if (lx_host_video_wait_timeout(&event, 10))
        {
            if (event.type == LX_HOST_EVENT_KEY_DOWN || event.type == LX_HOST_EVENT_KEY_UP)
                CHECK(event.key != LX_HOST_KEY_RETURN);
            if (event.type == LX_HOST_EVENT_KEY_DOWN)
            {
                if (event.key == 'q')
                    running = 0;
                else if (event.key == 'w')
                    CHECK(lx_host_mouse_warp(77, 83));
                else if (event.key == 'p')
                {
                    int x, y;
                    lx_host_mouse_position(&x, &y);
                    printf("pointer %d %d\n", x, y);
                }
                else if (event.key == LX_HOST_KEY_F1 + 10)
                    puts("F11");
            }
            if (event.type == LX_HOST_EVENT_BUTTON_DOWN)
                printf("click %d %d\n", event.x, event.y);
            if (event.type == LX_HOST_EVENT_RESIZED)
                CHECK(!lx_host_window_is_fullscreen(window) && event.width == 160 &&
                      event.height == 120);
            if (event.type == LX_HOST_EVENT_CLOSE || event.type == LX_HOST_EVENT_QUIT)
                running = 0;
        }
        int fullscreen = lx_host_window_is_fullscreen(window);
        if (fullscreen != previous)
        {
            printf("fullscreen %d\n", fullscreen);
            previous = fullscreen;
        }
        CHECK(lx_host_window_present(window));
    }
    CHECK(!running);
    uint8_t r, g, b;
    CHECK(lx_host_window_pixel(window, 40, 60, &r, &g, &b));
    CHECK(r == 224 && g == 32 && b == 64);
    CHECK(lx_host_window_pixel(window, 120, 60, &r, &g, &b));
    CHECK(r == 32 && g == 192 && b == 96);
    lx_host_window_destroy(window);
    lx_host_video_quit();
    puts("fullscreen contract passed");
    return 0;
}
