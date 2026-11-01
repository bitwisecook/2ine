/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 resource dialogs use native PM templates, controls and modal lifetime. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/input.h"
typedef struct Open32Dialog
{
    ULONG window, procedure, parameter;
    LONG result;
    BOOL finished;
    struct Open32Dialog *previous;
} Open32Dialog;
static _Thread_local Open32Dialog *dialogs;
BOOL open32NativeDialogWindow(ULONG handle)
{
    for (Open32Dialog *dialog = dialogs; dialog; dialog = dialog->previous)
        for (ULONG parent = handle; parent; parent = WinQueryWindow(parent, 5))
            if (dialog->window && parent == dialog->window)
                return TRUE;
    return FALSE;
}
static MRESULT OS2API dialog_procedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    Open32Dialog *dialog = dialogs;
    while (dialog && dialog->window && dialog->window != handle)
        dialog = dialog->previous;
    if (!dialog)
        return WinDefDlgProc(handle, message, first, second);
    ULONG windows_message, a = (ULONG)(uintptr_t)first, b = (ULONG)(uintptr_t)second;
    if (message == WM_INITDLG)
    {
        dialog->window = handle;
        windows_message = 0x110;
        b = dialog->parameter;
    }
    else if (message == WM_DESTROY)
        windows_message = 2;
    else if (message == WM_COMMAND || message == WM_CONTROL)
    {
        windows_message = 0x111;
        b = WinWindowFromID(handle, a & 0xffff);
        if (message == WM_COMMAND)
            a &= 0xffff;
    }
    else if (message == WM_CHAR)
    {
        Open32Key key;
        open32DecodeKey(a, b, &key);
        open32UpdateKey(&key);
        ULONG result = lx_execution_callback32_4(GLoaderState.execution, dialog->procedure, handle,
                                                 key.message, key.key, key.parameter);
        if (result)
            return (MRESULT)(uintptr_t)result;
        return WinDefDlgProc(handle, message, first, second);
    }
    else
    {
        QMSG native = {.hwnd = handle, .msg = message, .mp1 = first, .mp2 = second};
        if (!open32PmMessage(&native, &windows_message, &a, &b))
            return WinDefDlgProc(handle, message, first, second);
    }
    ULONG result = lx_execution_callback32_4(GLoaderState.execution, dialog->procedure, handle,
                                             windows_message, a, b);
    return result ? (MRESULT)(uintptr_t)result : WinDefDlgProc(handle, message, first, second);
}
LONG DialogBoxParam(ULONG module, PSZ resource, ULONG owner, ULONG procedure, ULONG parameter)
{
    TRACE_NATIVE("DialogBoxParam(%u, %p, %u, %X, %X)", module, resource, owner, procedure,
                 parameter);
    if ((uintptr_t)resource > UINT16_MAX || !resource || !open32ValidProcedure(procedure) ||
        !open32Presentation())
    {
        open32SetError(87);
        return -1;
    }
    if (owner && !WinIsWindow(open32Presentation(), owner))
    {
        open32SetError(1400);
        return -1;
    }
    if ((!module || module == UINT32_MAX) && GLoaderState.main_module)
        module = GLoaderState.main_module->handle;
    Open32Dialog dialog = {.procedure = procedure, .parameter = parameter, .previous = dialogs};
    dialogs = &dialog;
    ULONG result =
        WinDlgBox(HWND_DESKTOP, owner, dialog_procedure, module, (ULONG)(uintptr_t)resource, NULL);
    dialogs = dialog.previous;
    if (dialog.finished)
        return dialog.result;
    if (result == 0xffff)
    {
        open32SetError(1814);
        return -1;
    }
    return (LONG)result;
}
LONG DialogBox(ULONG module, PSZ resource, ULONG owner, ULONG procedure)
{
    return DialogBoxParam(module, resource, owner, procedure, 0);
}
BOOL EndDialog(ULONG handle, LONG result)
{
    for (Open32Dialog *dialog = dialogs; dialog; dialog = dialog->previous)
        if (dialog->window == handle)
        {
            if (!WinDismissDlg(handle, (ULONG)result))
                return FALSE;
            dialog->result = result;
            dialog->finished = TRUE;
            return TRUE;
        }
    open32SetError(1400);
    return FALSE;
}
ULONG GetDlgItem(ULONG handle, LONG identity)
{
    if (identity < 0 || identity > UINT16_MAX || !open32Presentation())
    {
        open32SetError(87);
        return 0;
    }
    return WinWindowFromID(handle, (ULONG)identity);
}
BOOL SetDlgItemText(ULONG handle, LONG identity, PSZ text)
{
    ULONG child = GetDlgItem(handle, identity);
    if (!child || !text)
    {
        open32SetError(87);
        return FALSE;
    }
    if (open32FindWindow(child))
        return SetWindowText(child, text);
    if (!open32NativeDialogWindow(child))
    {
        open32SetError(1400);
        return FALSE;
    }
    size_t length = strnlen(text, 65536);
    if (length == 65536)
        return FALSE;
    char *copy = malloc(length + 1);
    if (!copy)
    {
        open32SetError(8);
        return FALSE;
    }
    for (size_t i = 0; i < length; i++)
        copy[i] = WinCpTranslateChar(0, 1004, (UCHAR)text[i], 437);
    copy[length] = 0;
    BOOL result = WinSetWindowText(child, copy);
    free(copy);
    return result;
}
ULONG GetDlgItemText(ULONG handle, LONG identity, PCHAR output, LONG capacity)
{
    ULONG child = GetDlgItem(handle, identity);
    if (!child || !output || capacity <= 0)
    {
        open32SetError(87);
        return 0;
    }
    if (open32FindWindow(child))
        return (ULONG)GetWindowText(child, output, capacity);
    if (!open32NativeDialogWindow(child))
    {
        open32SetError(1400);
        return 0;
    }
    LONG count = WinQueryWindowText(child, capacity, output);
    for (LONG i = 0; i < count; i++)
        output[i] = WinCpTranslateChar(0, 437, (UCHAR)output[i], 1004);
    return (ULONG)count;
}

