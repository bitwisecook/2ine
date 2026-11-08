/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host windows and render targets contain no OS/2 window/message state. */
#include "host/sdl/window/internal.h"
#include "host/desktop.h"
#include "host/core/desktop/session_hooks.h"
#include <stdlib.h>

#define OPAQUE 255

unsigned LxSdlControlledWindows;

static int activateWindowSurface(LxHostWindow *window)
{
    const char *renderer = SDL_GetRendererName(window->renderer);
    if (!renderer)
        return 0;
    if (SDL_strcmp(renderer, "software"))
    {
        /* Hardware renderers retain their texture targets across native surface
           changes. Drawing to their window here can acquire an unpresented
           drawable whose dimensions become stale during another resize. */
        window->dirty = 1;
        return 1;
    }
    SDL_Texture *target = SDL_GetRenderTarget(window->renderer);
    /* Native surface changes can invalidate the software renderer's window
       surface. Activate it explicitly before returning to a retained target;
       otherwise its first draw can be redirected to the window surface. */
    int ready = SDL_SetRenderTarget(window->renderer, NULL) &&
                SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, OPAQUE) &&
                SDL_RenderClear(window->renderer) && SDL_FlushRenderer(window->renderer);
    int restored = SDL_SetRenderTarget(window->renderer, target);
    if (ready && restored)
        window->dirty = 1;
    return ready && restored;
}

LxHostWindow *lx_sdl_window_from(SDL_Window *window)
{
    return window ? SDL_GetPointerProperty(SDL_GetWindowProperties(window), LX_SDL_WINDOW_PROPERTY,
                                           NULL)
                  : NULL;
}

LxHostWindow *lx_sdl_focused_window(void)
{
    return lx_sdl_window_from(SDL_GetKeyboardFocus());
}

int lx_host_window_is_captured(LxHostWindow *window)
{
    return window && window->capture_control && window->captured;
}

int lx_host_window_set_capture(LxHostWindow *window, int captured)
{
    if (!window || window->capture_control != 1)
        return 0;
    captured = captured != 0;
    if (window->captured == captured)
        return 1;
    if (!SDL_SetWindowMouseGrab(window->window, captured) && captured)
        return 0;
    window->captured = captured;
    lx_window_chrome_capture(window->chrome, captured);
    if (captured)
    {
        SDL_RaiseWindow(window->window);
        SDL_HideCursor();
    }
    else
    {
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_ShowCursor();
    }
    SDL_Event reset = {0};
    reset.type = LxSdlCaptureResetEvent;
    reset.user.windowID = SDL_GetWindowID(window->window);
    SDL_PushEvent(&reset);
    return 1;
}

void lx_sdl_window_toggle_capture(void *opaque)
{
    LxHostWindow *window = opaque;
    lx_host_window_set_capture(window, !window->captured);
}

void lx_host_window_set_cycle_policy(LxHostWindow *window, int eligible, int modal)
{
    if (!window || !window->window)
        return;
    SDL_PropertiesID properties = SDL_GetWindowProperties(window->window);
    SDL_SetBooleanProperty(properties, "2ine.cycle.eligible", eligible != 0);
    SDL_SetBooleanProperty(properties, "2ine.cycle.modal", modal != 0);
    lx_desktop_poll();
}

void lx_host_window_get_cycle_policy(LxHostWindow *window, int *eligible, int *modal)
{
    SDL_PropertiesID properties =
        window && window->window ? SDL_GetWindowProperties(window->window) : 0;
    if (eligible)
        *eligible = properties && SDL_GetBooleanProperty(properties, "2ine.cycle.eligible", true);
    if (modal)
        *modal = properties && SDL_GetBooleanProperty(properties, "2ine.cycle.modal", false);
}

LxHostWindow *lx_host_window_create(const char *title, int x, int y, int width, int height,
                                    void *data)
{
    lx_sdl_prepare_renderer();
    LxHostWindow *window = calloc(1, sizeof *window);
    if (!window)
        return NULL;
    window->data = data;
    window->dirty = 1;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->window = SDL_CreateWindow(title ? title : "", width, height,
                                      SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (!window->window)
        goto failed;
    if (!SDL_SetPointerProperty(SDL_GetWindowProperties(window->window), LX_SDL_WINDOW_PROPERTY,
                                window))
        goto failed;
    const char *capture = getenv("TWOINE_MOUSE_CAPTURE");
    window->capture_control = !capture || !*capture ? 0 : SDL_strcmp(capture, "0") ? 1 : 2;
    if (window->capture_control)
    {
        LxSdlControlledWindows++;
        if (window->capture_control == 1)
            window->chrome =
                lx_window_chrome_create(window->window, lx_sdl_window_toggle_capture, window);
    }
    window->renderer = SDL_CreateRenderer(window->window, NULL);
    if (!window->renderer)
        goto failed;
    window->texture = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET, width, height);
    if (!window->texture || !SDL_SetRenderTarget(window->renderer, window->texture) ||
        !SDL_SetTextureBlendMode(window->texture, SDL_BLENDMODE_NONE) ||
        !SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, OPAQUE) ||
        !SDL_RenderClear(window->renderer))
        goto failed;
    SDL_StartTextInput(window->window);
    SDL_SetWindowPosition(window->window, x, y);
    return window;
failed:
    lx_host_window_destroy(window);
    return NULL;
}

void lx_host_window_destroy(LxHostWindow *window)
{
    lx_sdl_prepare_renderer();
    if (!window)
        return;
    if (window->capture_control)
    {
        lx_host_window_set_capture(window, 0);
        lx_window_chrome_destroy(window->chrome);
        LxSdlControlledWindows--;
    }
    lx_desktop_forget(window->window);
    SDL_DestroyTexture(window->video_texture);
    SDL_DestroyTexture(window->texture);
    SDL_DestroyRenderer(window->renderer);
    SDL_DestroyWindow(window->window);
    free(window);
}

