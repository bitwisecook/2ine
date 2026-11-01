/* Public domain. No rights reserved. Wholly AI-authored. */
/* Class extra bytes contain guest words; pointer values remain opaque guest addresses. */
#include "os2libs/pmwin/internal.h"
#include "guest/abi.h"

HWND WinWindowFromID(HWND window, ULONG id)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *parent = anchor ? getWindowFromHWND(anchor, window) : NULL;
    for (Window *child = parent ? parent->children : NULL; child; child = child->sibling)
        if (child->id == id)
            return child->hwnd;
    return 0;
}
ULONG WinQueryWindowULong(HWND window, LONG index)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!anchor)
        return 0;
    if (!w)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    if (index == -2) /* QWL_STYLE */
        return w->style;
    if (index == -4) /* QWL_HMQ */
        return anchor->message_queue.hmq;
    if (index < 0 || (size_t)index > w->data_len || w->data_len - index < 4)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    return lx_guest_load_u32((uint8 *)w->data + index);
}
BOOL WinSetWindowULong(HWND window, LONG index, ULONG value)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!anchor)
        return FALSE;
    if (!w)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    if (index == -2)
    {
        w->style = value;
        return TRUE;
    }
    if (index < 0 || (size_t)index > w->data_len || w->data_len - index < 4)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, FALSE);
    lx_guest_store_u32((uint8 *)w->data + index, value);
    return TRUE;
}
PVOID WinQueryWindowPtr(HWND window, LONG index)
{
    return (PVOID)(uintptr_t)WinQueryWindowULong(window, index);
}
BOOL WinSetWindowPtr(HWND window, LONG index, PVOID value)
{
    if ((uintptr_t)value > UINT32_MAX)
        return FALSE;
    return WinSetWindowULong(window, index, (ULONG)(uintptr_t)value);
}

PFNWP WinSubclassWindow(HWND window, PFNWP procedure)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!anchor)
        return NULL;
    if (!w || window == HWND_DESKTOP)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULL);
    if (!procedure)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_PARAMETER, NULL);
    PFNWP previous = w->window_proc;
    w->window_proc = procedure;
    return !previous || previous == WinDefWindowProc ? defaultGuestWindowProc() : previous;
}
