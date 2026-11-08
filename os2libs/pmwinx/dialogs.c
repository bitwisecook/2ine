/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 resource dialogs use native PM templates, controls and modal lifetime. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmwinx/input.h"
#include "os2libs/pmwin/menus.h"
#include "os2libs/pmgpi.h"
#include <stdio.h>

typedef struct Open32Dialog
{
    ULONG window, procedure, class_procedure, parameter;
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

/* A dialog of a registered class runs that class's procedure, whose DefDlgProc call reaches
   the dialog procedure; any other dialog runs the dialog procedure directly. */
static ULONG dialog_callback(Open32Dialog *dialog, ULONG handle, ULONG message, ULONG first,
                             ULONG second)
{
    return lx_execution_callback32_4(GProcessState.execution,
                                     dialog->class_procedure ? dialog->class_procedure
                                                             : dialog->procedure,
                                     handle, message, first, second);
}

static Open32Dialog *classed_dialog(ULONG handle)
{
    for (Open32Dialog *dialog = dialogs; dialog; dialog = dialog->previous)
        if (dialog->window == handle && dialog->class_procedure)
            return dialog;
    for (Open32Dialog *dialog = modeless_dialogs; dialog; dialog = dialog->previous)
        if (dialog->window == handle && dialog->class_procedure)
            return dialog;
    return NULL;
}

/* PM child origins follow the bottom edge and Windows ones follow the top edge,
   so a Windows dialog keeps its controls in place when its height changes. */
static void keep_controls_at_top(HWND dialog, LONG delta)
{
    if (!delta)
        return;
    for (HWND child = WinQueryWindow(dialog, 3); child; child = WinQueryWindow(child, 1))
    {
        SWP position;
        if ((USHORT)WinQueryWindowUShort(child, -1) < 0x8000 && WinQueryWindowPos(child, &position))
            WinSetWindowPos(child, 0, position.x, position.y + delta, 0, 0,
                            SWP_MOVE | SWP_NOADJUST | SWP_NOREDRAW);
    }
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
    if (message == WM_SIZE && dialog->window == handle && a >> 16 & 0xFFFF)
        keep_controls_at_top(handle, (LONG)(b >> 16 & 0xFFFF) - (LONG)(a >> 16 & 0xFFFF));
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
        ULONG result = dialog_callback(dialog, handle, key.message, key.key, key.parameter);
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
    ULONG result = dialog_callback(dialog, handle, windows_message, a, b);
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
    if (!resource || !open32ValidProcedure(procedure) || !open32Presentation())
    {
        open32SetError(87);
        return -1;
    }
    if (owner && !WinIsWindow(open32Presentation(), owner))
    {
        open32SetError(1400);
        return -1;
    }
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    ULONG identity = open32ResourceIdentity(module, resource, 4); /* RT_DIALOG */
    if (!identity)
    {
        open32SetError(1814);
        return -1;
    }
    Open32Dialog dialog = {.procedure = procedure, .parameter = parameter, .previous = dialogs};
    dialogs = &dialog;
    ULONG result = WinDlgBox(HWND_DESKTOP, owner, dialog_procedure, module, identity, NULL);
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
    if (!resource || !open32ValidProcedure(procedure) || !open32Presentation())
    {
        open32SetError(87);
        return 0;
    }
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    ULONG identity = open32ResourceIdentity(module, resource, 4); /* RT_DIALOG */
    if (!identity)
    {
        open32SetError(1814);
        return 0;
    }
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
    ULONG window = WinLoadDlg(HWND_DESKTOP, owner, dialog_procedure, module, identity, NULL);
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
    Open32Dialog *classed = classed_dialog(window);
    if (classed) /* dialog_procedure supplies the default once this call returns. */
        return (LONG)lx_execution_callback32_4(GProcessState.execution, classed->procedure, window,
                                               message, first, second);
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
        !lx_execution_read(GProcessState.execution, reader->address + reader->offset, wire, 2))
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
        /* PM text is byte text in the template's code page. */
        uint8 wide[2];
        char byte = '?';
        lx_guest_store_u16(wide, c);
        WideCharToMultiByte(437, 0, wide, 1, &byte, 1, NULL, NULL);
        text[count++] = byte;
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

typedef struct DialogMenu
{
    USHORT ordinal, class_ordinal;
    char name[256], class_name[256];
} DialogMenu;

/* Dialog units follow the template font: a quarter of its average width and an eighth of
   its height. Returns FALSE when PM's own units already apply. */
static BOOL template_font_units(const char *face, USHORT points, LONG *width, LONG *height)
{
    HPS space = WinGetPS(HWND_DESKTOP);
    if (!space)
        return FALSE;
    FATTRS attributes = {0};
    attributes.usRecordLength = sizeof attributes;
    snprintf(attributes.szFacename, sizeof attributes.szFacename, "%s", face);
    attributes.lMaxBaselineExt =
        (LONG)points * (LONG)(GProcessState.display_dpi ? GProcessState.display_dpi : 96) / 72;
    FONTMETRICS metrics;
    BOOL found = GpiCreateLogFont(space, NULL, 1, &attributes) > 0 && GpiSetCharSet(space, 1) &&
                 GpiQueryFontMetrics(space, sizeof metrics, &metrics) &&
                 metrics.lAveCharWidth > 0 && metrics.lMaxBaselineExt > 0;
    if (found)
    {
        *width = metrics.lAveCharWidth;
        *height = metrics.lMaxBaselineExt;
    }
    GpiSetCharSet(space, 0);
    GpiDeleteSetId(space, 1);
    WinReleasePS(space);
    return found;
}

static ULONG dialog_dword(DialogReader *reader)
{
    ULONG value = dialog_word(reader);
    return value | (ULONG)dialog_word(reader) << 16;
}

/* OS/2 applications load PM templates from their own resources and may hand them to
   the indirect dialog calls. PM's header has type 0 and items at offset 14. */
static PDLGTEMPLATE native_template(ULONG address)
{
    uint8 header[14];
    if (!lx_execution_read(GProcessState.execution, address, header, sizeof header))
        return NULL;
    ULONG size = lx_guest_load_u16(header);
    if (size < 44 || lx_guest_load_u16(header + 2) || lx_guest_load_u16(header + 6) != 14)
        return NULL;
    uint8 *bytes = calloc(65536, 1);
    if (bytes && !lx_execution_read(GProcessState.execution, address, bytes, size))
    {
        free(bytes);
        return NULL;
    }
    return (PDLGTEMPLATE)bytes;
}

static PDLGTEMPLATE convert_dialog(ULONG address, DialogMenu *menu)
{
    PDLGTEMPLATE native = native_template(address);
    if (native)
    {
        if (menu)
            memset(menu, 0, sizeof *menu);
        return native;
    }
    DialogReader reader = {address, 0, TRUE};
    ULONG style = dialog_dword(&reader);
    /* DLGTEMPLATEEX opens with version 1 and signature 0xFFFF, then help and extended style. */
    BOOL extended = style == 0xFFFF0001u;
    if (extended)
    {
        dialog_dword(&reader);
        dialog_dword(&reader);
        style = dialog_dword(&reader);
    }
    else
        dialog_dword(&reader);
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
    if (!dialog_string(&reader, text, &ordinal))
        goto convert_bad;
    if (menu)
    {
        menu->ordinal = ordinal;
        snprintf(menu->name, sizeof menu->name, "%s", text);
    }
    if (!dialog_string(&reader, class_name, &ordinal))
        goto convert_bad;
    if (menu)
    {
        menu->class_ordinal = ordinal;
        snprintf(menu->class_name, sizeof menu->class_name, "%s", class_name);
    }
    if (!dialog_string(&reader, text, &ordinal))
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
    unsigned font_parameters = 0xFFFF;
    LONG font_width = 0, font_height = 0;
    if (style & 0x40) /* DS_SETFONT */
    {
        USHORT points = dialog_word(&reader), weight = 0, italic = 0;
        if (extended)
        {
            weight = dialog_word(&reader);
            italic = dialog_word(&reader) & 0xFF;
        }
        if (!dialog_string(&reader, text, &ordinal))
            goto convert_bad;
        /* One PP_FONTNAMESIZE block serves the frame and every control. */
        char font[1100];
        int length = snprintf(font, sizeof font, "%u.%s%s%s", points, text,
                              weight >= 700 ? ".Bold" : "", italic ? ".Italic" : "");
        if (length < 0 || (size_t)length >= sizeof font || offset + length + 13 >= 65536)
            goto convert_bad;
        font_parameters = (unsigned)offset;
        lx_guest_store_u32(bytes + offset, (ULONG)length + 9);
        lx_guest_store_u32(bytes + offset + 4, 15);
        lx_guest_store_u32(bytes + offset + 8, (ULONG)length + 1);
        memcpy(bytes + offset + 12, font, (size_t)length + 1);
        offset += (size_t)length + 13;
        items[0].offPresParams = font_parameters;
        template_font_units(text, points, &font_width, &font_height);
    }
    for (ULONG i = 1; i <= count; i++)
    {
        reader.offset = (reader.offset + 3) & ~(size_t)3;
        DLGTITEM *item = items + i;
        if (extended)
        {
            dialog_dword(&reader);
            dialog_dword(&reader);
        }
        ULONG control_style = dialog_dword(&reader);
        if (!extended)
            dialog_dword(&reader);
        item->x = dialog_word(&reader);
        SHORT top = dialog_word(&reader);
        item->cx = dialog_word(&reader);
        item->cy = dialog_word(&reader);
        item->y = height - top - item->cy;
        item->id = dialog_word(&reader);
        if (extended)
            dialog_word(&reader);
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
        item->offCtlData = 0xFFFF;
        item->offPresParams = font_parameters;
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
            memcpy(bytes + offset, text, n + 1);
            offset += n + 1;
        }
        ULONG extra = dialog_word(&reader);
        if (extra)
            reader.offset += extended ? extra : extra - 2;
    }
    if (!reader.valid || offset > 65535)
        goto convert_bad;
    POINTL unit = {4, 8};
    if (font_width && WinMapDlgPoints(HWND_DESKTOP, &unit, 1, TRUE) && unit.x > 0 && unit.y > 0)
        for (ULONG i = 0; i <= count; i++)
        {
            DLGTITEM *item = items + i;
            item->x = (SHORT)((int64_t)item->x * font_width / unit.x);
            item->cx = (SHORT)((int64_t)item->cx * font_width / unit.x);
            item->y = (SHORT)((int64_t)item->y * font_height / unit.y);
            item->cy = (SHORT)((int64_t)item->cy * font_height / unit.y);
        }
    t->cbTemplate = offset;
    return t;
convert_bad:
    free(bytes);
    return NULL;
}

