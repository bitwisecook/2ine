/* Public domain. No rights reserved. Wholly AI-authored. */
/* Dialog windows: modal processing, dismissal, item enumeration and dialog units. Modal state
   survives reentrant destruction until its message loop returns. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/bsedos.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>
#include <limits.h>

/* Dialog units are a quarter of the dialog font's average character width and an eighth of its
   height. The font is System Proportional, whose 96-DPI average width and maximum baseline are
   6 and 16 pels. */
enum
{
    DIALOG_UNITS_PER_CHAR_X = 4,
    DIALOG_UNITS_PER_CHAR_Y = 8,
    SYSTEM_FONT_AVERAGE_WIDTH = 6,
    SYSTEM_FONT_HEIGHT = 16,
    MAX_DIALOG_POINTS = 65536
};

/* The logical color index WinDefDlgProc loads with the dialog background. */
enum
{
    DIALOG_BACKGROUND_INDEX = 254
};

typedef struct PmDialog
{
    unsigned references;
    BOOL dismissed, processing;
    ULONG result;
    HWND disabled_owner;
    HWND window, previous_focus;
} PmDialog;

static BOOL containsModal(Window *window)
{
    if (!window->destroying && window->dialog && window->dialog->processing &&
        !window->dialog->dismissed)
        return TRUE;
    for (Window *child = window->children; child; child = child->sibling)
        if (containsModal(child))
            return TRUE;
    return FALSE;
}

static BOOL windowCyclePolicy(Window *window, BOOL *eligible, BOOL *modal)
{
    if (!window || window == &desktop_window)
        return FALSE;
    while (window->parent && window->parent != &desktop_window)
        window = window->parent;
    if (window->parent != &desktop_window || !window->heavy.window)
        return FALSE;
    BOOL menu =
        window->menu || (window->window_class_name && !strcmp(window->window_class_name, "#4"));
    *eligible = window->enabled && !window->destroying && !window->dialog && !menu;
    *modal = containsModal(window);
    return TRUE;
}

void updateWindowCyclePolicy(Window *window)
{
    BOOL eligible, modal;
    if (!windowCyclePolicy(window, &eligible, &modal))
        return;
    while (window->parent && window->parent != &desktop_window)
        window = window->parent;
    lx_host_window_set_cycle_policy(window->heavy.window, eligible, modal);
}

BOOL windowAcceptsInput(Window *window)
{
    PmDialog *modal = NULL;
    for (Window *p = window; p && p != &desktop_window; p = p->parent)
    {
        if (p->destroying || (!p->enabled && (!modal || modal->disabled_owner != p->hwnd)))
            return FALSE;
        if (p->dialog && p->dialog->processing && !p->dialog->dismissed)
            modal = p->dialog;
    }
    return window != NULL;
}

void dialogWordChanged(Window *window, LONG index, USHORT value)
{
    if (!window->dialog)
        return;
    if (index == QWS_FLAGS)
        window->dialog->dismissed = (value & FF_DLGDISMISSED) != 0;
    if (index == QWS_RESULT)
        window->dialog->result = value;
    updateWindowCyclePolicy(window);
}

BOOL initializeDialog(Window *window)
{
    window->dialog = calloc(1, sizeof *window->dialog);
    if (!window->dialog)
        return FALSE;
    window->dialog->references = 1;
    window->dialog->result = DID_ERROR;
    window->dialog->window = window->hwnd;
    window->dialog->previous_focus = WinQueryFocus(HWND_DESKTOP);
    updateWindowCyclePolicy(window);
    return TRUE;
}

static void release(PmDialog *dialog)
{
    if (!--dialog->references)
        free(dialog);
}

