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
    if (open32FileDialogWindow(handle))
        return TRUE;
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
        else
            a = open32ControlNotification(b, a);
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
    open32NativeControlsPrune();
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
    TRACE_NATIVE("GetDlgItem(%u, %d)", (uint)handle, (int)identity);
    if (identity < 0 || identity > UINT16_MAX || !open32Presentation())
    {
        open32SetError(87);
        return 0;
    }
    return WinWindowFromID(open32ClientWindow(handle), (ULONG)identity);
}
BOOL SetDlgItemText(ULONG handle, LONG identity, PSZ text)
{
    ULONG child = GetDlgItem(handle, identity);
    if (!child || !text)
    {
        open32SetError(87);
        return FALSE;
    }
    return SetWindowText(child, text);
}
ULONG GetDlgItemText(ULONG handle, LONG identity, PCHAR output, LONG capacity)
{
    ULONG child = GetDlgItem(handle, identity);
    if (!child || !output || capacity <= 0)
    {
        open32SetError(87);
        return 0;
    }
    return (ULONG)GetWindowText(child, output, capacity);
}

LONG SendDlgItemMessage(ULONG dialog, LONG identity, ULONG message, ULONG first, ULONG second)
{
    TRACE_NATIVE("SendDlgItemMessage(%u, %d, %x, %x, %x)", (uint)dialog, (int)identity,
                 (uint)message, (uint)first, (uint)second);
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
    return open32NativeControlMessage(child, message, first, second, FALSE);
}

ULONG IsDlgButtonChecked(ULONG dialog, LONG identity)
{
    return (ULONG)SendDlgItemMessage(dialog, identity, 0xf0, 0, 0);
}
BOOL CheckDlgButton(ULONG dialog, LONG identity, ULONG check)
{
    ULONG child = GetDlgItem(dialog, identity);
    if (!child || check > 2)
    {
        open32SetError(87);
        return FALSE;
    }
    SendDlgItemMessage(dialog, identity, 0xf1, check, 0);
    return TRUE;
}
