/* Public domain. No rights reserved. Wholly AI-authored. */
/* Class extra bytes contain guest words; pointer values remain opaque guest addresses. */
#include "os2/ps/pmwin/internal.h"
#include "emulator/abi.h"

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
            lx_execution_map(GProcessState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
        const LxExecutionApi32 api = {control_callback, 16, 0, LX_ABI_STACK32};
        if (!mapping)
            return NULL;
        if (!lx_execution_emit_bridge32(GProcessState.execution, lx_execution_mapping_host(mapping),
                                        4096, &api) ||
            !lx_execution_protect(GProcessState.execution, mapping,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GProcessState.execution, mapping);
            return NULL;
        }
        anchor->control_bridge = mapping;
    }
    return (PFNWP)(uintptr_t)lx_execution_mapping_address(anchor->control_bridge);
}

static uint32 frame_control_callback(uint8 *args)
{
    return (uint32)(uintptr_t)frameControlProcedure(
        lx_guest_load_u32(args), lx_guest_load_u32(args + 4),
        (MPARAM)(uintptr_t)lx_guest_load_u32(args + 8),
        (MPARAM)(uintptr_t)lx_guest_load_u32(args + 12));
}

static PFNWP previous_frame_control_procedure(AnchorBlock *anchor)
{
    if (!anchor->frame_control_bridge)
    {
        LxGuestMapping *mapping =
            lx_execution_map(GProcessState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
        const LxExecutionApi32 api = {frame_control_callback, 16, 0, LX_ABI_STACK32};
        if (!mapping)
            return NULL;
        if (!lx_execution_emit_bridge32(GProcessState.execution, lx_execution_mapping_host(mapping),
                                        4096, &api) ||
            !lx_execution_protect(GProcessState.execution, mapping,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GProcessState.execution, mapping);
            return NULL;
        }
        anchor->frame_control_bridge = mapping;
    }
    return (PFNWP)(uintptr_t)lx_execution_mapping_address(anchor->frame_control_bridge);
}

static uint32 menu_callback(uint8 *args)
{
    return (uint32)(uintptr_t)menuProcedure(lx_guest_load_u32(args), lx_guest_load_u32(args + 4),
                                            (MPARAM)(uintptr_t)lx_guest_load_u32(args + 8),
                                            (MPARAM)(uintptr_t)lx_guest_load_u32(args + 12));
}

static PFNWP previous_menu_procedure(AnchorBlock *anchor)
{
    if (!anchor->menu_bridge)
    {
        LxGuestMapping *mapping =
            lx_execution_map(GProcessState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
        const LxExecutionApi32 api = {menu_callback, 16, 0, LX_ABI_STACK32};
        if (!mapping)
            return NULL;
        if (!lx_execution_emit_bridge32(GProcessState.execution, lx_execution_mapping_host(mapping),
                                        4096, &api) ||
            !lx_execution_protect(GProcessState.execution, mapping,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GProcessState.execution, mapping);
            return NULL;
        }
        anchor->menu_bridge = mapping;
    }
    return (PFNWP)(uintptr_t)lx_execution_mapping_address(anchor->menu_bridge);
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
    TRACE_NATIVE("WinQueryWindowULong(%u, %d)", (uint)window, (int)index);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!anchor)
        return 0;
    if (!w)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    if (index == -3) /* QWP_PFNWP */
        return (ULONG)(uintptr_t)callableWindowProcedure(anchor, w->window_proc);
    if (index == -2) /* QWL_STYLE */
        return w->style;
    if (index == -4) /* QWL_HMQ */
        return anchor->message_queue.hmq;
    if (index < 0 || (size_t)index > w->data_len || w->data_len - index < 4)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    return lx_guest_load_u32((uint8 *)w->data + index);
}

static void resume_style_damage(Window *window)
{
    if (windowAcceptsUpdates(window) && !WinIsRectEmpty(0, &window->invalid))
    {
        RECTL damage = window->invalid;
        WinInvalidateRect(window->hwnd, &damage, FALSE);
    }
    for (Window *child = window->children; child; child = child->sibling)
        resume_style_damage(child);
}

BOOL WinSetWindowULong(HWND window, LONG index, ULONG value)
{
    TRACE_NATIVE("WinSetWindowULong(%u, %d, %u)", (uint)window, (int)index, (uint)value);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!anchor)
        return FALSE;
    if (!w)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    if (index == -3)
        return WinSubclassWindow(window, (PFNWP)(uintptr_t)value) != NULL;
    if (index == -2)
    {
        BOOL was_visible = (w->style & WS_VISIBLE) != 0;
        ULONG previous_style = w->style;
        w->style = value;
        if (isFrameWindow(w) && ((previous_style ^ value) & 0x800)) /* FS_SIZEBORDER */
            applyHostWindowChrome(w);
        /* Applications can hide an ancestor through QWL_STYLE while changing
           several controls, then restore its style. Damage recorded while the
           ancestor was hidden must become eligible for paint again. This does
           not manufacture damage or change WinEnableWindowUpdate semantics. */
        if (!was_visible && (value & WS_VISIBLE))
            resume_style_damage(w);
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
    TRACE_NATIVE("WinSubclassWindow(%u, %p)", (uint)window, procedure);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!anchor)
        return NULL;
    if (!w || window == HWND_DESKTOP)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULL);
    if (!procedure)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_PARAMETER, NULL);
    PFNWP previous = w->window_proc;
    PFNWP callable = callableWindowProcedure(anchor, previous);
    if (!callable)
        return NULL;
    w->window_proc = procedure;
    return callable;
}

PFNWP callableWindowProcedure(AnchorBlock *anchor, PFNWP previous)
{
    PFNWP callable = previous;
    if (!previous || previous == WinDefWindowProc)
        callable = defaultGuestWindowProc();
    else if (previous == WinDefDlgProc)
        callable = defaultGuestDialogProc();
#if TWOINE_PORTABLE_LOADER && LX_LEGACY
    else if (previous == menuProcedure && GProcessState.execution)
        callable = previous_menu_procedure(anchor);
    else if (previous == controlProcedure && GProcessState.execution)
        callable = previous_control_procedure(anchor);
    else if (previous == frameControlProcedure && GProcessState.execution)
        callable = previous_frame_control_procedure(anchor);
#endif
    return callable;
}

BOOL WinQueryClassInfo(HAB hab, PSZ name, PCLASSINFO info)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !name || !info)
        return FALSE;
    WindowClass *window_class = findRegisteredClass(anchor, name);
    if (!window_class)
        return FALSE;
    PFNWP procedure = callableWindowProcedure(anchor, window_class->window_proc);
    if (!procedure)
        return FALSE;
    info->flClassStyle = window_class->style;
    info->pfnWindowProc = procedure;
    info->cbWindowData = window_class->data_len;
    return TRUE;
}
