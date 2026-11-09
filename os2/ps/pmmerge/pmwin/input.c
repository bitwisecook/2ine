/* Public domain. No rights reserved. Wholly AI-authored. */
/* Focus, activation and capture. */
#include "os2/include/private/pmwinp.h"
#include "os2/ps/common/pm_text.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>

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

/* FC_NOLOSEACTIVE keeps WM_ACTIVATE from the window being deactivated. */
static void changeActive(AnchorBlock *anchor, HWND handle, ULONG flags)
{
    if (!(flags & 8))
    {
        WinSetActiveWindow(HWND_DESKTOP, handle);
        return;
    }
    Window *window = getWindowFromHWND(anchor, handle);
    if (!window)
        return;
    anchor->active = handle;
    sendMessage(window, WM_ACTIVATE, (MPARAM)(uintptr_t)TRUE, (MPARAM)(uintptr_t)handle);
}

BOOL lxPmHostFocusReport;

BOOL WinSetFocus(HWND desktop, HWND handle)
{
    TRACE_NATIVE("WinSetFocus(%u, %u)", (uint)desktop, (uint)handle);
    return changeFocus(desktop, handle, 0);
}

BOOL changeFocus(HWND desktop, HWND handle, ULONG flags)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP)
        return FALSE;
    if (handle == HWND_DESKTOP)
        handle = 0;
    Window *window = getWindowFromHWND(anchor, handle);
    if (handle && !window)
        return FALSE;
    if (handle && !windowAcceptsInput(window))
        return FALSE;
    HWND previous = anchor->focus;
    if (previous == handle)
        return TRUE;
    anchor->focus = handle;
    Window *old = getWindowFromHWND(anchor, previous);
    if (old && (flags & FOCUS_NOTIFY))
    {
        sendMessage(old, WM_FOCUSCHANGE, (MPARAM)(uintptr_t)handle,
                    (MPARAM)(uintptr_t)((flags & 0xffff) << 16));
        if (!(flags & 0x20)) /* FC_NOLOSESELECTION */
            sendMessage(old, WM_SETSELECTION, (MPARAM)(uintptr_t)FALSE, 0);
    }
    if (old && !(flags & 2)) /* FC_NOLOSEFOCUS */
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
        if (flags & FOCUS_NOTIFY)
        {
            sendMessage(window, WM_FOCUSCHANGE, (MPARAM)(uintptr_t)previous,
                        (MPARAM)(uintptr_t)(TRUE | (flags & 0xffff) << 16));
            if (!(flags & 0x10)) /* FC_NOSETSELECTION */
                sendMessage(window, WM_SETSELECTION, (MPARAM)(uintptr_t)TRUE, 0);
        }
        if (!(flags & 1)) /* FC_NOSETFOCUS */
            sendMessage(window, WM_SETFOCUS, (MPARAM)(uintptr_t)previous, (MPARAM)(uintptr_t)TRUE);
        if (root && anchor->focus == handle)
        {
            if (anchor->active != top && !(flags & 4)) /* FC_NOSETACTIVE */
                changeActive(anchor, top, flags);
            if (!(flags & 3)) /* FC_NOBRINGTOTOP, FC_NOBRINGTOPFIRSTWINDOW */
                if (!lxPmHostFocusReport)
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

ULONG WinGetCurrentTime(HAB hab)
{
    return getAnchorBlock(hab) ? currentSystemTicks() : 0;
}

BOOL WinFocusChange(HWND desktop, HWND focus, ULONG flags)
{
    return changeFocus(desktop, focus, (flags & 0xffff) | FOCUS_NOTIFY);
}
