/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host pointer shapes, visibility and mouse position. */
#include "host/input.h"
#include "host/sdl/window/internal.h"
#include "host/sync.h"
#include "host/core/desktop/session_hooks.h"
#include <stdlib.h>

/* The largest colour pointer accepted, in pixels per side. */
#define POINTER_MAX_EXTENT 256
#define ARGB_BYTES 4

struct LxHostPointer
{
    struct LxHostPointer *next;
    SDL_Cursor *cursor;
    LxHostPointerShape shape;
    uint32_t *pixels;
    unsigned width, height, hot_x, hot_y;
};

static LxHostPointer *Pointers;
static uint64_t PointerGeneration;
static const SDL_SystemCursor PointerShapes[] = {
    SDL_SYSTEM_CURSOR_DEFAULT,     SDL_SYSTEM_CURSOR_TEXT,      SDL_SYSTEM_CURSOR_WAIT,
    SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_MOVE,      SDL_SYSTEM_CURSOR_NWSE_RESIZE,
    SDL_SYSTEM_CURSOR_NESW_RESIZE, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE,
    SDL_SYSTEM_CURSOR_NOT_ALLOWED};

static int native_pointer_thread(void)
{
    const char *driver = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetCurrentVideoDriver() : NULL;
    return !driver || SDL_strcmp(driver, "cocoa") || lx_desktop_native_main_thread() ||
           SDL_SetError("Native macOS cursor selection requires the main thread");
}

void lx_sdl_pointers_invalidate(void)
{
    PointerGeneration++;
    for (LxHostPointer *p = Pointers; p; p = p->next)
        p->cursor = NULL; /* SDL_VideoQuit freed the native cursor objects. */
}

static int cursor_visible(int requested)
{
    LxHostWindow *window = lx_sdl_focused_window();
    if (window && window->capture_control)
        requested = !window->captured;
    else if (!window && LxSdlControlledWindows)
        requested = 1;
    return requested ? SDL_ShowCursor() : SDL_HideCursor();
}

LxHostPointer *lx_host_pointer_create(LxHostPointerShape shape)
{
    if ((unsigned)shape >= sizeof PointerShapes / sizeof *PointerShapes)
        return NULL;
    LxHostPointer *pointer = calloc(1, sizeof *pointer);
    if (pointer)
    {
        pointer->shape = shape;
        lx_sdl_video_lock();
        pointer->next = Pointers;
        Pointers = pointer;
        lx_sdl_video_unlock();
    }
    return pointer;
}

typedef struct DeferredCursor
{
    SDL_Cursor *cursor;
    uint64_t generation;
} DeferredCursor;

static void destroy_deferred_cursor(void *data)
{
    DeferredCursor *item = data;
    lx_sdl_video_lock();
    if (item->generation == PointerGeneration)
        SDL_DestroyCursor(item->cursor);
    lx_sdl_video_unlock();
    free(item);
}

void lx_host_pointer_destroy(LxHostPointer *pointer)
{
    if (!pointer)
        return;
    DeferredCursor *deferred = NULL;
    lx_sdl_video_lock();
    LxHostPointer **link = &Pointers;
    while (*link && *link != pointer)
        link = &(*link)->next;
    if (*link)
        *link = pointer->next;
    if (pointer->cursor)
    {
        if (native_pointer_thread())
            SDL_DestroyCursor(pointer->cursor);
        else
        {
            deferred = malloc(sizeof *deferred);
            if (deferred)
                *deferred = (DeferredCursor){pointer->cursor, PointerGeneration};
            /* On allocation/queue failure SDL still owns and frees it at video shutdown. */
        }
    }
    lx_sdl_video_unlock();
    if (deferred && !SDL_RunOnMainThread(destroy_deferred_cursor, deferred, false))
        free(deferred);
    free(pointer->pixels);
    free(pointer);
}

LxHostPointer *lx_host_pointer_create_color(const uint32_t *pixels, unsigned width, unsigned height,
                                            unsigned hot_x, unsigned hot_y)
{
    if (!pixels || !width || !height || width > POINTER_MAX_EXTENT || height > POINTER_MAX_EXTENT ||
        hot_x >= width || hot_y >= height)
        return NULL;
    LxHostPointer *pointer = lx_host_pointer_create(LX_HOST_POINTER_ARROW);
    if (!pointer)
        return NULL;
    pointer->pixels = malloc((size_t)width * height * sizeof *pixels);
    if (!pointer->pixels)
    {
        lx_host_pointer_destroy(pointer);
        return NULL;
    }
    SDL_memcpy(pointer->pixels, pixels, (size_t)width * height * sizeof *pixels);
    pointer->width = width;
    pointer->height = height;
    pointer->hot_x = hot_x;
    pointer->hot_y = hot_y;
    return pointer;
}

int lx_host_pointer_set(LxHostPointer *pointer)
{
    lx_sdl_video_lock();
    int result = 0;
    if (!native_pointer_thread())
        goto done;
    if (!pointer)
    {
        result = cursor_visible(0);
        goto done;
    }
    if (!SDL_WasInit(SDL_INIT_VIDEO))
    {
        SDL_SetError("Initialize the display before selecting a cursor");
        goto done;
    }
    if (!pointer->cursor)
    {
        if (pointer->pixels)
        {
            SDL_Surface *surface = SDL_CreateSurfaceFrom((int)pointer->width, (int)pointer->height,
                                                         SDL_PIXELFORMAT_ARGB8888, pointer->pixels,
                                                         (int)pointer->width * ARGB_BYTES);
            if (surface)
            {
                pointer->cursor =
                    SDL_CreateColorCursor(surface, (int)pointer->hot_x, (int)pointer->hot_y);
                SDL_DestroySurface(surface);
            }
        }
        else
            pointer->cursor = SDL_CreateSystemCursor(PointerShapes[pointer->shape]);
    }
    result = pointer->cursor && SDL_SetCursor(pointer->cursor);
done:
    lx_sdl_video_unlock();
    return result;
}

void lx_host_mouse_position(int *x, int *y)
{
    float a, b;
    SDL_GetGlobalMouseState(&a, &b);
    *x = (int)a;
    *y = (int)b;
    SDL_Window *sdl = SDL_GetKeyboardFocus();
    LxHostWindow *window = lx_sdl_window_from(sdl);
    if (window && window->fullscreen)
    {
        int origin_x, origin_y, width, height;
        if (SDL_GetWindowPosition(sdl, &origin_x, &origin_y) &&
            SDL_GetWindowSize(sdl, &width, &height) && width > 0 && height > 0)
        {
            *x = window->x + (int)((a - origin_x) * window->width / width);
            *y = window->y + (int)((b - origin_y) * window->height / height);
        }
    }
}

int lx_host_mouse_warp(int x, int y)
{
    SDL_Window *sdl = SDL_GetKeyboardFocus();
    LxHostWindow *window = lx_sdl_window_from(sdl);
    if ((window && window->capture_control && !window->captured) ||
        (!window && LxSdlControlledWindows))
        return 1; /* A released guest cannot pull the desktop pointer back. */
    if (window && window->fullscreen)
    {
        int origin_x, origin_y, width, height;
        if (SDL_GetWindowPosition(sdl, &origin_x, &origin_y) &&
            SDL_GetWindowSize(sdl, &width, &height))
        {
            x = origin_x + (int)((int64_t)(x - window->x) * width / window->width);
            y = origin_y + (int)((int64_t)(y - window->y) * height / window->height);
        }
    }
    return SDL_WarpMouseGlobal((float)x, (float)y);
}

int lx_host_pointer_show(int show)
{
    return cursor_visible(show);
}