LONG SendDlgItemMessage(ULONG dialog, LONG identity, ULONG message, ULONG first, ULONG second)
{
    ULONG child = GetDlgItem(dialog, identity);
    if (!child)
        return 0;
    if (open32FindWindow(child))
        return SendMessage(child, message, first, second);
    if (!open32NativeDialogWindow(child))
    {
        open32SetError(1400);
        return 0;
    }
    ULONG native;
    switch (message)
    {
        case 0xf0:
            native = 0x124;
            break; /* BM_GETCHECK */
        case 0xf1:
            native = 0x125;
            break; /* BM_SETCHECK */
        case 0xf2: /* BM_GETSTATE */
            return ((LONG)(intptr_t)WinSendMsg(child, 0x124, 0, 0) & 3) |
                   (WinSendMsg(child, 0x122, 0, 0) ? 4 : 0) |
                   (WinQueryFocus(HWND_DESKTOP) == child ? 8 : 0);
        case 0xf3:
            native = 0x123;
            break; /* BM_SETSTATE */
        case 0xf5:
            native = 0x120;
            break; /* BM_CLICK */
        case 0xf4: /* BM_SETSTYLE */
        {
            char name[16];
            if (!WinQueryClassName(child, sizeof name, name) || strcmp(name, "#3"))
            {
                open32SetError(87);
                return 0;
            }
            static const LONG styles[] = {0, 0, 1, 2, 3, 5, 6, -1, -1, 4};
            if (first >= sizeof styles / sizeof *styles || styles[first] < 0)
            {
                open32SetError(50);
                return 0;
            }
            ULONG style = (ULONG)styles[first] | (first == 1 ? 0x400 : 0);
            WinSetWindowULong(child, -2, (WinQueryWindowULong(child, -2) & ~0x40fu) | style);
            if (second)
                WinInvalidateRect(child, NULL, FALSE);
            return 0;
        }
        default:
            open32SetError(50);
            return 0;
    }
    LONG result = (LONG)(intptr_t)WinSendMsg(child, native, (MPARAM)(uintptr_t)first,
                                             (MPARAM)(uintptr_t)second);
    return message == 0xf0 ? result : 0;
}
