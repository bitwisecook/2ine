/* Public domain. No rights reserved. Wholly AI-authored. */
/* Modal state survives reentrant destruction until its message loop returns. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"

enum
{
    DIALOG_ERROR = 0xffff,
    DIALOG_CANCEL = 2
};
typedef struct PmDialog
{
    unsigned references;
    BOOL dismissed, processing;
    ULONG result;
    HWND disabled_owner;
    HWND window, previous_focus;
} PmDialog;
void dialogWordChanged(Window *window, LONG index, USHORT value)
{
    if (!window->dialog)
        return;
    if (index == 8) /* QWS_FLAGS / FF_DLGDISMISSED */
        window->dialog->dismissed = (value & 0x10) != 0;
    if (index == 10) /* QWS_RESULT */
        window->dialog->result = value;
}

BOOL initializeDialog(Window *window)
{
    window->dialog = calloc(1, sizeof *window->dialog);
    if (!window->dialog)
        return FALSE;
    window->dialog->references = 1;
    window->dialog->result = DIALOG_ERROR;
    window->dialog->window = window->hwnd;
    window->dialog->previous_focus = WinQueryFocus(HWND_DESKTOP);
    return TRUE;
}
static void release(PmDialog *dialog)
{
    if (!--dialog->references)
        free(dialog);
}
BOOL initializeDialogProcedure(HWND handle, HWND focus, PVOID parameters)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    PmDialog *dialog = window ? window->dialog : NULL;
    if (!dialog)
        return FALSE;
    dialog->references++;
    sendMessage(window, WM_INITDLG, (MPARAM)(uintptr_t)focus, parameters);
    anchor = getAnchorBlockNoHAB();
    window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    BOOL alive = window && window->dialog == dialog;
    if (alive && !dialog->dismissed && !WinIsChild(WinQueryFocus(HWND_DESKTOP), handle))
        WinSetFocus(HWND_DESKTOP, focus ? focus : handle);
    release(dialog);
    return alive;
}
static void restore_owner(PmDialog *dialog)
{
    HWND handle = dialog->disabled_owner;
    dialog->disabled_owner = 0;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *owner = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (owner && !owner->destroying)
        WinEnableWindow(handle, TRUE);
    if (anchor && (!anchor->focus || WinIsChild(anchor->focus, dialog->window)))
    {
        Window *focus = getWindowFromHWND(anchor, dialog->previous_focus);
        if (focus && !focus->destroying)
            WinSetFocus(HWND_DESKTOP, focus->hwnd);
    }
}
void destroyDialog(Window *window)
{
    PmDialog *dialog = window->dialog;
    if (!dialog)
        return;
    window->dialog = NULL;
    dialog->dismissed = TRUE;
    restore_owner(dialog);
    release(dialog);
}
BOOL WinDismissDlg(HWND handle, ULONG result)
{
    TRACE_NATIVE("WinDismissDlg(%u, %u)", (uint)handle, (uint)result);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    PmDialog *dialog = window ? window->dialog : NULL;
    if (!dialog)
        return FALSE;
    dialog->references++;
    WinSetWindowUShort(handle, 8, WinQueryWindowUShort(handle, 8) | 0x10);
    WinSetWindowUShort(handle, 10, (USHORT)result);
    dialog->dismissed = TRUE;
    dialog->result = result;
    WinShowWindow(handle, FALSE);
    restore_owner(dialog);
    release(dialog);
    return TRUE;
}
static BOOL dialog_key(AnchorBlock *anchor, HWND dialog, const QMSG *message)
{
    ULONG flags = (USHORT)(uintptr_t)message->mp1;
    unsigned key = (USHORT)((uintptr_t)message->mp2 >> 16);
    if (message->msg != WM_CHAR || !(flags & 2) || (flags & 0x40))
        return FALSE;
    if (key == 6) /* VK_TAB */
    {
        HWND first = 0, previous = 0, next = 0;
        BOOL found = FALSE;
        for (size_t i = 0; i < anchor->windows_array_len; i++)
        {
            Window *w = anchor->windows[i];
            if (!w || !w->hwnd || !(w->style & 0x10000) || !WinIsChild(w->hwnd, dialog))
                continue;
            BOOL visible = TRUE;
            for (Window *p = w; p && p != &desktop_window; p = p->parent)
                if (!p->enabled || !(p->style & WS_VISIBLE))
                    visible = FALSE;
            if (!visible)
                continue;
            if (!first)
                first = w->hwnd;
            if (found && !next)
                next = w->hwnd;
            if (w->hwnd == anchor->focus)
            {
                found = TRUE;
                if (flags & 8)
                    next = previous;
            }
            if (!found)
                previous = w->hwnd;
        }
        if (first)
            WinSetFocus(HWND_DESKTOP, next ? next : first);
        return TRUE;
    }
    Window *focused = getWindowFromHWND(anchor, anchor->focus);
    if ((key == 0x1e || key == 8) && focused && focused->window_class_name &&
        !strcmp(focused->window_class_name, "#10"))
        return FALSE;
    if (key == 0xf || key == 0x1e || key == 8) /* Escape / Enter / Newline */
    {
        ULONG id = key == 0xf ? 2 : 1;
        if (id == 1)
            for (size_t i = 0; i < anchor->windows_array_len; i++)
            {
                Window *w = anchor->windows[i];
                if (w && w->hwnd && w->enabled && (w->style & WS_VISIBLE) && (w->style & 0x400) &&
                    WinIsChild(w->hwnd, dialog))
                {
                    id = w->id;
                    break;
                }
            }
        WinPostMsg(dialog, WM_COMMAND, (MPARAM)(uintptr_t)id, (MPARAM)(uintptr_t)1);
        return TRUE;
    }
    return FALSE;
}
ULONG WinProcessDlg(HWND handle)
{
    TRACE_NATIVE("WinProcessDlg(%u)", (uint)handle);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    PmDialog *dialog = window ? window->dialog : NULL;
    if (!dialog || dialog->processing)
        return DIALOG_ERROR;
    dialog->references++;
    dialog->processing = TRUE;
    if (!dialog->dismissed && window->owner && window->owner != &desktop_window &&
        window->owner->enabled)
    {
        dialog->disabled_owner = window->owner->hwnd;
        WinEnableWindow(dialog->disabled_owner, FALSE);
    }
    while (!dialog->dismissed)
    {
        if (!anchor->message_queue.head && !WinIsWindowVisible(handle))
            WinShowWindow(handle, TRUE);
        if (dialog->dismissed)
            break;
        QMSG message = {0};
        if (!WinGetMsg(anchor->hab, &message, 0, 0, 0))
        {
            WinDismissDlg(handle, DIALOG_CANCEL);
            if (message.msg == WM_QUIT)
                WinPostQueueMsg(anchor->message_queue.hmq, WM_QUIT, message.mp1, message.mp2);
            break;
        }
        if (!dialog_key(anchor, handle, &message))
            WinDispatchMsg(anchor->hab, &message);
    }
    ULONG result = dialog->result;
    dialog->processing = FALSE;
    restore_owner(dialog);
    release(dialog);
    return result;
}
HWND WinLoadDlg(HWND parent, HWND owner, PFNWP procedure, HMODULE module, ULONG id,
                PVOID parameters)
{
    TRACE_NATIVE("WinLoadDlg(%u, %u, %p, %u, %u, %p)", (uint)parent, (uint)owner, procedure,
                 (uint)module, (uint)id, parameters);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    ULONG size;
    void *data;
    if (!anchor)
        return 0;
    if (!GLoaderState.getResource || !GLoaderState.queryResourceSize ||
        !GLoaderState.freeResource || GLoaderState.queryResourceSize(module, 4, id, &size) ||
        GLoaderState.getResource(module, 4, id, &data))
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_RESOURCE_NOT_FOUND, 0);
    HWND result = createDialog(parent, owner, procedure, data, size, parameters, module);
    GLoaderState.freeResource(data);
    return result;
}
ULONG WinDlgBox(HWND parent, HWND owner, PFNWP procedure, HMODULE module, ULONG id,
                PVOID parameters)
{
    TRACE_NATIVE("WinDlgBox(%u, %u, %p, %u, %u, %p)", (uint)parent, (uint)owner, procedure,
                 (uint)module, (uint)id, parameters);
    HWND window = WinLoadDlg(parent, owner, procedure, module, id, parameters);
    if (!window)
        return DIALOG_ERROR;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PmDialog *dialog = getWindowFromHWND(anchor, window)->dialog;
    dialog->references++;
    ULONG result = WinProcessDlg(window);
    anchor = getAnchorBlockNoHAB();
    Window *current = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (current && current->dialog == dialog)
        WinDestroyWindow(window);
    release(dialog);
    return result;
}
HWND WinCreateDlg(HWND parent, HWND owner, PFNWP procedure, PDLGTEMPLATE data, PVOID parameters)
{
    return data ? createDialog(parent, owner, procedure, (const uint8 *)data, data->cbTemplate,
                               parameters, 0)
                : 0;
}
MRESULT WinDefDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_CLOSE || message == WM_COMMAND)
    {
        WinDismissDlg(window, message == WM_CLOSE ? DIALOG_CANCEL : (USHORT)(uintptr_t)first);
        return 0;
    }
    if (message == WM_PAINT)
    {
        RECTL rect;
        HPS space = WinBeginPaint(window, 0, &rect);
        if (space)
        {
            WinFillRect(space, &rect, CLR_PALEGRAY);
            WinEndPaint(space);
        }
        return 0;
    }
    return WinDefWindowProc(window, message, first, second);
}
MRESULT WinSendDlgItemMsg(HWND window, ULONG id, ULONG message, MPARAM first, MPARAM second)
{
    return WinSendMsg(WinWindowFromID(window, id), message, first, second);
}
BOOL WinMapDlgPoints(HWND window, PPOINTL points, ULONG count, BOOL to_pixels)
{
    if (count > 65536 || (count && !points))
        return FALSE;
    HPS space = WinGetPS(window);
    LxHostFontMetrics metrics;
    BOOL valid = space && lx_host_font_metrics(lx_pm_font(space), &metrics) &&
                 metrics.average_width > 0 && metrics.ascent + metrics.descent > 0;
    if (space)
        WinReleasePS(space);
    if (!valid)
        return FALSE;
    LONG factors[2] = {metrics.average_width, metrics.ascent + metrics.descent};
    for (ULONG i = 0; i < count; i++)
    {
        for (unsigned j = 0; j < 2; j++)
        {
            sint64 value = (sint64)(j ? points[i].y : points[i].x) * (to_pixels ? factors[j]
                                                                      : j       ? 8
                                                                                : 4);
            LONG divisor = to_pixels ? (j ? 8 : 4) : factors[j];
            value /= divisor;
            if (value < INT32_MIN || value > INT32_MAX)
                return FALSE;
        }
    }
    for (ULONG i = 0; i < count; i++)
    {
        points[i].x = (LONG)((sint64)points[i].x * (to_pixels ? factors[0] : 4) /
                             (to_pixels ? 4 : factors[0]));
        points[i].y = (LONG)((sint64)points[i].y * (to_pixels ? factors[1] : 8) /
                             (to_pixels ? 8 : factors[1]));
    }
    return TRUE;
}
