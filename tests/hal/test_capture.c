/* Public domain. No rights reserved. Wholly AI-authored. */
/* Dummy-driver host events exercise capture without manipulating the desktop. */
#define _POSIX_C_SOURCE 200809L
#include "hal/video.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s (%s)\n", __LINE__, #x, SDL_GetError());                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static unsigned resets, buttons;
static void drain(void)
{
    LxHostEvent event;
    /* Events generated while polling follow SDL's current poll-cycle sentinel. */
    for (unsigned pass = 0; pass < 2; pass++)
        while (lx_host_video_poll(&event))
        {
            resets += event.type == LX_HOST_EVENT_INPUT_RESET;
            buttons +=
                event.type == LX_HOST_EVENT_BUTTON_DOWN || event.type == LX_HOST_EVENT_BUTTON_UP;
        }
}
static void key(SDL_WindowID id, Uint32 type, SDL_Keycode symbol, SDL_Keymod modifiers)
{
    SDL_Event event = {0};
    event.type = type;
    event.key.windowID = id;
    event.key.key = symbol;
    event.key.mod = modifiers;
    CHECK(SDL_PushEvent(&event));
}
int main(void)
{
    int width, height, count;
    CHECK(lx_host_video_initialize(&width, &height));
    LxHostWindow *window = lx_host_window_create("capture contract", 0, 0, 320, 240, NULL);
    CHECK(window && !lx_host_window_is_captured(window));
    SDL_Window **windows = SDL_GetWindows(&count);
    CHECK(windows && count == 1);
    SDL_WindowID id = SDL_GetWindowID(windows[0]);
    SDL_free(windows);
    CHECK(lx_host_mouse_warp(100, 100));
    CHECK(lx_host_pointer_show(0) && SDL_CursorVisible());
    CHECK(lx_host_window_set_capture(window, 1));
    drain();
    CHECK(lx_host_window_is_captured(window) && resets == 1);
    key(id, SDL_EVENT_KEY_DOWN, SDLK_LCTRL, SDL_KMOD_CTRL);
    key(id, SDL_EVENT_KEY_DOWN, SDLK_LALT, SDL_KMOD_CTRL | SDL_KMOD_ALT);
    key(id, SDL_EVENT_KEY_UP, SDLK_LALT, SDL_KMOD_CTRL);
    key(id, SDL_EVENT_KEY_UP, SDLK_LCTRL, 0);
    drain();
    CHECK(!lx_host_window_is_captured(window) && resets == 2);
    CHECK(lx_host_pointer_show(0) && SDL_CursorVisible());
    SDL_Event event = {0};
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    event.window.windowID = id;
    CHECK(SDL_PushEvent(&event));
    drain();
    CHECK(!lx_host_window_is_captured(window));
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = id;
    event.button.button = SDL_BUTTON_LEFT;
    CHECK(SDL_PushEvent(&event));
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    CHECK(SDL_PushEvent(&event));
    drain();
    CHECK(lx_host_window_is_captured(window) && resets == 3 && !buttons);
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = id;
    CHECK(SDL_PushEvent(&event));
    drain();
    CHECK(!lx_host_window_is_captured(window) && resets == 4 && SDL_CursorVisible());
    lx_host_window_destroy(window);
    CHECK(!setenv("TWOINE_MOUSE_CAPTURE", "0", 1));
    window = lx_host_window_create("capture disabled", 0, 0, 320, 240, NULL);
    CHECK(window && !lx_host_window_set_capture(window, 1));
    CHECK(lx_host_mouse_warp(100, 100));
    CHECK(lx_host_pointer_show(0) && SDL_CursorVisible());
    lx_host_window_destroy(window);
    lx_host_video_quit();
    puts("Control-Option, click capture, focus release and guest-warp suppression passed");
    return 0;
}