void *lx_host_window_data(LxHostWindow *window)
{
    return window ? window->data : NULL;
}

void lx_host_window_constrain(LxHostWindow *window, int x, int *y, int width, int height)
{
    const SDL_Rect area = {x, *y, width, height};
    SDL_Rect usable;
    SDL_DisplayID display = SDL_GetDisplayForRect(&area);
    if (!window || window->fullscreen || !display || !SDL_GetDisplayUsableBounds(display, &usable))
        return;
    int title = 0;
    if (!SDL_GetWindowBordersSize(window->window, &title, NULL, NULL, NULL))
        title = 0;
    if (*y < usable.y + title)
        *y = usable.y + title;
}

int lx_host_window_position(LxHostWindow *window, int x, int y, int width, int height)
{
    lx_sdl_prepare_renderer();
    if (width <= 0 || height <= 0 || !SDL_FlushRenderer(window->renderer))
        return 0;
    if (!window->fullscreen &&
        !(SDL_SetWindowPosition(window->window, x, y) &&
          SDL_SetWindowSize(window->window, width, height) && SDL_SyncWindow(window->window)))
        return 0;
    if (!activateWindowSurface(window))
        return 0;
    if (width != window->width || height != window->height)
    {
        SDL_Texture *texture = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET, width, height);
        if (!texture)
        {
            SDL_SetRenderTarget(window->renderer, window->texture);
            return 0;
        }
        if (!SDL_SetRenderTarget(window->renderer, texture))
        {
            SDL_DestroyTexture(texture);
            SDL_SetRenderTarget(window->renderer, window->texture);
            return 0;
        }
        SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, OPAQUE);
        SDL_RenderClear(window->renderer);
        SDL_DestroyTexture(window->texture);
        window->texture = texture;
        window->dirty = 1;
        window->width = width;
        window->height = height;
    }
    window->x = x;
    window->y = y;
    if (window->fullscreen)
    {
        window->windowed_x = x;
        window->windowed_y = y;
        return 1;
    }
    return 1;
}

int lx_host_window_fullscreen(LxHostWindow *window, int enabled)
{
    lx_sdl_prepare_renderer();
    if (!window)
        return 0;
    enabled = enabled != 0;
    if (window->fullscreen == enabled)
        return 1;
    /* A surface change must not redirect queued canvas drawing to the window. */
    if (!SDL_FlushRenderer(window->renderer) || !SDL_SyncWindow(window->window))
        return 0;
    if (enabled)
        SDL_GetWindowPosition(window->window, &window->windowed_x, &window->windowed_y);
    if (!SDL_SetWindowFullscreen(window->window, enabled))
        return 0;
    window->fullscreen = enabled;
    window->restoring = !enabled;
    window->dirty = 1;
    if (!SDL_SyncWindow(window->window))
        return 0;
    if (!enabled &&
        !(SDL_SetWindowPosition(window->window, window->windowed_x, window->windowed_y) &&
          SDL_SetWindowSize(window->window, window->width, window->height) &&
          SDL_SyncWindow(window->window)))
        return 0;
    return activateWindowSurface(window);
}

int lx_host_window_is_fullscreen(LxHostWindow *window)
{
    return window && window->fullscreen;
}

void lx_host_window_show(LxHostWindow *window)
{
    lx_sdl_prepare_renderer();
    if (!SDL_FlushRenderer(window->renderer))
        return;
    if (!SDL_ShowWindow(window->window) || !SDL_SyncWindow(window->window) ||
        !activateWindowSurface(window))
        return;
    lx_desktop_visible(window->window, 1);
    if (!window->startup_checked)
    {
        const char *fullscreen = getenv("TWOINE_FULLSCREEN");
        window->startup_checked = 1;
        if (fullscreen && SDL_strcmp(fullscreen, "0") && *fullscreen)
            lx_host_window_fullscreen(window, 1);
    }
}

void lx_host_window_hide(LxHostWindow *window)
{
    lx_sdl_prepare_renderer();
    if (SDL_HideWindow(window->window))
        lx_desktop_visible(window->window, 0);
}

int lx_host_window_minimize(LxHostWindow *window, int minimized)
{
    lx_sdl_prepare_renderer();
    return window && SDL_FlushRenderer(window->renderer) &&
           (minimized ? SDL_MinimizeWindow(window->window) : SDL_RestoreWindow(window->window)) &&
           SDL_SyncWindow(window->window) && activateWindowSurface(window);
}

int lx_host_window_title(LxHostWindow *window, const char *title)
{
    lx_sdl_prepare_renderer();
    return window && SDL_SetWindowTitle(window->window, title);
}

int lx_host_window_border(LxHostWindow *window, int bordered)
{
    lx_sdl_prepare_renderer();
    return window && SDL_FlushRenderer(window->renderer) &&
           SDL_SetWindowBordered(window->window, bordered != 0) && SDL_SyncWindow(window->window) &&
           activateWindowSurface(window);
}

int lx_host_window_focus(LxHostWindow *window)
{
    lx_sdl_prepare_renderer();
    return window && SDL_RaiseWindow(window->window);
}

int lx_host_window_resizable(LxHostWindow *window, int resizable)
{
    lx_sdl_prepare_renderer();
    return window && SDL_FlushRenderer(window->renderer) &&
           SDL_SetWindowResizable(window->window, resizable != 0) &&
           SDL_SyncWindow(window->window) && activateWindowSurface(window);
}

int lx_host_window_is_resizable(LxHostWindow *window)
{
    return window && (SDL_GetWindowFlags(window->window) & SDL_WINDOW_RESIZABLE) != 0;
}
