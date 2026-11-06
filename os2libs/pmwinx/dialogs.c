/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 resource dialogs use native PM templates, controls and modal lifetime. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmwinx/input.h"
typedef struct Open32Dialog
{
    ULONG window, procedure, parameter;
    LONG result;
    BOOL finished;
    struct Open32Dialog *previous;
} Open32Dialog;
static _Thread_local Open32Dialog *dialogs, *modeless_dialogs;
BOOL open32NativeDialogWindow(ULONG handle)
{
    if (open32FileDialogWindow(handle) || open32NativeControlOwned(handle))
        return TRUE;
    for (Open32Dialog *dialog = dialogs; dialog; dialog = dialog->previous)
        for (ULONG parent = handle; parent; parent = WinQueryWindow(parent, 5))
            if (dialog->window && parent == dialog->window)
                return TRUE;
    for (Open32Dialog *dialog = modeless_dialogs; dialog; dialog = dialog->previous)
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
    {
        dialog = modeless_dialogs;
        while (dialog && dialog->window != handle)
            dialog = dialog->previous;
    }
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
    MRESULT native_result =
        result ? (MRESULT)(uintptr_t)result : WinDefDlgProc(handle, message, first, second);
    if (message == WM_DESTROY)
    {
        Open32Dialog **link = &modeless_dialogs;
        while (*link && *link != dialog)
            link = &(*link)->previous;
        if (*link)
        {
            *link = dialog->previous;
            free(dialog);
        }
    }
    return native_result;
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
    for (Open32Dialog *dialog = modeless_dialogs; dialog; dialog = dialog->previous)
        if (dialog->window == handle)
            return WinDestroyWindow(handle);
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

ULONG CreateDialogParam(ULONG module, PSZ resource, ULONG owner, ULONG procedure, ULONG parameter)
{
    if (!resource || (uintptr_t)resource > UINT16_MAX || !open32ValidProcedure(procedure) ||
        !open32Presentation())
    {
        open32SetError(87);
        return 0;
    }
    if ((!module || module == UINT32_MAX) && GLoaderState.main_module)
        module = GLoaderState.main_module->handle;
    Open32Dialog *dialog = calloc(1, sizeof *dialog);
    if (!dialog)
    {
        open32SetError(8);
        return 0;
    }
    dialog->procedure = procedure;
    dialog->parameter = parameter;
    dialog->previous = dialogs;
    Open32Dialog *previous = dialogs;
    dialogs = dialog;
    ULONG window =
        WinLoadDlg(HWND_DESKTOP, owner, dialog_procedure, module, (ULONG)(uintptr_t)resource, NULL);
    dialogs = previous;
    if (!window)
    {
        free(dialog);
        open32SetError(1814);
        return 0;
    }
    dialog->window = window;
    dialog->previous = modeless_dialogs;
    modeless_dialogs = dialog;
    return window;
}
ULONG CreateDialog(ULONG module, PSZ resource, ULONG owner, ULONG procedure)
{
    return CreateDialogParam(module, resource, owner, procedure, 0);
}
LONG DefDlgProc(ULONG window, ULONG message, ULONG first, ULONG second)
{
    if (message == 0x111 || message == 16)
        return (LONG)(uintptr_t)WinDefDlgProc(window, message == 16 ? WM_CLOSE : WM_COMMAND,
                                              (MPARAM)(uintptr_t)first, (MPARAM)(uintptr_t)second);
    return DefWindowProc(window, message, first, second);
}
/* Indirect Windows dialog data contains UTF-16 strings and DWORD-aligned
   control records. Convert its bounded guest stream into PM's packed template. */
typedef struct DialogReader
{
    ULONG address;
    size_t offset;
    BOOL valid;
} DialogReader;
static USHORT dialog_word(DialogReader *reader)
{
    uint8 wire[2];
    if (!reader->valid || reader->offset > 1024u * 1024u - 2 ||
        reader->address > UINT32_MAX - reader->offset - 2 ||
        !lx_execution_read(GLoaderState.execution, reader->address + reader->offset, wire, 2))
    {
        reader->valid = FALSE;
        return 0;
    }
    reader->offset += 2;
    return lx_guest_load_u16(wire);
}
static BOOL dialog_string(DialogReader *reader, char text[1024], USHORT *ordinal)
{
    USHORT c = dialog_word(reader);
    size_t count = 0;
    *ordinal = 0;
    if (c == 0xFFFF)
    {
        *ordinal = dialog_word(reader);
        text[0] = 0;
        return reader->valid;
    }
    while (c && reader->valid && count < 1023)
    {
        text[count++] = c < 256 ? (char)c : '?';
        c = dialog_word(reader);
    }
    text[count] = 0;
    if (c)
        reader->valid = FALSE;
    return reader->valid;
}
static ULONG dialog_pm_style(ULONG style, unsigned *kind)
{
    if (*kind == 2 || *kind == 3 || *kind == 5 || *kind == 6 || *kind == 7 || *kind == 10)
        return open32ControlStyle(style, kind);
    return (style & 0x10000000 ? WS_VISIBLE : 0) | (style & 0x08000000 ? WS_DISABLED : 0) |
           (style & 0x10000 ? 0x20000 : 0) | (style & 0x20000 ? 0x10000 : 0) | (style & 0xFFFF);
}
static PDLGTEMPLATE convert_dialog(ULONG address)
{
    DialogReader reader = {address, 0, TRUE};
    ULONG style = dialog_word(&reader);
    style |= (ULONG)dialog_word(&reader) << 16;
    dialog_word(&reader);
    dialog_word(&reader);
    ULONG count = dialog_word(&reader);
    SHORT x = dialog_word(&reader), y = dialog_word(&reader), width = dialog_word(&reader),
          height = dialog_word(&reader);
    if (!reader.valid || count > 1024 || width < 0 || height < 0)
        return NULL;
    uint8 *bytes = calloc(65536, 1);
    if (!bytes)
        return NULL;
    DLGTEMPLATE *t = (PDLGTEMPLATE)bytes;
    DLGTITEM *items = (DLGTITEM *)(bytes + 14);
    size_t offset = 14 + (count + 1) * 30;
    char text[1024], class_name[1024];
    USHORT ordinal;
    if (!dialog_string(&reader, text, &ordinal) || !dialog_string(&reader, class_name, &ordinal) ||
        !dialog_string(&reader, text, &ordinal))
        goto convert_bad;
    t->codepage = 437;
    t->offadlgti = 14;
    t->iItemFocus = 0xFFFF;
    items[0].cChildren = count;
    items[0].offClassName = 1;
    items[0].x = x;
    items[0].y = y;
    items[0].cx = width;
    items[0].cy = height;
    unsigned dialog_kind = 1;
    items[0].flStyle = dialog_pm_style(style, &dialog_kind);
    items[0].cchText = strlen(text);
    items[0].offText = offset;
    memcpy(bytes + offset, text, strlen(text) + 1);
    offset += strlen(text) + 1;
    items[0].offPresParams = 0xFFFF;
    items[0].offCtlData = offset;
    ULONG frame =
        (style & 0xC00000 ? 1 : 0) | (style & 0x80000 ? 2 : 0) | (style & 0x40000 ? 8 : 0);
    lx_guest_store_u32(bytes + offset, frame);
    offset += 4;
    if (style & 0x40)
    {
        dialog_word(&reader);
        if (!dialog_string(&reader, text, &ordinal))
            goto convert_bad;
    }
    for (ULONG i = 1; i <= count; i++)
    {
        reader.offset = (reader.offset + 3) & ~(size_t)3;
        DLGTITEM *item = items + i;
        ULONG control_style = dialog_word(&reader);
        control_style |= (ULONG)dialog_word(&reader) << 16;
        dialog_word(&reader);
        dialog_word(&reader);
        item->x = dialog_word(&reader);
        SHORT top = dialog_word(&reader);
        item->cx = dialog_word(&reader);
        item->cy = dialog_word(&reader);
        item->y = height - top - item->cy;
        item->id = dialog_word(&reader);
        if (!dialog_string(&reader, class_name, &ordinal))
            goto convert_bad;
        unsigned kind = ordinal == 0x80   ? 3
                        : ordinal == 0x81 ? ((control_style & 4) ? 10 : 6)
                        : ordinal == 0x82 ? 5
                        : ordinal == 0x83 ? 7
                        : ordinal == 0x84 ? 8
                        : ordinal == 0x85 ? 2
                                          : 0;
        if (!kind)
        {
            if (!strcasecmp(class_name, "BUTTON"))
                kind = 3;
            else if (!strcasecmp(class_name, "EDIT"))
                kind = 6;
            else if (!strcasecmp(class_name, "STATIC"))
                kind = 5;
            else if (!strcasecmp(class_name, "LISTBOX"))
                kind = 7;
            else if (!strcasecmp(class_name, "COMBOBOX"))
                kind = 2;
            else if (!strcasecmp(class_name, "SCROLLBAR"))
                kind = 8;
        }
        item->flStyle = dialog_pm_style(control_style, &kind);
        if (kind)
            item->offClassName = kind;
        else
        {
            size_t n = strlen(class_name);
            if (!n || offset + n + 1 >= 65536)
                goto convert_bad;
            item->cchClassName = n;
            item->offClassName = offset;
            memcpy(bytes + offset, class_name, n + 1);
            offset += n + 1;
        }
        item->offPresParams = item->offCtlData = 0xFFFF;
        if (!dialog_string(&reader, text, &ordinal))
            goto convert_bad;
        size_t n = strlen(text);
        if (offset + n + 4 >= 65536)
            goto convert_bad;
        item->offText = offset;
        if (ordinal)
        {
            item->cchText = 3;
            bytes[offset] = 0xFF;
            lx_guest_store_u16(bytes + offset + 1, ordinal);
            offset += 3;
        }
        else
        {
            item->cchText = n;
            for (size_t j = 0; j <= n; j++)
                bytes[offset + j] = WinCpTranslateChar(0, 1004, (uint8)text[j], 437);
            offset += n + 1;
        }
        ULONG extra = dialog_word(&reader);
        if (extra)
            reader.offset += extra - 2;
    }
    if (!reader.valid || offset > 65535)
        goto convert_bad;
    t->cbTemplate = offset;
    return t;
convert_bad:
    free(bytes);
    return NULL;
}
ULONG _CreateDialogIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure,
                                 ULONG parameter)
{
    (void)module;
    if (!open32ValidProcedure(procedure) || !open32Presentation())
    {
        open32SetError(87);
        return 0;
    }
    PDLGTEMPLATE data = convert_dialog(address);
    if (!data)
    {
        open32SetError(1813);
        return 0;
    }
    Open32Dialog *dialog = calloc(1, sizeof *dialog);
    if (!dialog)
    {
        free(data);
        open32SetError(8);
        return 0;
    }
    dialog->procedure = procedure;
    dialog->parameter = parameter;
    Open32Dialog *previous = dialogs;
    dialog->previous = dialogs;
    dialogs = dialog;
    ULONG window = WinCreateDlg(HWND_DESKTOP, owner, dialog_procedure, data, NULL);
    dialogs = previous;
    free(data);
    if (!window)
    {
        free(dialog);
        open32SetError(1813);
        return 0;
    }
    dialog->window = window;
    dialog->previous = modeless_dialogs;
    modeless_dialogs = dialog;
    return window;
}
LONG _DialogBoxIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure,
                             ULONG parameter)
{
    (void)module;
    if (!open32ValidProcedure(procedure) || !open32Presentation())
    {
        open32SetError(87);
        return -1;
    }
    PDLGTEMPLATE data = convert_dialog(address);
    if (!data)
    {
        open32SetError(1813);
        return -1;
    }
    Open32Dialog dialog = {.procedure = procedure, .parameter = parameter, .previous = dialogs};
    dialogs = &dialog;
    ULONG window = WinCreateDlg(HWND_DESKTOP, owner, dialog_procedure, data, NULL);
    free(data);
    LONG result = window ? (LONG)WinProcessDlg(window) : -1;
    if (dialog.finished)
        result = dialog.result;
    if (window && WinIsWindow(open32Presentation(), window))
        WinDestroyWindow(window);
    dialogs = dialog.previous;
    open32NativeControlsPrune();
    return result;
}
LONG _DialogBoxIndirect(ULONG module, ULONG address, ULONG owner, ULONG procedure)
{
    return _DialogBoxIndirectParam(module, address, owner, procedure, 0);
}

void open32DialogsCleanup(void)
{
    while (modeless_dialogs)
    {
        Open32Dialog *dialog = modeless_dialogs;
        if (WinIsWindow(open32Presentation(), dialog->window))
            WinDestroyWindow(dialog->window);
        if (modeless_dialogs == dialog)
        {
            modeless_dialogs = dialog->previous;
            free(dialog);
        }
    }
}
