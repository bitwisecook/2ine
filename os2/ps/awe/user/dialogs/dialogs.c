/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 resource dialogs use native PM templates, controls and modal lifetime. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/pmgpi.h"
#include "os2/include/bsedos.h"
#include <stddef.h>
#include <stdio.h>

enum
{
    /* PM's packed dialog template: a DLGTEMPLATE header, then DLGTITEM records. Its offsets
       are USHORTs, so a template stays below 64 KB and 0xFFFF means "none". */
    TEMPLATE_HEADER = offsetof(DLGTEMPLATE, adlgti),
    TEMPLATE_ITEM = sizeof(DLGTITEM),
    TEMPLATE_TYPE = offsetof(DLGTEMPLATE, type),
    TEMPLATE_ITEMS_OFFSET = offsetof(DLGTEMPLATE, offadlgti),
    TEMPLATE_MAX = 65536,
    TEMPLATE_NONE = 0xFFFF,
    /* PM template text that is a resource ordinal: a 0xFF byte, then the USHORT ordinal. */
    TEXT_ORDINAL_MARKER = 0xFF,
    TEXT_ORDINAL_SIZE = 3,
    /* PRESPARAMS: a ULONG byte count, then PARAM entries of ULONG id and ULONG size. */
    PRESPARAMS_HEADER = 4,
    PARAM_HEADER = 8,
    /* The template's text and the PM font tables use the PC code page. */
    TEMPLATE_CODEPAGE = 437,
    /* Windows template limits on the guest stream and the control count. */
    WINDOWS_TEMPLATE_MAX = 1024 * 1024,
    WINDOWS_TEMPLATE_ITEMS_MAX = 1024,
    /* A Windows template string or class that is an ordinal starts with this word. */
    WINDOWS_TEMPLATE_ORDINAL = 0xFFFF,
    /* DLGTEMPLATEEX opens with version 1 and this signature. */
    DLGTEMPLATEEX_VERSION = 1,
    DLGTEMPLATEEX_SIGNATURE = 0xFFFF,
    /* The predefined window class atoms of a Windows template item. */
    WINDOWS_CLASS_BUTTON = 0x0080,
    WINDOWS_CLASS_EDIT = 0x0081,
    WINDOWS_CLASS_STATIC = 0x0082,
    WINDOWS_CLASS_LISTBOX = 0x0083,
    WINDOWS_CLASS_SCROLLBAR = 0x0084,
    WINDOWS_CLASS_COMBOBOX = 0x0085,
    /* A dialog unit is a quarter of the font's average width and an eighth of its height. */
    DIALOG_UNITS_PER_CHAR_X = 4,
    DIALOG_UNITS_PER_CHAR_Y = 8,
    DEFAULT_DPI = 96,
    POINTS_PER_INCH = 72,
    TEMPLATE_FONT_LCID = 1,
    /* PM frame controls (FID_*) have IDs from 0x8000 and keep their frame-relative place. */
    FIRST_FRAME_CONTROL_ID = 0x8000
};

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
        for (ULONG parent = handle; parent; parent = WinQueryWindow(parent, QW_PARENT))
            if (dialog->window && parent == dialog->window)
                return TRUE;
    for (Open32Dialog *dialog = modeless_dialogs; dialog; dialog = dialog->previous)
        for (ULONG parent = handle; parent; parent = WinQueryWindow(parent, QW_PARENT))
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
    for (HWND child = WinQueryWindow(dialog, QW_BOTTOM); child;
         child = WinQueryWindow(child, QW_PREV))
    {
        SWP position;
        if ((USHORT)WinQueryWindowUShort(child, QWS_ID) < FIRST_FRAME_CONTROL_ID &&
            WinQueryWindowPos(child, &position))
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
        windows_message = WIN32_WM_INITDIALOG;
        b = dialog->parameter;
    }
    else if (message == WM_DESTROY)
        windows_message = WIN32_WM_DESTROY;
    else if (message == WM_COMMAND || message == WM_CONTROL)
    {
        windows_message = WIN32_WM_COMMAND;
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    if (owner && !WinIsWindow(open32Presentation(), owner))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return -1;
    }
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    ULONG identity = open32ResourceIdentity(module, resource, RT_DIALOG);
    if (!identity)
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return -1;
    }
    Open32Dialog dialog = {.procedure = procedure, .parameter = parameter, .previous = dialogs};
    dialogs = &dialog;
    ULONG result = WinDlgBox(HWND_DESKTOP, owner, dialog_procedure, module, identity, NULL);
    dialogs = dialog.previous;
    open32NativeControlsPrune();
    if (dialog.finished)
        return dialog.result;
    if (result == DID_ERROR)
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
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
    open32SetError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
}

