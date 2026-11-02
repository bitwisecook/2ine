/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 focus and activation use the current PM thread's actual window state. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
ULONG GetActiveWindow(void)
{
    return open32Presentation() ? WinQueryActiveWindow(HWND_DESKTOP) : 0;
}
ULONG SetActiveWindow(ULONG window)
{
    ULONG previous = GetActiveWindow();
    if (!WinSetActiveWindow(HWND_DESKTOP, window))
    {
        open32SetError(1400);
        return 0;
    }
    return previous;
}
ULONG GetFocus(void)
{
    return open32Presentation() ? open32PublicWindow(WinQueryFocus(HWND_DESKTOP)) : 0;
}
ULONG SetFocus(ULONG window)
{
    ULONG previous = GetFocus();
    if (!WinSetFocus(HWND_DESKTOP, open32ClientWindow(window)))
    {
        open32SetError(1400);
        return 0;
    }
    return previous;
}

ULONG GetCapture(void)
{
    return open32Presentation() ? open32PublicWindow(WinQueryCapture(HWND_DESKTOP)) : 0;
}
ULONG SetCapture(ULONG window)
{
    ULONG previous = GetCapture();
    if (!WinSetCapture(HWND_DESKTOP, open32ClientWindow(window)))
    {
        open32SetError(1400);
        return 0;
    }
    return previous;
}
BOOL ReleaseCapture(void)
{
    return open32Presentation() && WinSetCapture(HWND_DESKTOP, 0);
}

BOOL EnableWindow(ULONG handle, BOOL enable)
{
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), handle))
    {
        open32SetError(1400);
        return FALSE;
    }
    BOOL disabled = !WinIsWindowEnabled(handle);
    Open32Window *window = open32FindWindow(handle);
    if (window)
    {
        if (enable)
            window->style &= ~0x08000000u;
        else
            window->style |= 0x08000000u;
    }
    if (!WinEnableWindow(handle, enable))
    {
        open32SetError(1400);
        return FALSE;
    }
    return disabled;
}
BOOL IsWindowEnabled(ULONG window)
{
    return open32Presentation() && WinIsWindowEnabled(window);
}
BOOL IsWindowVisible(ULONG window)
{
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), window))
        return FALSE;
    for (ULONG cursor = window; cursor; cursor = WinQueryWindow(cursor, 5))
        if (!WinIsWindowVisible(cursor))
            return FALSE;
    return TRUE;
}
BOOL IsWindow(ULONG window)
{
    return open32Presentation() && WinIsWindow(open32Presentation(), window);
}

BOOL BringWindowToTop(ULONG window)
{
    if (!open32Presentation() ||
        !WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER | SWP_ACTIVATE))
    {
        open32SetError(1400);
        return FALSE;
    }
    return TRUE;
}

ULONG GetCurrentProcessId(void)
{
    return GLoaderState.pib.pib_ulpid;
}
ULONG GetWindowThreadProcessId(ULONG window, PVOID process)
{
    if (!open32FindWindow(window) || !WinIsWindow(open32Presentation(), window))
    {
        open32SetError(1400);
        return 0;
    }
    if (process)
        lx_guest_store_u32(process, GetCurrentProcessId());
    return GetCurrentThreadId();
}

ULONG GetCurrentProcess(void)
{
    return UINT32_MAX;
}

BOOL SetForegroundWindow(ULONG handle)
{
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), handle))
    {
        open32SetError(1400);
        return FALSE;
    }
    ULONG parent;
    while ((parent = WinQueryWindow(handle, 5)) && parent != HWND_DESKTOP)
        handle = parent;
    return BringWindowToTop(handle);
}
