/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host windows and render targets contain no OS/2 window/message state. */
#include "hal/video.h"
#include "window_chrome.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
static void (*GraphicsRelease)(void);
static Uint32 CaptureResetEvent;
static Uint32 WakeEvent;
static unsigned ControlledWindows;
static int cursor_visible(int requested);
void lx_host_video_set_graphics_release(void (*release)(void))
{
    GraphicsRelease = release;
}
static void prepareRenderer(void)
{
    if (GraphicsRelease)
        GraphicsRelease();
}
struct LxHostPointer
{
    SDL_Cursor *cursor;
};
LxHostPointer *lx_host_pointer_create(LxHostPointerShape shape)
{
    static const SDL_SystemCursor shapes[] = {
        SDL_SYSTEM_CURSOR_DEFAULT,     SDL_SYSTEM_CURSOR_TEXT,      SDL_SYSTEM_CURSOR_WAIT,
        SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_MOVE,      SDL_SYSTEM_CURSOR_NWSE_RESIZE,
        SDL_SYSTEM_CURSOR_NESW_RESIZE, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE,
        SDL_SYSTEM_CURSOR_NOT_ALLOWED};
    if ((unsigned)shape >= sizeof shapes / sizeof *shapes)
        return NULL;
    LxHostPointer *pointer = calloc(1, sizeof *pointer);
    if (pointer)
    {
        pointer->cursor = SDL_CreateSystemCursor(shapes[shape]);
        if (!pointer->cursor)
        {
            free(pointer);
            return NULL;
        }
    }
    return pointer;
}
void lx_host_pointer_destroy(LxHostPointer *pointer)
{
    if (pointer)
    {
        SDL_DestroyCursor(pointer->cursor);
        free(pointer);
    }
}
int lx_host_pointer_set(LxHostPointer *pointer)
{
    return pointer ? SDL_SetCursor(pointer->cursor) && cursor_visible(1) : cursor_visible(0);
}
struct LxHostWindow
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
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
static LxHostWindow *focused_window(void)
{
    SDL_Window *sdl = SDL_GetKeyboardFocus();
    return sdl ? SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), "2ine.window", NULL) : NULL;
}
static int cursor_visible(int requested)
{
    LxHostWindow *window = focused_window();
    if (window && window->capture_control)
        requested = !window->captured;
    else if (!window && ControlledWindows)
        requested = 1;
    return requested ? SDL_ShowCursor() : SDL_HideCursor();
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
    reset.type = CaptureResetEvent;
    reset.user.windowID = SDL_GetWindowID(window->window);
    SDL_PushEvent(&reset);
    return 1;
}
static void toggle_capture(void *opaque)
{
    LxHostWindow *window = opaque;
    lx_host_window_set_capture(window, !window->captured);
}
int lx_host_video_initialize(int *width, int *height)
{
    /* Popup-to-document focus changes must retain the dismissing click. */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
        return 0;
    if (!WakeEvent)
    {
        WakeEvent = SDL_RegisterEvents(2);
        if (!WakeEvent)
            return 0;
        CaptureResetEvent = WakeEvent + 1;
    }
    SDL_Rect bounds;
    if (!SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds))
        return 0;
    *width = bounds.w;
    *height = bounds.h;
    return 1;
}
void lx_host_video_quit(void)
{
    prepareRenderer();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    CaptureResetEvent = 0;
    WakeEvent = 0;
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
LxHostWindow *lx_host_window_create(const char *title, int x, int y, int width, int height,
                                    void *data)
{
    prepareRenderer();
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
    if (!SDL_SetPointerProperty(SDL_GetWindowProperties(window->window), "2ine.window", window))
        goto failed;
    const char *capture = getenv("TWOINE_MOUSE_CAPTURE");
    window->capture_control = !capture || !*capture ? 0 : SDL_strcmp(capture, "0") ? 1 : 2;
    if (window->capture_control)
    {
        ControlledWindows++;
        if (window->capture_control == 1)
            window->chrome = lx_window_chrome_create(window->window, toggle_capture, window);
    }
    window->renderer = SDL_CreateRenderer(window->window, NULL);
    if (!window->renderer)
        goto failed;
    window->texture = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET, width, height);
    if (!window->texture || !SDL_SetRenderTarget(window->renderer, window->texture))
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
    prepareRenderer();
    if (!window)
        return;
    if (window->capture_control)
    {
        lx_host_window_set_capture(window, 0);
        lx_window_chrome_destroy(window->chrome);
        ControlledWindows--;
    }
    SDL_DestroyTexture(window->texture);
    SDL_DestroyRenderer(window->renderer);
    SDL_DestroyWindow(window->window);
    free(window);
}
void *lx_host_window_data(LxHostWindow *window)
{
    return window ? window->data : NULL;
}
int lx_host_window_position(LxHostWindow *window, int x, int y, int width, int height)
{
    prepareRenderer();
    if (width <= 0 || height <= 0)
        return 0;
    if (width != window->width || height != window->height)
    {
        SDL_Texture *texture = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET, width, height);
        if (!texture)
            return 0;
        if (!SDL_SetRenderTarget(window->renderer, texture))
        {
            SDL_DestroyTexture(texture);
            return 0;
        }
        SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, 255);
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
    return SDL_SetWindowPosition(window->window, x, y) &&
           SDL_SetWindowSize(window->window, width, height);
}
int lx_host_window_fullscreen(LxHostWindow *window, int enabled)
{
    prepareRenderer();
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
    if (!enabled)
        return SDL_SetWindowPosition(window->window, window->windowed_x, window->windowed_y) &&
               SDL_SetWindowSize(window->window, window->width, window->height) &&
               SDL_SyncWindow(window->window);
    return 1;
}
int lx_host_window_is_fullscreen(LxHostWindow *window)
{
    return window && window->fullscreen;
}
void lx_host_window_show(LxHostWindow *window)
{
    prepareRenderer();
    if (!SDL_FlushRenderer(window->renderer))
        return;
    SDL_ShowWindow(window->window);
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
    prepareRenderer();
    SDL_HideWindow(window->window);
}
int lx_host_window_title(LxHostWindow *window, const char *title)
{
    prepareRenderer();
    return window && SDL_SetWindowTitle(window->window, title);
}
int lx_host_window_border(LxHostWindow *window, int bordered)
{
    prepareRenderer();
    return window && SDL_SetWindowBordered(window->window, bordered != 0);
}
int lx_host_window_focus(LxHostWindow *window)
{
    prepareRenderer();
    return window && SDL_RaiseWindow(window->window);
}
int lx_host_window_mask(LxHostWindow *window, const LxHostRect *clip, int x, int y, int width,
                        int height, const uint8_t *coverage, uint8_t red, uint8_t green,
                        uint8_t blue)
{
    prepareRenderer();
    if (!window || !clip || width < 0 || height < 0 || width > 4096 || height > 4096 ||
        (width && height && !coverage))
        return 0;
    if (!width || !height)
        return 1;
    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return 0;
    for (int row = 0; row < height; row++)
    {
        uint8_t *pixels = (uint8_t *)surface->pixels + row * surface->pitch;
        for (int column = 0; column < width; column++)
        {
            pixels[column * 4] = red;
            pixels[column * 4 + 1] = green;
            pixels[column * 4 + 2] = blue;
            pixels[column * 4 + 3] = coverage[(size_t)row * width + column];
        }
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(window->renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture)
        return 0;
    SDL_Rect clipping = {clip->x, clip->y, clip->w, clip->h};
    SDL_FRect destination = {(float)x, (float)y, (float)width, (float)height};
    int ok = SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) &&
             SDL_SetRenderClipRect(window->renderer, &clipping) &&
             SDL_RenderTexture(window->renderer, texture, NULL, &destination);
    SDL_DestroyTexture(texture);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}