PVOID open32ConvertDialog(ULONG address)
{
    return convert_dialog(address, NULL);
}

static ULONG template_class_procedure(ULONG module, const DialogMenu *menu)
{
    if (!menu->class_ordinal && !menu->class_name[0])
        return 0;
    Open32Class *class = open32ClassAcquire(
        module, menu->class_ordinal ? (PSZ)(uintptr_t)menu->class_ordinal : (PSZ)menu->class_name);
    ULONG procedure = class ? class->procedure : 0;
    if (class)
        open32ClassRelease(class);
    return procedure;
}

static void attach_dialog_menu(ULONG module, ULONG window, const DialogMenu *menu)
{
    if (!menu->ordinal && !menu->name[0])
        return;
    ULONG handle =
        LoadMenu(module, menu->ordinal ? (PSZ)(uintptr_t)menu->ordinal : (PSZ)menu->name);
    if (handle)
        lx_pm_menu_attach(handle, window);
}

ULONG _CreateDialogIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure,
                                 ULONG parameter)
{
    if (!open32ValidProcedure(procedure) || !open32Presentation())
    {
        open32SetError(87);
        return 0;
    }
    DialogMenu menu = {0};
    PDLGTEMPLATE data = convert_dialog(address, &menu);
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
    dialog->class_procedure = template_class_procedure(module, &menu);
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
    attach_dialog_menu(module, window, &menu);
    return window;
}

LONG _DialogBoxIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure,
                             ULONG parameter)
{
    if (!open32ValidProcedure(procedure) || !open32Presentation())
    {
        open32SetError(87);
        return -1;
    }
    DialogMenu menu = {0};
    PDLGTEMPLATE data = convert_dialog(address, &menu);
    if (!data)
    {
        open32SetError(1813);
        return -1;
    }
    Open32Dialog dialog = {.procedure = procedure,
                           .class_procedure = template_class_procedure(module, &menu),
                           .parameter = parameter,
                           .previous = dialogs};
    dialogs = &dialog;
    ULONG window = WinCreateDlg(HWND_DESKTOP, owner, dialog_procedure, data, NULL);
    free(data);
    if (window)
        attach_dialog_menu(module, window, &menu);
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
