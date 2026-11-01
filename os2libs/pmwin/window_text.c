/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window text is owned by its window; queries exclude the terminating NUL. */
#include "os2libs/pmwin/controls.h"

BOOL WinSetWindowText(HWND handle, PSZ text)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || !text)
        return FALSE;
    char *copy = strdup(text);
    if (!copy)
        return FALSE;
    free(window->text);
    window->text = copy;
    if (window->parent == &desktop_window)
        lx_host_window_title(window->heavy.window, copy);
    postTimestampedMsg(anchor, handle, WM_PAINT, 0, 0, currentSystemTicks());
    controlTextChanged(window);
    return TRUE;
}
LONG WinQueryWindowTextLength(HWND handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    return window && window->text ? (LONG)strlen(window->text) : 0;
}
LONG WinQueryWindowText(HWND handle, LONG capacity, PCHAR output)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || capacity <= 0 || !output)
        return 0;
    const char *text = window->text ? window->text : "";
    size_t size = strlen(text);
    if (size >= (size_t)capacity)
        size = (size_t)capacity - 1;
    memcpy(output, text, size);
    output[size] = 0;
    return (LONG)size;
}
BOOL WinSetDlgItemText(HWND dialog, ULONG id, PSZ text)
{
    return WinSetWindowText(WinWindowFromID(dialog, id), text);
}
ULONG WinQueryDlgItemText(HWND dialog, ULONG id, LONG capacity, PCHAR output)
{
    return (ULONG)WinQueryWindowText(WinWindowFromID(dialog, id), capacity, output);
}
