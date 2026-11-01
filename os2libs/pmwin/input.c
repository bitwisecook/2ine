/* Public domain. No rights reserved. Wholly AI-authored. */
/* Focus and host input are translated into the calling PM thread's WM_CHAR stream. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pm_text.h"
BOOL WinIsChild(HWND child, HWND parent)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, child) : NULL;
    for (; w; w = w->parent)
        if (w->hwnd == parent)
            return TRUE;
    return FALSE;
}
HWND topLevelWindow(AnchorBlock *anchor, HWND handle)
{
    Window *w = getWindowFromHWND(anchor, handle);
    if (!w || w == &desktop_window)
        return 0;
    while (w->parent && w->parent != &desktop_window)
        w = w->parent;
    return w->hwnd;
}
HWND WinQueryFocus(HWND desktop)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && desktop == HWND_DESKTOP && getWindowFromHWND(anchor, anchor->focus)
               ? anchor->focus
               : 0;
}
HWND WinQueryActiveWindow(HWND desktop)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && desktop == HWND_DESKTOP && getWindowFromHWND(anchor, anchor->active)
               ? anchor->active
               : 0;
}
BOOL WinSetFocus(HWND desktop, HWND handle)
{
    TRACE_NATIVE("WinSetFocus(%u, %u)", (uint)desktop, (uint)handle);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP)
        return FALSE;
    if (handle == HWND_DESKTOP)
        handle = 0;
    Window *window = getWindowFromHWND(anchor, handle);
    if (handle && !window)
        return FALSE;
    for (Window *w = window; w && w != &desktop_window; w = w->parent)
        if (!w->enabled || w->destroying)
            return FALSE;
    HWND previous = anchor->focus;
    if (previous == handle)
        return TRUE;
    anchor->focus = handle;
    Window *old = getWindowFromHWND(anchor, previous);
    if (old)
        sendMessage(old, WM_SETFOCUS, (MPARAM)(uintptr_t)handle, 0);
    if (anchor->focus != handle)
        return TRUE; /* A focus notification may redirect focus. */
    window = getWindowFromHWND(anchor, handle);
    if (window && !window->destroying)
    {
        HWND top = topLevelWindow(anchor, handle);
        Window *root = getWindowFromHWND(anchor, top);
        if (root)
            root->last_focus = handle;
        sendMessage(window, WM_SETFOCUS, (MPARAM)(uintptr_t)previous, (MPARAM)(uintptr_t)TRUE);
        if (root && anchor->focus == handle)
        {
            if (anchor->active != top)
                WinSetActiveWindow(HWND_DESKTOP, top);
            lx_host_window_focus(root->heavy.window);
        }
    }
    return TRUE;
}
BOOL WinSetActiveWindow(HWND desktop, HWND handle)
{
    TRACE_NATIVE("WinSetActiveWindow(%u, %u)", (uint)desktop, (uint)handle);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP)
        return FALSE;
    if (handle == HWND_DESKTOP)
        handle = 0;
    Window *window = getWindowFromHWND(anchor, handle);
    if (handle && (!window || window->parent != &desktop_window || !window->enabled))
        return FALSE;
    HWND previous = anchor->active;
    if (previous == handle)
        return TRUE;
    anchor->active = handle;
    Window *old = getWindowFromHWND(anchor, previous);
    if (old)
        sendMessage(old, WM_ACTIVATE, 0, (MPARAM)(uintptr_t)handle);
    window = getWindowFromHWND(anchor, handle);
    if (window && !window->destroying)
    {
        sendMessage(window, WM_ACTIVATE, (MPARAM)(uintptr_t)TRUE, (MPARAM)(uintptr_t)previous);
        if (topLevelWindow(anchor, anchor->focus) != handle)
            WinSetFocus(desktop, getWindowFromHWND(anchor, window->last_focus) ? window->last_focus
                                                                               : handle);
    }
    else if (!handle)
        WinSetFocus(desktop, 0);
    return TRUE;
}
BOOL WinSetCapture(HWND desktop, HWND window)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || (window && !getWindowFromHWND(anchor, window)))
        return FALSE;
    anchor->capture = window;
    return TRUE;
}
HWND WinQueryCapture(HWND desktop)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && desktop == HWND_DESKTOP ? anchor->capture : 0;
}
LONG WinGetKeyState(HWND desktop, LONG key)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || key < 0 || key > 255)
        return 0;
    BOOL down = key == 9    ? (anchor->modifiers & 8) != 0
                : key == 10 ? (anchor->modifiers & 16) != 0
                : key == 11 ? (anchor->modifiers & 32) != 0
                            : anchor->key_down[key] != 0;
    return down ? (SHORT)0x8000 : 0;
}
LONG WinGetPhysKeyState(HWND desktop, LONG scan)
{
    if (scan == 0x2a || scan == 0x36)
        return WinGetKeyState(desktop, 9);
    if (scan == 0x1d)
        return WinGetKeyState(desktop, 10);
    if (scan == 0x38)
        return WinGetKeyState(desktop, 11);
    return 0;
}
static unsigned virtual_key(uint32 key)
{
    switch (key)
    {
        case LX_HOST_KEY_BACKSPACE:
            return 5;
        case LX_HOST_KEY_TAB:
            return 6;
        case LX_HOST_KEY_RETURN:
            return 0x1e;
        case LX_HOST_KEY_ESCAPE:
            return 0xf;
        case LX_HOST_KEY_HOME:
            return 0x14;
        case LX_HOST_KEY_END:
            return 0x13;
        case LX_HOST_KEY_LEFT:
            return 0x15;
        case LX_HOST_KEY_RIGHT:
            return 0x17;
        case LX_HOST_KEY_UP:
            return 0x16;
        case LX_HOST_KEY_DOWN:
            return 0x18;
        case LX_HOST_KEY_DELETE:
            return 0x1b;
        case LX_HOST_KEY_INSERT:
            return 0x1a;
        case LX_HOST_KEY_PAGEUP:
            return 0x11;
        case LX_HOST_KEY_PAGEDOWN:
            return 0x12;
        case LX_HOST_KEY_SHIFT:
            return 9;
        case LX_HOST_KEY_CTRL:
            return 10;
        case LX_HOST_KEY_ALT:
            return 11;
        default:
            break;
    }
    return key >= LX_HOST_KEY_F1 && key < LX_HOST_KEY_F1 + 24 ? 0x20 + key - LX_HOST_KEY_F1 : 0;
}
static unsigned cp437(uint32 point)
{
    if (point < 128)
        return point;
    for (unsigned i = 128; i <= 255; i++)
        if (lx_pm_codepoint((uint8)i) == point)
            return i;
    return '?';
}
BOOL processKeyboardEvent(AnchorBlock *anchor, const LxHostEvent *event)
{
    Window *window = getWindowFromHWND(anchor, anchor->focus);
    if (!window || window->destroying)
        return FALSE;
    for (Window *p = window; p && p != &desktop_window; p = p->parent)
        if (!p->enabled)
            return FALSE;
    ULONG flags = (event->modifiers & LX_HOST_MOD_SHIFT ? 8 : 0) |
                  (event->modifiers & LX_HOST_MOD_CTRL ? 16 : 0) |
                  (event->modifiers & LX_HOST_MOD_ALT ? 32 : 0);
    if (event->type == LX_HOST_EVENT_TEXT)
        flags = anchor->text_modifiers;
    else
        anchor->modifiers = flags;
    if (event->type == LX_HOST_EVENT_TEXT)
    {
        const uint8 *p = (const uint8 *)event->text;
        BOOL posted = FALSE;
        for (unsigned i = 0; i < sizeof event->text && p[i];)
        {
            uint32 point = p[i++];
            unsigned extra = point >= 0xf0 ? 3 : point >= 0xe0 ? 2 : point >= 0xc0 ? 1 : 0;
            if (extra)
                point &= (1u << (6 - extra)) - 1;
            while (extra && i < sizeof event->text && (p[i] & 0xc0) == 0x80)
            {
                point = point << 6 | (p[i++] & 0x3f);
                extra--;
            }
            if (extra)
                continue;
            posted |=
                postTimestampedMsg(anchor, window->hwnd, WM_CHAR,
                                   (MPARAM)(uintptr_t)(flags | 1 | (anchor->text_scancode ? 4 : 0) |
                                                       (anchor->text_repeat ? 0x80 : 0) | 1u << 16 |
                                                       anchor->text_scancode << 24),
                                   (MPARAM)(uintptr_t)cp437(point), (ULONG)event->timestamp);
        }
        return posted;
    }
    unsigned key = virtual_key(event->key);
    if (key)
        anchor->key_down[key] = event->type == LX_HOST_EVENT_KEY_DOWN;
    if (!key && event->type == LX_HOST_EVENT_KEY_DOWN && !(flags & (16 | 32)))
    {
        anchor->text_scancode = event->scancode;
        anchor->text_modifiers = flags;
        anchor->text_repeat = event->repeat;
        return FALSE; /* Printable keydowns arrive as text. */
    }
    if (event->type == LX_HOST_EVENT_KEY_UP)
        flags |= 0x40;
    if (event->repeat)
        flags |= 0x80;
    ULONG character = event->key;
    if ((flags & 8) && character >= 'a' && character <= 'z')
        character -= 'a' - 'A';
    ULONG parameter = key ? key << 16 : character;
    return postTimestampedMsg(anchor, window->hwnd, WM_CHAR,
                              (MPARAM)(uintptr_t)(flags | (key ? 2 : 1) |
                                                  (event->scancode ? 4 : 0) | 1u << 16 |
                                                  event->scancode << 24),
                              (MPARAM)(uintptr_t)parameter, (ULONG)event->timestamp);
}

ULONG WinGetCurrentTime(HAB hab)
{
    return getAnchorBlock(hab) ? currentSystemTicks() : 0;
}
