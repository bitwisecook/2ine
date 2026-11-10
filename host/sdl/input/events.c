/* Public domain. No rights reserved. Wholly AI-authored. */
/* SDL events translated into host window, keyboard and mouse events. */
#include "host/input.h"
#include "host/sdl/window/internal.h"
#include "host/core/desktop/session_hooks.h"

/* A grouped desktop session is polled at least this often while waiting. */
#define DESKTOP_POLL_MS 100

/* Returns the PC/AT set 1 scan code OS/2 reports for a physical key, or 0. The case values
   below are those scan codes. */
static unsigned physical_key(SDL_Scancode key)
{
    static const uint8_t letters['Z' - 'A' + 1] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
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
    if (input->type == LxSdlCaptureResetEvent)
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
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            event->type = input->type == SDL_EVENT_WINDOW_MINIMIZED ? LX_HOST_EVENT_MINIMIZED
                                                                    : LX_HOST_EVENT_RESTORED;
            id = input->window.windowID;
            break;
        case SDL_EVENT_WINDOW_MOVED:
            id = input->window.windowID;
            {
                SDL_Window *moved = SDL_GetWindowFromID(id);
                LxHostWindow *window = lx_sdl_window_from(moved);
                if (window)
                {
                    window->dirty = 1;
                    if (!window->fullscreen && !window->restoring)
                    {
                        event->type = LX_HOST_EVENT_MOVED;
                        SDL_GetWindowPosition(moved, &window->x, &window->y);
                        event->x = window->x;
                        event->y = window->y;
                    }
                }
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
            /* Only ASCII keycodes pass through as characters. */
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
            event->clicks = input->button.clicks;
            event->button = input->button.button == SDL_BUTTON_LEFT     ? 1
                            : input->button.button == SDL_BUTTON_RIGHT  ? 2
                            : input->button.button == SDL_BUTTON_MIDDLE ? 3
                                                                        : 0;
            break;
        default:
            break;
    }
    SDL_Window *sdl = id ? SDL_GetWindowFromID(id) : NULL;
    LxHostWindow *window = lx_sdl_window_from(sdl);
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
                lx_sdl_window_toggle_capture(window);
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

int lx_host_video_wait_timeout(LxHostEvent *event, int milliseconds)
{
    SDL_Event input;
    Uint64 started = SDL_GetTicks();
    for (;;)
    {
        lx_desktop_poll();
        int wait = milliseconds;
        if (lx_desktop_grouped())
        {
            if (milliseconds >= 0)
            {
                Uint64 elapsed = SDL_GetTicks() - started;
                wait = elapsed >= (Uint64)milliseconds ? 0 : milliseconds - (int)elapsed;
            }
            if (wait < 0 || wait > DESKTOP_POLL_MS)
                wait = DESKTOP_POLL_MS;
        }
        SDL_ClearError();
        if (SDL_WaitEventTimeout(&input, wait))
        {
            lx_desktop_poll();
            event_from_sdl(&input, event);
            return 1;
        }
        if (*SDL_GetError())
            return 0;
        lx_desktop_poll();
        if (!lx_desktop_grouped() ||
            (milliseconds >= 0 && SDL_GetTicks() - started >= (Uint64)milliseconds))
            return 0;
    }
}

int lx_host_video_poll(LxHostEvent *event)
{
    SDL_Event input;
    lx_desktop_poll();
    if (!SDL_PollEvent(&input))
        return 0;
    event_from_sdl(&input, event);
    return 1;
}

int lx_host_video_wait(LxHostEvent *event)
{
    return lx_host_video_wait_timeout(event, -1);
}

int lx_host_video_push(const LxHostEvent *event)
{
    if (event->type != LX_HOST_EVENT_QUIT && event->type != LX_HOST_EVENT_OTHER)
        return 0;
    SDL_Event output = {0};
    output.type = event->type == LX_HOST_EVENT_QUIT ? SDL_EVENT_QUIT : LxSdlWakeEvent;
    return SDL_PushEvent(&output);
}

uint32_t lx_host_key_character(uint32_t scancode, int shifted)
{
    for (int key = 0; key < SDL_SCANCODE_COUNT; key++)
        if (physical_key((SDL_Scancode)key) == scancode)
        {
            SDL_Keycode code = SDL_GetKeyFromScancode((SDL_Scancode)key,
                                                      shifted ? SDL_KMOD_SHIFT : SDL_KMOD_NONE, 0);
            return code & SDLK_SCANCODE_MASK ? 0 : (uint32_t)code;
        }
    return 0;
}