int lx_host_window_present(LxHostWindow *window)
{
    prepareRenderer();
    if (!window->dirty)
        return 1;
    if (!SDL_SetRenderTarget(window->renderer, NULL))
        return 0;
    int ok = SDL_RenderTexture(window->renderer, window->texture, NULL, NULL) &&
             SDL_RenderPresent(window->renderer);
    int restored = SDL_SetRenderTarget(window->renderer, window->texture);
    if (ok && restored)
        window->dirty = 0;
    return restored && ok;
}
int lx_host_window_fill(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                        uint8_t red, uint8_t green, uint8_t blue)
{
    prepareRenderer();
    SDL_Rect clipping = {clip->x, clip->y, clip->w, clip->h};
    SDL_FRect area = {(float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h};
    int ok = SDL_SetRenderClipRect(window->renderer, &clipping) &&
             SDL_SetRenderDrawColor(window->renderer, red, green, blue, 255) &&
             SDL_RenderFillRect(window->renderer, &area);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}
int lx_host_window_pixel(LxHostWindow *window, int x, int y, uint8_t *red, uint8_t *green,
                         uint8_t *blue)
{
    prepareRenderer();
    SDL_Surface *surface = SDL_RenderReadPixels(window->renderer, NULL);
    if (!surface)
        return 0;
    Uint8 alpha;
    int ok = SDL_ReadSurfacePixel(surface, x, y, red, green, blue, &alpha);
    SDL_DestroySurface(surface);
    return ok;
}
static unsigned physical_key(SDL_Scancode key)
{
    static const uint8_t letters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                        0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                        0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z)
        return letters[key - SDL_SCANCODE_A];
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0)
        return 2 + key - SDL_SCANCODE_1;
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F10)
        return 0x3b + key - SDL_SCANCODE_F1;
    switch (key)
    {
        case SDL_SCANCODE_ESCAPE:
            return 1;
        case SDL_SCANCODE_BACKSPACE:
            return 0x0e;
        case SDL_SCANCODE_TAB:
            return 0x0f;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            return 0x1c;
        case SDL_SCANCODE_SPACE:
            return 0x39;
        case SDL_SCANCODE_MINUS:
            return 0x0c;
        case SDL_SCANCODE_EQUALS:
            return 0x0d;
        case SDL_SCANCODE_LEFTBRACKET:
            return 0x1a;
        case SDL_SCANCODE_RIGHTBRACKET:
            return 0x1b;
        case SDL_SCANCODE_BACKSLASH:
            return 0x2b;
        case SDL_SCANCODE_SEMICOLON:
            return 0x27;
        case SDL_SCANCODE_APOSTROPHE:
            return 0x28;
        case SDL_SCANCODE_GRAVE:
            return 0x29;
        case SDL_SCANCODE_COMMA:
            return 0x33;
        case SDL_SCANCODE_PERIOD:
            return 0x34;
        case SDL_SCANCODE_SLASH:
            return 0x35;
        case SDL_SCANCODE_LSHIFT:
            return 0x2a;
        case SDL_SCANCODE_RSHIFT:
            return 0x36;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            return 0x1d;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
            return 0x38;
        case SDL_SCANCODE_CAPSLOCK:
            return 0x3a;
        case SDL_SCANCODE_F11:
            return 0x57;
        case SDL_SCANCODE_F12:
            return 0x58;
        case SDL_SCANCODE_HOME:
            return 0x47;
        case SDL_SCANCODE_UP:
            return 0x48;
        case SDL_SCANCODE_PAGEUP:
            return 0x49;
        case SDL_SCANCODE_LEFT:
            return 0x4b;
        case SDL_SCANCODE_RIGHT:
            return 0x4d;
        case SDL_SCANCODE_END:
            return 0x4f;
        case SDL_SCANCODE_DOWN:
            return 0x50;
        case SDL_SCANCODE_PAGEDOWN:
            return 0x51;
        case SDL_SCANCODE_INSERT:
            return 0x52;
        case SDL_SCANCODE_DELETE:
            return 0x53;
        default:
            return 0;
    }
}
static void event_from_sdl(const SDL_Event *input, LxHostEvent *event)
{
    SDL_memset(event, 0, sizeof *event);
    event->timestamp = input->common.timestamp / SDL_NS_PER_MS;
    SDL_WindowID id = 0;
    if (input->type == CaptureResetEvent)
    {
        event->type = LX_HOST_EVENT_INPUT_RESET;
        id = input->user.windowID;
    }
    switch (input->type)
    {
        case SDL_EVENT_QUIT:
            event->type = LX_HOST_EVENT_QUIT;
            break;
        case SDL_EVENT_WINDOW_SHOWN:
            event->type = LX_HOST_EVENT_SHOWN;
            id = input->window.windowID;
            break;
        case SDL_EVENT_WINDOW_MOVED:
            id = input->window.windowID;
            {
                SDL_Window *moved = SDL_GetWindowFromID(id);
                LxHostWindow *window = moved
                                           ? SDL_GetPointerProperty(SDL_GetWindowProperties(moved),
                                                                    "2ine.window", NULL)
                                           : NULL;
                if (window)
                    window->dirty = 1;
            }
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            event->type = LX_HOST_EVENT_EXPOSED;
            id = input->window.windowID;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            event->type = LX_HOST_EVENT_CLOSE;
            id = input->window.windowID;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            event->type = input->type == SDL_EVENT_WINDOW_FOCUS_GAINED ? LX_HOST_EVENT_FOCUS_GAINED
                                                                       : LX_HOST_EVENT_FOCUS_LOST;
            id = input->window.windowID;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            event->type =
                input->type == SDL_EVENT_KEY_DOWN ? LX_HOST_EVENT_KEY_DOWN : LX_HOST_EVENT_KEY_UP;
            id = input->key.windowID;
            event->modifiers = (input->key.mod & SDL_KMOD_SHIFT ? LX_HOST_MOD_SHIFT : 0) |
                               (input->key.mod & SDL_KMOD_CTRL ? LX_HOST_MOD_CTRL : 0) |
                               (input->key.mod & SDL_KMOD_ALT ? LX_HOST_MOD_ALT : 0);
            event->repeat = input->key.repeat;
            event->scancode = physical_key(input->key.scancode);
            event->key = input->key.key < 128 ? input->key.key : 0;
            if (input->key.key >= SDLK_F1 && input->key.key <= SDLK_F12)
                event->key = LX_HOST_KEY_F1 + (input->key.key - SDLK_F1);
            switch (input->key.key)
            {
                case SDLK_BACKSPACE:
                    event->key = LX_HOST_KEY_BACKSPACE;
                    break;
                case SDLK_TAB:
                    event->key = LX_HOST_KEY_TAB;
                    break;
                case SDLK_RETURN:
                    event->key = LX_HOST_KEY_RETURN;
                    break;
                case SDLK_KP_ENTER:
                    event->key = LX_HOST_KEY_KP_ENTER;
                    break;
                case SDLK_ESCAPE:
                    event->key = LX_HOST_KEY_ESCAPE;
                    break;
                case SDLK_HOME:
                    event->key = LX_HOST_KEY_HOME;
                    break;
                case SDLK_END:
                    event->key = LX_HOST_KEY_END;
                    break;
                case SDLK_LEFT:
                    event->key = LX_HOST_KEY_LEFT;
                    break;
                case SDLK_RIGHT:
                    event->key = LX_HOST_KEY_RIGHT;
                    break;
                case SDLK_UP:
                    event->key = LX_HOST_KEY_UP;
                    break;
                case SDLK_DOWN:
                    event->key = LX_HOST_KEY_DOWN;
                    break;
                case SDLK_DELETE:
                    event->key = LX_HOST_KEY_DELETE;
                    break;
                case SDLK_INSERT:
                    event->key = LX_HOST_KEY_INSERT;
                    break;
                case SDLK_PAGEUP:
                    event->key = LX_HOST_KEY_PAGEUP;
                    break;
                case SDLK_PAGEDOWN:
                    event->key = LX_HOST_KEY_PAGEDOWN;
                    break;
                case SDLK_LSHIFT:
                case SDLK_RSHIFT:
                    event->key = LX_HOST_KEY_SHIFT;
                    break;
                case SDLK_LCTRL:
                case SDLK_RCTRL:
                    event->key = LX_HOST_KEY_CTRL;
                    break;
                case SDLK_LALT:
                case SDLK_RALT:
                    event->key = LX_HOST_KEY_ALT;
                    break;
                default:
                    break;
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            event->type = LX_HOST_EVENT_TEXT;
            id = input->text.windowID;
            SDL_strlcpy(event->text, input->text.text, sizeof event->text);
            SDL_Keymod modifiers = SDL_GetModState();
            event->modifiers = (modifiers & SDL_KMOD_SHIFT ? LX_HOST_MOD_SHIFT : 0) |
                               (modifiers & SDL_KMOD_CTRL ? LX_HOST_MOD_CTRL : 0) |
                               (modifiers & SDL_KMOD_ALT ? LX_HOST_MOD_ALT : 0);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            event->type = LX_HOST_EVENT_RESIZED;
            id = input->window.windowID;
            event->width = input->window.data1;
            event->height = input->window.data2;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            event->type = LX_HOST_EVENT_MOTION;
            id = input->motion.windowID;
            event->x = (int)input->motion.x;
            event->y = (int)input->motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            event->type = input->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? LX_HOST_EVENT_BUTTON_DOWN
                                                                     : LX_HOST_EVENT_BUTTON_UP;
            id = input->button.windowID;
            event->x = (int)input->button.x;
            event->y = (int)input->button.y;
            event->button = input->button.button == SDL_BUTTON_LEFT     ? 1
                            : input->button.button == SDL_BUTTON_RIGHT  ? 2
                            : input->button.button == SDL_BUTTON_MIDDLE ? 3
                                                                        : 0;
            break;
        default:
            break;
    }
    SDL_Window *sdl = id ? SDL_GetWindowFromID(id) : NULL;
    LxHostWindow *window =
        sdl ? SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), "2ine.window", NULL) : NULL;
    if (window)
    {
        if (window->capture_control == 1)
        {
            unsigned chord = LX_HOST_MOD_CTRL | LX_HOST_MOD_ALT;
            int modifier = event->key == LX_HOST_KEY_CTRL || event->key == LX_HOST_KEY_ALT;
            if (modifier && event->type == LX_HOST_EVENT_KEY_DOWN &&
                (event->modifiers & chord) == chord && !window->capture_chord)
            {
                window->capture_chord = 1;
                toggle_capture(window);
                event->type = LX_HOST_EVENT_OTHER;
            }
            else if (modifier && window->capture_chord)
            {
                if (!(event->modifiers & chord))
                    window->capture_chord = 0;
                event->type = LX_HOST_EVENT_OTHER;
            }
            if (event->type == LX_HOST_EVENT_FOCUS_LOST)
            {
                lx_host_window_set_capture(window, 0);
                window->capture_chord = window->suppress_click = 0;
            }
            if (event->type == LX_HOST_EVENT_BUTTON_UP && window->suppress_click &&
                event->button == 1)
            {
                window->suppress_click = 0;
                event->type = LX_HOST_EVENT_OTHER;
            }
            if (!window->captured)
            {
                if (event->type == LX_HOST_EVENT_BUTTON_DOWN && event->button == 1)
                {
                    lx_host_window_set_capture(window, 1);
                    window->suppress_click = 1;
                    event->type = LX_HOST_EVENT_OTHER;
                }
                if (event->type == LX_HOST_EVENT_MOTION ||
                    event->type == LX_HOST_EVENT_BUTTON_DOWN ||
                    event->type == LX_HOST_EVENT_BUTTON_UP)
                    event->type = LX_HOST_EVENT_OTHER;
            }
        }
        if ((event->type == LX_HOST_EVENT_KEY_DOWN || event->type == LX_HOST_EVENT_KEY_UP) &&
            event->key == LX_HOST_KEY_RETURN)
        {
            if (event->type == LX_HOST_EVENT_KEY_DOWN && (event->modifiers & LX_HOST_MOD_ALT))
            {
                if (!event->repeat)
                    lx_host_window_fullscreen(window, !window->fullscreen);
                window->suppress_enter = 1;
                event->type = LX_HOST_EVENT_OTHER;
            }
            else if (window->suppress_enter)
            {
                if (event->type == LX_HOST_EVENT_KEY_UP)
                    window->suppress_enter = 0;
                event->type = LX_HOST_EVENT_OTHER;
            }
        }
        if (event->type == LX_HOST_EVENT_RESIZED && (window->fullscreen || window->restoring))
        {
            if (!window->fullscreen && event->width == window->width &&
                event->height == window->height)
                window->restoring = 0;
            window->dirty = 1;
            event->type = LX_HOST_EVENT_OTHER;
        }
        if (window->fullscreen &&
            (event->type == LX_HOST_EVENT_MOTION || event->type == LX_HOST_EVENT_BUTTON_DOWN ||
             event->type == LX_HOST_EVENT_BUTTON_UP))
        {
            int width, height;
            if (SDL_GetWindowSize(sdl, &width, &height) && width > 0 && height > 0)
            {
                event->x = (int)((int64_t)event->x * window->width / width);
                event->y = (int)((int64_t)event->y * window->height / height);
            }
        }
    }
    if (window && (event->type == LX_HOST_EVENT_SHOWN || event->type == LX_HOST_EVENT_EXPOSED))
        window->dirty = 1;
    event->window_data = window ? window->data : NULL;
}
int lx_host_window_line(LxHostWindow *window, const LxHostRect *clip, int x1, int y1, int x2,
                        int y2, uint8_t r, uint8_t g, uint8_t b)
{
    prepareRenderer();
    SDL_Rect bounds = {clip->x, clip->y, clip->w, clip->h};
    int ok = SDL_SetRenderClipRect(window->renderer, &bounds) &&
             SDL_SetRenderDrawColor(window->renderer, r, g, b, 255) &&
             SDL_RenderLine(window->renderer, (float)x1, (float)y1, (float)x2, (float)y2);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}
