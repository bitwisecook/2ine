/* Public domain. No rights reserved. Wholly AI-authored. */
/* State shared by the SDL display, window, render, event and pointer sources. */
#ifndef TWOINE_HOST_SDL_WINDOW_INTERNAL_H
#define TWOINE_HOST_SDL_WINDOW_INTERNAL_H
#include "host/window.h"
#include "host/core/window/chrome_hooks.h"
#include <SDL3/SDL.h>

/* The SDL window property that holds the owning LxHostWindow. */
#define LX_SDL_WINDOW_PROPERTY "2ine.window"

struct LxHostWindow
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *video_texture;
    unsigned video_width, video_height, video_format, video_colors;
    void *data;
    int x, y;
    int width, height;
    int dirty;
    int fullscreen;
    int startup_checked;
    int suppress_enter;
    int restoring;
    int windowed_x, windowed_y;
    int capture_control, captured, capture_chord,
        suppress_click; /* 0 legacy, 1 managed, 2 disabled. */
    LxWindowChrome *chrome;
};

/* User events registered by the first display initialization; zero once the
   last display reference is released. */
extern Uint32 LxSdlWakeEvent;
extern Uint32 LxSdlCaptureResetEvent;
/* Windows with managed or disabled mouse capture (TWOINE_MOUSE_CAPTURE). */
extern unsigned LxSdlControlledWindows;

/* Serializes display initialization and shutdown with the pointer registry. */
void lx_sdl_video_lock(void);
void lx_sdl_video_unlock(void);
/* Releases an external graphics API's binding before renderer operations. */
void lx_sdl_prepare_renderer(void);
/* SDL frees native cursors when the display closes; their owners recreate them. */
void lx_sdl_pointers_invalidate(void);
/* The LxHostWindow behind an SDL window, or NULL. */
LxHostWindow *lx_sdl_window_from(SDL_Window *window);
LxHostWindow *lx_sdl_focused_window(void);
void lx_sdl_window_toggle_capture(void *window);
#endif