HWND WinEnumDlgItem(HWND dialog, HWND item, ULONG code)
{
    TRACE_NATIVE("WinEnumDlgItem(%u, %u, %u)", dialog, item, code);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *parent = anchor ? getWindowFromHWND(anchor, dialog) : NULL;
    if (!parent)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    if (code > EDI_PREVGROUPITEM || !parent->children)
        return 0;
    Window *current = getWindowFromHWND(anchor, item);
    while (current && current != parent && current->parent != parent)
        current = current->parent;
    if (code >= EDI_NEXTTABITEM && (!current || current == parent))
        return parent->children->hwnd;
    Window *start = parent->children, *end = NULL;
    if (code >= EDI_FIRSTGROUPITEM)
    {
        for (Window *w = start; w; w = w->sibling)
        {
            if (w->style & WS_GROUP)
                start = w;
            if (w == current)
                break;
        }
        end = start->sibling;
        while (end && !(end->style & WS_GROUP))
            end = end->sibling;
    }
    Window *first = NULL, *last = NULL, *previous = NULL, *next = NULL;
    BOOL passed = FALSE;
    for (Window *w = start; w != end; w = w->sibling)
    {
        BOOL eligible = code >= EDI_FIRSTGROUPITEM || (w->style & WS_TABSTOP);
        if (w == current)
            passed = TRUE;
        if (!eligible)
            continue;
        if (!first)
            first = w;
        last = w;
        if (!passed)
            previous = w;
        else if (w != current && !next)
            next = w;
    }
    Window *result;
    switch (code)
    {
        case EDI_FIRSTTABITEM:
        case EDI_FIRSTGROUPITEM:
            result = first;
            break;
        case EDI_LASTTABITEM:
        case EDI_LASTGROUPITEM:
            result = last;
            break;
        case EDI_NEXTTABITEM:
        case EDI_NEXTGROUPITEM:
            result = next ? next : first;
            break;
        default:
            result = previous ? previous : last;
            break;
    }
    return result ? result->hwnd : 0;
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
    updateWindowCyclePolicy(window);
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
    WinSetWindowUShort(handle, QWS_FLAGS,
                       WinQueryWindowUShort(handle, QWS_FLAGS) | FF_DLGDISMISSED);
    WinSetWindowUShort(handle, QWS_RESULT, (USHORT)result);
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
    if (message->msg != WM_CHAR || !(flags & KC_VIRTUALKEY) || (flags & KC_KEYUP))
        return FALSE;
    if (key == VK_TAB)
    {
        HWND first = 0, previous = 0, next = 0;
        BOOL found = FALSE;
        for (size_t i = 0; i < anchor->windows_array_len; i++)
        {
            Window *w = anchor->windows[i];
            if (!w || !w->hwnd || !(w->style & WS_TABSTOP) || !WinIsChild(w->hwnd, dialog))
                continue;
            BOOL visible = windowAcceptsInput(w);
            for (Window *p = w; p && p != &desktop_window; p = p->parent)
                if (!(p->style & WS_VISIBLE))
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
                if (flags & KC_SHIFT)
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
    /* A multi-line entry field keeps Enter for itself. */
    if ((key == VK_ENTER || key == VK_NEWLINE) && focused && focused->window_class_name &&
        !strcmp(focused->window_class_name, "#10"))
        return FALSE;
    if (key == VK_ESC || key == VK_ENTER || key == VK_NEWLINE)
    {
        ULONG id = key == VK_ESC ? DID_CANCEL : DID_OK;
        if (id == DID_OK)
            for (size_t i = 0; i < anchor->windows_array_len; i++)
            {
                Window *w = anchor->windows[i];
                if (w && w->hwnd && w->enabled && (w->style & WS_VISIBLE) &&
                    (w->style & BS_DEFAULT) && WinIsChild(w->hwnd, dialog))
                {
                    id = w->id;
                    break;
                }
            }
        WinPostMsg(dialog, WM_COMMAND, (MPARAM)(uintptr_t)id, (MPARAM)(uintptr_t)CMDSRC_PUSHBUTTON);
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
        return DID_ERROR;
    dialog->references++;
    dialog->processing = TRUE;
    updateWindowCyclePolicy(window);
    if (!dialog->dismissed && window->owner && window->owner != &desktop_window &&
        window->owner->enabled)
    {
        HWND focus = anchor->focus;
        dialog->disabled_owner = window->owner->hwnd;
        WinEnableWindow(dialog->disabled_owner, FALSE);
        if (focus && WinIsChild(focus, handle))
            WinSetFocus(HWND_DESKTOP, focus);
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
            WinDismissDlg(handle, DID_CANCEL);
            if (message.msg == WM_QUIT)
                WinPostQueueMsg(anchor->message_queue.hmq, WM_QUIT, message.mp1, message.mp2);
            break;
        }
        if (!dialog_key(anchor, handle, &message))
            WinDispatchMsg(anchor->hab, &message);
    }
    ULONG result = dialog->result;
    dialog->processing = FALSE;
    window = getWindowFromHWND(anchor, handle);
    if (window)
        updateWindowCyclePolicy(window);
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
    if (!GProcessState.getResource || !GProcessState.queryResourceSize ||
        !GProcessState.freeResource ||
        GProcessState.queryResourceSize(module, RT_DIALOG, id, &size) ||
        GProcessState.getResource(module, RT_DIALOG, id, &data))
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_RESOURCE_NOT_FOUND, 0);
    HWND result = createDialog(parent, owner, procedure, data, size, parameters, module);
    GProcessState.freeResource(data);
    return result;
}

ULONG WinDlgBox(HWND parent, HWND owner, PFNWP procedure, HMODULE module, ULONG id,
                PVOID parameters)
{
    TRACE_NATIVE("WinDlgBox(%u, %u, %p, %u, %u, %p)", (uint)parent, (uint)owner, procedure,
                 (uint)module, (uint)id, parameters);
    HWND window = WinLoadDlg(parent, owner, procedure, module, id, parameters);
    if (!window)
        return DID_ERROR;
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
        WinDismissDlg(window, message == WM_CLOSE ? DID_CANCEL : (USHORT)(uintptr_t)first);
        return 0;
    }
    if (message == WM_PAINT)
    {
        RECTL rect;
        HPS space = WinBeginPaint(window, 0, &rect);
        if (space)
        {
            LONG color =
                windowPresentationColor(getWindowFromHWND(getAnchorBlockNoHAB(), window), FALSE,
                                        WinQuerySysColor(HWND_DESKTOP, SYSCLR_DIALOGBACKGROUND, 0));
            if (lx_pm_color_table(space, 0, LCOLF_CONSECRGB, DIALOG_BACKGROUND_INDEX, 1, &color))
                WinFillRect(space, &rect, DIALOG_BACKGROUND_INDEX);
            paintFrameBorder(getWindowFromHWND(getAnchorBlockNoHAB(), window), space);
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

typedef struct
{
    unsigned dpi;
    LONG width, height;
} SystemDialogMetrics;

static LX_HOST_THREAD_LOCAL SystemDialogMetrics DialogMetrics;

static BOOL mapDialogCoordinate(LONG input, LONG numerator, LONG divisor, LONG *output)
{
    int64_t value = (int64_t)input * numerator;
    value = (value + (value < 0 ? -(int64_t)divisor / 2 : divisor / 2)) / divisor;
    if (value < INT32_MIN || value > INT32_MAX)
        return FALSE;
    *output = (LONG)value;
    return TRUE;
}

BOOL WinMapDlgPoints(HWND window, PPOINTL points, ULONG count, BOOL to_pixels)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (count > MAX_DIALOG_POINTS || (count && !points) || !anchor ||
        !getWindowFromHWND(anchor, window))
        return FALSE;
    unsigned dpi = lx_pm_dc_dpi(0);
    if (DialogMetrics.dpi != dpi)
    {
        /* Presentation fonts affect text, independently of template coordinates. */
        DialogMetrics.width = lx_pm_display_scale(SYSTEM_FONT_AVERAGE_WIDTH);
        DialogMetrics.height = lx_pm_display_scale(SYSTEM_FONT_HEIGHT);
        DialogMetrics.dpi = dpi;
    }
    LONG factors[2] = {DialogMetrics.width, DialogMetrics.height};
    for (ULONG i = 0; i < count; i++)
    {
        for (unsigned j = 0; j < 2; j++)
        {
            LONG output;
            if (!mapDialogCoordinate(j ? points[i].y : points[i].x,
                                     to_pixels ? factors[j]
                                     : j       ? DIALOG_UNITS_PER_CHAR_Y
                                               : DIALOG_UNITS_PER_CHAR_X,
                                     to_pixels
                                         ? (j ? DIALOG_UNITS_PER_CHAR_Y : DIALOG_UNITS_PER_CHAR_X)
                                         : factors[j],
                                     &output))
                return FALSE;
        }
    }
    for (ULONG i = 0; i < count; i++)
    {
        mapDialogCoordinate(points[i].x, to_pixels ? factors[0] : DIALOG_UNITS_PER_CHAR_X,
                            to_pixels ? DIALOG_UNITS_PER_CHAR_X : factors[0], &points[i].x);
        mapDialogCoordinate(points[i].y, to_pixels ? factors[1] : DIALOG_UNITS_PER_CHAR_Y,
                            to_pixels ? DIALOG_UNITS_PER_CHAR_Y : factors[1], &points[i].y);
    }
    return TRUE;
}

BOOL WinQueryDlgItemShort(HWND parent, ULONG id, PSHORT output, BOOL signed_value)
{
    if (output)
        *output = 0;
    if (!WinWindowFromID(parent, id))
        return FALSE;
    char text[32] = {0};
    WinQueryDlgItemText(parent, id, sizeof text, text);
    size_t end = strlen(text);
    while (end && text[end - 1] == ' ')
        end--;
    text[end] = 0;
    const char *p = text;
    while (*p == ' ')
        p++;
    BOOL negative = *p == '-';
    if (negative)
    {
        if (!signed_value)
            return FALSE;
        p++;
    }
    if (*p < '0' || *p > '9')
        return FALSE;
    ULONG value = 0;
    for (; *p >= '0' && *p <= '9'; p++)
    {
        ULONG digit = (ULONG)(*p - '0');
        if (value > USHRT_MAX / 10 || (value == USHRT_MAX / 10 && digit > USHRT_MAX % 10))
            return FALSE;
        value = value * 10 + digit;
        if (signed_value && value > SHRT_MAX)
            return FALSE;
    }
    if (*p)
        return FALSE;
    if (output)
        *output = (SHORT)(negative ? -(LONG)value : (LONG)value);
    return TRUE;
}

BOOL WinSetDlgItemShort(HWND parent, ULONG id, USHORT value, BOOL signed_value)
{
    char text[24];
    if (signed_value)
        snprintf(text, sizeof text, "%d", (int)(SHORT)value);
    else
        snprintf(text, sizeof text, "%u", (unsigned)value);
    return WinSetDlgItemText(parent, id, text);
}
