/* Public domain. No rights reserved. Wholly AI-authored. */
/* Class extra bytes contain guest words; pointer values remain opaque guest addresses. */
#include "os2libs/pmwin/internal.h"
#include "guest/abi.h"

#if TWOINE_PORTABLE_LOADER && LX_LEGACY
static uint32 control_callback(uint8 *args)
{
    return (uint32)(uintptr_t)controlProcedure(lx_guest_load_u32(args), lx_guest_load_u32(args + 4),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(args + 8),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(args + 12));
}
static PFNWP previous_control_procedure(AnchorBlock *anchor)
{
    if (!anchor->control_bridge)
    {
        LxGuestMapping *mapping =
            lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
        const LxExecutionApi32 api = {control_callback, 16, 0, LX_ABI_STACK32};
        if (!mapping)
            return NULL;
        if (!lx_execution_emit_bridge32(GLoaderState.execution, lx_execution_mapping_host(mapping),
                                        4096, &api) ||
            !lx_execution_protect(GLoaderState.execution, mapping,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GLoaderState.execution, mapping);
            return NULL;
        }
        anchor->control_bridge = mapping;
    }
    return (PFNWP)(uintptr_t)lx_execution_mapping_address(anchor->control_bridge);
}
#endif

HWND WinWindowFromID(HWND window, ULONG id)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *parent = anchor ? getWindowFromHWND(anchor, window) : NULL;
    for (Window *child = parent ? parent->children : NULL; child; child = child->sibling)
        if (child->id == id)
            return child->hwnd;
    return 0;
}
USHORT WinQueryWindowUShort(HWND window, LONG index)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w)
        return 0;
    if (index == -1) /* QWS_ID */
        return (USHORT)w->id;
    if (index < 0 || (size_t)index > w->data_len || w->data_len - index < 2)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    return lx_guest_load_u16((uint8 *)w->data + index);
}
BOOL WinSetWindowUShort(HWND window, LONG index, USHORT value)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w)
        return FALSE;
    if (index == -1)
        w->id = value;
    else
    {
        if (index < 0 || (size_t)index > w->data_len || w->data_len - index < 2)
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, FALSE);
        lx_guest_store_u16((uint8 *)w->data + index, value);
        dialogWordChanged(w, index, value);
    }
    return TRUE;
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
    PFNWP callable = previous;
    if (!previous || previous == WinDefWindowProc)
        callable = defaultGuestWindowProc();
    else if (previous == WinDefDlgProc)
        callable = defaultGuestDialogProc();
#if TWOINE_PORTABLE_LOADER && LX_LEGACY
    else if (previous == controlProcedure && GLoaderState.execution)
        callable = previous_control_procedure(anchor);
#endif
    if (!callable)
        return NULL;
    w->window_proc = procedure;
    return callable;
}