ULONG GetDlgItem(ULONG handle, LONG identity)
{
    TRACE_NATIVE("GetDlgItem(%u, %d)", (uint)handle, (int)identity);
    if (identity < 0 || identity > UINT16_MAX || !open32Presentation())
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return WinWindowFromID(open32ClientWindow(handle), (ULONG)identity);
}

BOOL SetDlgItemText(ULONG handle, LONG identity, PSZ text)
{
    ULONG child = GetDlgItem(handle, identity);
    if (!child || !text)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetWindowText(child, text);
}

ULONG GetDlgItemText(ULONG handle, LONG identity, PCHAR output, LONG capacity)
{
    ULONG child = GetDlgItem(handle, identity);
    if (!child || !output || capacity <= 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    return open32NativeControlMessage(child, message, first, second, FALSE);
}

ULONG IsDlgButtonChecked(ULONG dialog, LONG identity)
{
    return (ULONG)SendDlgItemMessage(dialog, identity, WIN32_BM_GETCHECK, 0, 0);
}

BOOL CheckDlgButton(ULONG dialog, LONG identity, ULONG check)
{
    ULONG child = GetDlgItem(dialog, identity);
    if (!child || check > WIN32_BST_INDETERMINATE)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SendDlgItemMessage(dialog, identity, WIN32_BM_SETCHECK, check, 0);
    return TRUE;
}

ULONG CreateDialogParam(ULONG module, PSZ resource, ULONG owner, ULONG procedure, ULONG parameter)
{
    if (!resource || !open32ValidProcedure(procedure) || !open32Presentation())
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    ULONG identity = open32ResourceIdentity(module, resource, RT_DIALOG);
    if (!identity)
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    Open32Dialog *dialog = calloc(1, sizeof *dialog);
    if (!dialog)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
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
    if (message == WIN32_WM_COMMAND || message == WIN32_WM_CLOSE)
        return (LONG)(uintptr_t)WinDefDlgProc(window,
                                              message == WIN32_WM_CLOSE ? WM_CLOSE : WM_COMMAND,
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
    if (!reader->valid || reader->offset > WINDOWS_TEMPLATE_MAX - 2 ||
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
    if (c == WINDOWS_TEMPLATE_ORDINAL)
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
        WideCharToMultiByte(TEMPLATE_CODEPAGE, 0, wide, 1, &byte, 1, NULL, NULL);
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
    if (*kind == PM_CLASS_COMBOBOX || *kind == PM_CLASS_BUTTON || *kind == PM_CLASS_STATIC ||
        *kind == PM_CLASS_ENTRYFIELD || *kind == PM_CLASS_LISTBOX || *kind == PM_CLASS_MLE)
        return open32ControlStyle(style, kind);
    /* Other classes keep their class-specific low word. */
    return (style & WIN32_WS_VISIBLE ? WS_VISIBLE : 0) |
           (style & WIN32_WS_DISABLED ? WS_DISABLED : 0) |
           (style & WIN32_WS_TABSTOP ? WS_TABSTOP : 0) | (style & WIN32_WS_GROUP ? WS_GROUP : 0) |
           (style & 0xFFFF);
}

typedef struct DialogMenu
{
    USHORT ordinal, class_ordinal;
    char name[256], class_name[256];
} DialogMenu;

/* Dialog units follow the template font. Returns FALSE when PM's own units already apply. */
static BOOL template_font_units(const char *face, USHORT points, LONG *width, LONG *height)
{
    HPS space = WinGetPS(HWND_DESKTOP);
    if (!space)
        return FALSE;
    FATTRS attributes = {0};
    attributes.usRecordLength = sizeof attributes;
    snprintf(attributes.szFacename, sizeof attributes.szFacename, "%s", face);
    attributes.lMaxBaselineExt =
        (LONG)points * (LONG)(GProcessState.display_dpi ? GProcessState.display_dpi : DEFAULT_DPI) /
        POINTS_PER_INCH;
    FONTMETRICS metrics;
    BOOL found = GpiCreateLogFont(space, NULL, TEMPLATE_FONT_LCID, &attributes) > 0 &&
                 GpiSetCharSet(space, TEMPLATE_FONT_LCID) &&
                 GpiQueryFontMetrics(space, sizeof metrics, &metrics) &&
                 metrics.lAveCharWidth > 0 && metrics.lMaxBaselineExt > 0;
    if (found)
    {
        *width = metrics.lAveCharWidth;
        *height = metrics.lMaxBaselineExt;
    }
    GpiSetCharSet(space, LCID_DEFAULT);
    GpiDeleteSetId(space, TEMPLATE_FONT_LCID);
    WinReleasePS(space);
    return found;
}

static ULONG dialog_dword(DialogReader *reader)
{
    ULONG value = dialog_word(reader);
    return value | (ULONG)dialog_word(reader) << 16;
}

/* OS/2 applications load PM templates from their own resources and may hand them to
   the indirect dialog calls. PM's header has type 0 and items straight after it. */
static PDLGTEMPLATE native_template(ULONG address)
{
    uint8 header[TEMPLATE_HEADER];
    if (!lx_execution_read(GProcessState.execution, address, header, sizeof header))
        return NULL;
    ULONG size = lx_guest_load_u16(header);
    if (size < TEMPLATE_HEADER + TEMPLATE_ITEM || lx_guest_load_u16(header + TEMPLATE_TYPE) ||
        lx_guest_load_u16(header + TEMPLATE_ITEMS_OFFSET) != TEMPLATE_HEADER)
        return NULL;
    uint8 *bytes = calloc(TEMPLATE_MAX, 1);
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
    /* DLGTEMPLATEEX continues with the help ID and extended style before the style. */
    BOOL extended = style == ((ULONG)DLGTEMPLATEEX_SIGNATURE << 16 | DLGTEMPLATEEX_VERSION);
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
    if (!reader.valid || count > WINDOWS_TEMPLATE_ITEMS_MAX || width < 0 || height < 0)
        return NULL;
    uint8 *bytes = calloc(TEMPLATE_MAX, 1);
    if (!bytes)
        return NULL;
    DLGTEMPLATE *t = (PDLGTEMPLATE)bytes;
    DLGTITEM *items = (DLGTITEM *)(bytes + TEMPLATE_HEADER);
    size_t offset = TEMPLATE_HEADER + (count + 1) * TEMPLATE_ITEM;
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
    t->codepage = TEMPLATE_CODEPAGE;
    t->offadlgti = TEMPLATE_HEADER;
    t->iItemFocus = TEMPLATE_NONE;
    items[0].cChildren = count;
    items[0].offClassName = PM_CLASS_FRAME;
    items[0].x = x;
    items[0].y = y;
    items[0].cx = width;
    items[0].cy = height;
    unsigned dialog_kind = PM_CLASS_FRAME;
    items[0].flStyle = dialog_pm_style(style, &dialog_kind);
    items[0].cchText = strlen(text);
    items[0].offText = offset;
    memcpy(bytes + offset, text, strlen(text) + 1);
    offset += strlen(text) + 1;
    items[0].offPresParams = TEMPLATE_NONE;
    /* The frame's control data is its FCF_* creation flags. */
    items[0].offCtlData = offset;
    ULONG frame = (style & WIN32_WS_CAPTION ? FCF_TITLEBAR : 0) |
                  (style & WIN32_WS_SYSMENU ? FCF_SYSMENU : 0) |
                  (style & WIN32_WS_THICKFRAME ? FCF_SIZEBORDER : 0);
    lx_guest_store_u32(bytes + offset, frame);
    offset += 4;
    unsigned font_parameters = TEMPLATE_NONE;
    LONG font_width = 0, font_height = 0;
    if (style & WIN32_DS_SETFONT)
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
                              weight >= WIN32_FW_BOLD ? ".Bold" : "", italic ? ".Italic" : "");
        if (length < 0 || (size_t)length >= sizeof font ||
            offset + length + (PRESPARAMS_HEADER + PARAM_HEADER + 1) >= TEMPLATE_MAX)
            goto convert_bad;
        font_parameters = (unsigned)offset;
        lx_guest_store_u32(bytes + offset, (ULONG)length + (PARAM_HEADER + 1));
        lx_guest_store_u32(bytes + offset + PRESPARAMS_HEADER, PP_FONTNAMESIZE);
        lx_guest_store_u32(bytes + offset + PRESPARAMS_HEADER + 4, (ULONG)length + 1);
        memcpy(bytes + offset + PRESPARAMS_HEADER + PARAM_HEADER, font, (size_t)length + 1);
        offset += (size_t)length + (PRESPARAMS_HEADER + PARAM_HEADER + 1);
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
        unsigned kind =
            ordinal == WINDOWS_CLASS_BUTTON ? PM_CLASS_BUTTON
            : ordinal == WINDOWS_CLASS_EDIT
                ? ((control_style & WIN32_ES_MULTILINE) ? PM_CLASS_MLE : PM_CLASS_ENTRYFIELD)
            : ordinal == WINDOWS_CLASS_STATIC    ? PM_CLASS_STATIC
            : ordinal == WINDOWS_CLASS_LISTBOX   ? PM_CLASS_LISTBOX
            : ordinal == WINDOWS_CLASS_SCROLLBAR ? PM_CLASS_SCROLLBAR
            : ordinal == WINDOWS_CLASS_COMBOBOX  ? PM_CLASS_COMBOBOX
                                                 : 0;
        if (!kind)
        {
            if (!strcasecmp(class_name, "BUTTON"))
                kind = PM_CLASS_BUTTON;
            else if (!strcasecmp(class_name, "EDIT"))
                kind = PM_CLASS_ENTRYFIELD;
            else if (!strcasecmp(class_name, "STATIC"))
                kind = PM_CLASS_STATIC;
            else if (!strcasecmp(class_name, "LISTBOX"))
                kind = PM_CLASS_LISTBOX;
            else if (!strcasecmp(class_name, "COMBOBOX"))
                kind = PM_CLASS_COMBOBOX;
            else if (!strcasecmp(class_name, "SCROLLBAR"))
                kind = PM_CLASS_SCROLLBAR;
        }
        item->flStyle = dialog_pm_style(control_style, &kind);
        if (kind)
            item->offClassName = kind;
        else
        {
            size_t n = strlen(class_name);
            if (!n || offset + n + 1 >= TEMPLATE_MAX)
                goto convert_bad;
            item->cchClassName = n;
            item->offClassName = offset;
            memcpy(bytes + offset, class_name, n + 1);
            offset += n + 1;
        }
        item->offCtlData = TEMPLATE_NONE;
        item->offPresParams = font_parameters;
        if (!dialog_string(&reader, text, &ordinal))
            goto convert_bad;
        size_t n = strlen(text);
        if (offset + n + 4 >= TEMPLATE_MAX)
            goto convert_bad;
        item->offText = offset;
        if (ordinal)
        {
            item->cchText = TEXT_ORDINAL_SIZE;
            bytes[offset] = TEXT_ORDINAL_MARKER;
            lx_guest_store_u16(bytes + offset + 1, ordinal);
            offset += TEXT_ORDINAL_SIZE;
        }
        else
        {
            item->cchText = n;
            memcpy(bytes + offset, text, n + 1);
            offset += n + 1;
        }
        /* A DLGTEMPLATE item's creation data size counts its own size word; a
           DLGITEMTEMPLATEEX's does not. */
        ULONG extra = dialog_word(&reader);
        if (extra)
            reader.offset += extended ? extra : extra - 2;
    }
    if (!reader.valid || offset > TEMPLATE_MAX - 1)
        goto convert_bad;
    POINTL unit = {DIALOG_UNITS_PER_CHAR_X, DIALOG_UNITS_PER_CHAR_Y};
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    DialogMenu menu = {0};
    PDLGTEMPLATE data = convert_dialog(address, &menu);
    if (!data)
    {
        open32SetError(ERROR_RESOURCE_TYPE_NOT_FOUND);
        return 0;
    }
    Open32Dialog *dialog = calloc(1, sizeof *dialog);
    if (!dialog)
    {
        free(data);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(ERROR_RESOURCE_TYPE_NOT_FOUND);
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    DialogMenu menu = {0};
    PDLGTEMPLATE data = convert_dialog(address, &menu);
    if (!data)
    {
        open32SetError(ERROR_RESOURCE_TYPE_NOT_FOUND);
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