int lx_host_video_wait_timeout(LxHostEvent *event, int milliseconds)
{
    SDL_Event input;
    if (!SDL_WaitEventTimeout(&input, milliseconds))
        return 0;
    event_from_sdl(&input, event);
    return 1;
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
    if (event->type != LX_HOST_EVENT_QUIT && event->type != LX_HOST_EVENT_OTHER)
        return 0;
    SDL_Event output = {0};
    output.type = event->type == LX_HOST_EVENT_QUIT ? SDL_EVENT_QUIT : WakeEvent;
    return SDL_PushEvent(&output);
}
void lx_host_mouse_position(int *x, int *y)
{
    float a, b;
    SDL_GetGlobalMouseState(&a, &b);
    *x = (int)a;
    *y = (int)b;
    SDL_Window *sdl = SDL_GetKeyboardFocus();
    LxHostWindow *window =
        sdl ? SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), "2ine.window", NULL) : NULL;
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

int lx_host_window_read(LxHostWindow *window, const LxHostRect *rect, uint32_t *rgb)
{
    prepareRenderer();
    if (!window || !rect || !rgb || rect->w < 0 || rect->h < 0 || rect->x < 0 || rect->y < 0 ||
        rect->w > window->width - rect->x || rect->h > window->height - rect->y)
        return 0;
    SDL_Rect area = {rect->x, rect->y, rect->w, rect->h};
    SDL_Surface *surface = SDL_RenderReadPixels(window->renderer, &area);
    if (!surface)
        return 0;
    int ok = 1;
    for (int y = 0; ok && y < rect->h; y++)
        for (int x = 0; x < rect->w; x++)
        {
            Uint8 r, g, b, a;
            if (!SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a))
            {
                ok = 0;
                break;
            }
            rgb[(size_t)y * rect->w + x] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
        }
    SDL_DestroySurface(surface);
    return ok;
}
int lx_host_window_image(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                         const uint32_t *rgb)
{
    prepareRenderer();
    if (!window || !clip || !rect || !rgb || rect->w <= 0 || rect->h <= 0)
        return 0;
    SDL_Surface *surface = SDL_CreateSurface(rect->w, rect->h, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return 0;
    for (int y = 0; y < rect->h; y++)
    {
        uint8_t *row = (uint8_t *)surface->pixels + y * surface->pitch;
        for (int x = 0; x < rect->w; x++)
        {
            uint32_t p = rgb[(size_t)y * rect->w + x];
            row[4 * x] = p >> 16;
            row[4 * x + 1] = p >> 8;
            row[4 * x + 2] = p;
            row[4 * x + 3] = 255;
        }
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(window->renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture)
        return 0;
    SDL_Rect clipping = {clip->x, clip->y, clip->w, clip->h};
    SDL_FRect dest = {(float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h};
    int ok = SDL_SetRenderClipRect(window->renderer, &clipping) &&
             SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) &&
             SDL_RenderTexture(window->renderer, texture, NULL, &dest);
    SDL_DestroyTexture(texture);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}

int lx_host_mouse_warp(int x, int y)
{
    SDL_Window *sdl = SDL_GetKeyboardFocus();
    LxHostWindow *window =
        sdl ? SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), "2ine.window", NULL) : NULL;
    if ((window && window->capture_control && !window->captured) || (!window && ControlledWindows))
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
