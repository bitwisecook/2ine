/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host windows and render targets contain no OS/2 window/message state. */
#include "hal/video.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
struct LxHostWindow
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    void *data;
};
int lx_host_video_initialize(int *width, int *height)
{
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
        return 0;
    SDL_Rect bounds;
    if (!SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds))
        return 0;
    *width = bounds.w;
    *height = bounds.h;
    return 1;
}
void lx_host_video_quit(void)
{
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
const char *lx_host_video_error(void)
{
    return SDL_GetError();
}
uint64_t lx_host_video_ticks_ms(void)
{
    return SDL_GetTicks();
}
LxHostWindow *lx_host_window_create(const char *title, int x, int y, int width, int height,
                                    void *data)
{
    LxHostWindow *window = calloc(1, sizeof *window);
    if (!window)
        return NULL;
    window->data = data;
    window->window = SDL_CreateWindow(title ? title : "", width, height,
                                      SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
    if (!window->window)
        goto failed;
    SDL_SetWindowPosition(window->window, x, y);
    if (!SDL_SetPointerProperty(SDL_GetWindowProperties(window->window), "2ine.window", window))
        goto failed;
    window->renderer = SDL_CreateRenderer(window->window, NULL);
    if (!window->renderer)
        goto failed;
    window->texture = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET, width, height);
    if (!window->texture || !SDL_SetRenderTarget(window->renderer, window->texture))
        goto failed;
    return window;
failed:
    lx_host_window_destroy(window);
    return NULL;
}
void lx_host_window_destroy(LxHostWindow *window)
{
    if (!window)
        return;
    SDL_DestroyTexture(window->texture);
    SDL_DestroyRenderer(window->renderer);
    SDL_DestroyWindow(window->window);
    free(window);
}
void *lx_host_window_data(LxHostWindow *window)
{
    return window ? window->data : NULL;
}
void lx_host_window_position(LxHostWindow *window, int x, int y, int width, int height)
{
    SDL_SetWindowPosition(window->window, x, y);
    SDL_SetWindowSize(window->window, width, height);
}
void lx_host_window_show(LxHostWindow *window)
{
    SDL_ShowWindow(window->window);
}
int lx_host_window_present(LxHostWindow *window)
{
    if (!SDL_SetRenderTarget(window->renderer, NULL))
        return 0;
    int ok = SDL_RenderTexture(window->renderer, window->texture, NULL, NULL) &&
             SDL_RenderPresent(window->renderer);
    return SDL_SetRenderTarget(window->renderer, window->texture) && ok;
}
int lx_host_window_fill(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                        uint8_t red, uint8_t green, uint8_t blue)
{
    SDL_Rect clipping = {clip->x, clip->y, clip->w, clip->h};
    SDL_FRect area = {(float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h};
    int ok = SDL_SetRenderClipRect(window->renderer, &clipping) &&
             SDL_SetRenderDrawColor(window->renderer, red, green, blue, 255) &&
             SDL_RenderFillRect(window->renderer, &area);
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}
int lx_host_window_pixel(LxHostWindow *window, int x, int y, uint8_t *red, uint8_t *green,
                         uint8_t *blue)
{
    SDL_Surface *surface = SDL_RenderReadPixels(window->renderer, NULL);
    if (!surface)
        return 0;
    Uint8 alpha;
    int ok = SDL_ReadSurfacePixel(surface, x, y, red, green, blue, &alpha);
    SDL_DestroySurface(surface);
    return ok;
}
static void event_from_sdl(const SDL_Event *input, LxHostEvent *event)
{
    event->timestamp = input->common.timestamp / SDL_NS_PER_MS;
    event->type = LX_HOST_EVENT_OTHER;
    event->window_data = NULL;
    if (input->type == SDL_EVENT_QUIT)
        event->type = LX_HOST_EVENT_QUIT;
    else if (input->type == SDL_EVENT_WINDOW_SHOWN || input->type == SDL_EVENT_WINDOW_EXPOSED)
    {
        SDL_Window *sdl = SDL_GetWindowFromID(input->window.windowID);
        LxHostWindow *window =
            sdl ? SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), "2ine.window", NULL) : NULL;
        event->window_data = window ? window->data : NULL;
        event->type =
            input->type == SDL_EVENT_WINDOW_SHOWN ? LX_HOST_EVENT_SHOWN : LX_HOST_EVENT_EXPOSED;
    }
}
int lx_host_video_poll(LxHostEvent *event)
{
    SDL_Event input;
    if (!SDL_PollEvent(&input))
        return 0;
    event_from_sdl(&input, event);
    return 1;
}
int lx_host_video_wait(LxHostEvent *event)
{
    SDL_Event input;
    if (!SDL_WaitEvent(&input))
        return 0;
    event_from_sdl(&input, event);
    return 1;
}
int lx_host_video_push(const LxHostEvent *event)
{
    if (event->type != LX_HOST_EVENT_QUIT)
        return 0;
    SDL_Event output = {0};
    output.type = SDL_EVENT_QUIT;
    return SDL_PushEvent(&output);
}
void lx_host_mouse_position(int *x, int *y)
{
    float a, b;
    SDL_GetGlobalMouseState(&a, &b);
    *x = (int)a;
    *y = (int)b;
}
