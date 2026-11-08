/* Public domain. No rights reserved. Wholly AI-authored. */
/* The SDL display: initialization, shutdown, errors and the shared clock. */
#include "host/sdl/window/internal.h"
#include "host/sync.h"
#include "host/core/desktop/session_hooks.h"
#include <stdlib.h>

/* SDL user events: the first wakes a waiting event loop, the second resets
   input after a mouse-capture change. */
#define LX_SDL_USER_EVENTS 2

static void (*GraphicsRelease)(void);
Uint32 LxSdlCaptureResetEvent;
Uint32 LxSdlWakeEvent;
static LxHostOnce VideoOnce = LX_HOST_ONCE_INIT;
static LxHostMutex *VideoLock;

static void initializeVideoLock(void)
{
    VideoLock = lx_host_mutex_create(0, 0);
    if (!VideoLock)
        abort();
}

void lx_sdl_video_lock(void)
{
    lx_host_once(&VideoOnce, initializeVideoLock);
    lx_host_mutex_lock(VideoLock, UINT32_MAX);
}

void lx_sdl_video_unlock(void)
{
    lx_host_mutex_unlock(VideoLock);
}

void lx_host_video_set_graphics_release(void (*release)(void))
{
    GraphicsRelease = release;
}

void lx_sdl_prepare_renderer(void)
{
    if (GraphicsRelease)
        GraphicsRelease();
}

int lx_host_video_initialize(int *width, int *height)
{
    lx_sdl_video_lock();
    int result = 0;
    const char *requested = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !lx_desktop_native_main_thread() &&
        (!requested || !SDL_strcmp(requested, "cocoa")))
    {
        SDL_SetError(
            "The macOS display must first be initialized on the main thread; a background OS/2 thread requested it");
        goto done;
    }
    /* Popup-to-document focus changes must retain the dismissing click. */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    lx_desktop_prepare();
    /* Every client owns one SDL subsystem reference, including PM worker threads. */
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        goto done;
    if (!lx_desktop_initialized())
        goto failed;
    if (!LxSdlWakeEvent)
    {
        LxSdlWakeEvent = SDL_RegisterEvents(LX_SDL_USER_EVENTS);
        if (!LxSdlWakeEvent)
            goto failed;
        LxSdlCaptureResetEvent = LxSdlWakeEvent + 1;
    }
    SDL_Rect bounds;
    if (!SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds))
        goto failed;
    *width = bounds.w;
    *height = bounds.h;
    result = 1;
    goto done;
failed:
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
    {
        lx_sdl_pointers_invalidate();
        LxSdlCaptureResetEvent = 0;
        LxSdlWakeEvent = 0;
    }
done:
    lx_sdl_video_unlock();
    return result;
}

void lx_host_video_quit(void)
{
    lx_sdl_video_lock();
    lx_sdl_prepare_renderer();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
    {
        lx_sdl_pointers_invalidate();
        LxSdlCaptureResetEvent = 0;
        LxSdlWakeEvent = 0;
    }
    lx_sdl_video_unlock();
}

int lx_host_video_release_graphics(void)
{
    return !SDL_GL_GetCurrentContext() || SDL_GL_MakeCurrent(NULL, NULL);
}

const char *lx_host_video_error(void)
{
    return SDL_GetError();
}

uint64_t lx_host_video_ticks_ms(void)
{
    return SDL_GetTicks();
}
