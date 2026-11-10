/* Public domain. No rights reserved. Wholly AI-authored. */
/* ANSI common file dialogs preserve the caller's native template and Windows hook. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/include/private/pmp.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/bsedos.h"
#include "os2/ps/awe/win32.h"
#include <stdio.h>

enum
{
    FILTER_LIMIT = 128,
    /* One mapping holds the FILEDLG, the type-list pointers and their strings. */
    DIALOG_BYTES = 65536,
    FILTER_TEXT_LIMIT = 1024,
    DEFAULT_EXTENSION_LIMIT = 256,
    CAPTION_LIMIT = 32768,
    /* Reject nMaxFile values no real caller uses before probing guest memory. */
    FILE_BUFFER_LIMIT = 1048576,
    /* The shortest directory that keeps its separator: "C:\\". */
    DRIVE_ROOT_LENGTH = 3
};

/* PM USERBUTTON, received with BN_PAINT. */
enum
{
    USERBUTTON_WINDOW = 0,
    USERBUTTON_STATE = 8,
    USERBUTTON_SIZE = 16
};

typedef struct FileDialog
{
    struct FileDialog *previous;
    ULONG address, window, hook, flags, count, selected;
    ULONG file_ok, list_changed, help;
    LxGuestMapping *mapping, *procedure;
    LXFILEDLG *native;
    char *patterns[FILTER_LIMIT];
    BOOL save, accepted;
} FileDialog;

static _Thread_local FileDialog *file_dialogs;
static _Thread_local ULONG common_error;

ULONG CommDlgExtendedError(void)
{
    return common_error;
}

void open32CommonDialogError(ULONG error)
{
    common_error = error;
}

BOOL open32FileDialogWindow(ULONG handle)
{
    for (FileDialog *dialog = file_dialogs; dialog; dialog = dialog->previous)
        for (ULONG parent = handle; parent; parent = WinQueryWindow(parent, QW_PARENT))
            if (dialog->window && parent == dialog->window)
                return TRUE;
    return FALSE;
}

static BOOL fail(ULONG error)
{
    common_error = error;
    return FALSE;
}

static BOOL read_record(FileDialog *dialog, uint8 record[WIN32_OPENFILENAME_SIZE])
{
    return lx_execution_read(GProcessState.execution, dialog->address, record,
                             WIN32_OPENFILENAME_SIZE);
}

static char *copy_text(ULONG address, size_t capacity)
{
    return address ? lx_execution_copy_string(GProcessState.execution, address, capacity) : NULL;
}

static ULONG add_text(FileDialog *dialog, size_t *offset, const char *text)
{
    size_t size = strlen(text) + 1;
    if (*offset > DIALOG_BYTES || size > DIALOG_BYTES - *offset)
        return 0;
    ULONG address = lx_execution_mapping_address(dialog->mapping) + (ULONG)*offset;
    memcpy((uint8 *)lx_execution_mapping_host(dialog->mapping) + *offset, text, size);
    *offset += size;
    return address;
}

static ULONG hook(FileDialog *dialog, ULONG message, ULONG first, ULONG second)
{
    return dialog->hook ? lx_execution_callback32_4(GProcessState.execution, dialog->hook,
                                                    dialog->window, message, first, second)
                        : 0;
}

static void select_type(FileDialog *dialog)
{
    LONG index = (LONG)(intptr_t)WinSendDlgItemMsg(dialog->window, DID_FILTER_CB, LM_QUERYSELECTION,
                                                   (MPARAM)(intptr_t)LIT_FIRST, 0);
    if (index < 0 || (ULONG)index >= dialog->count)
        return;
    dialog->selected = (ULONG)index;
    uint8 value[4];
    lx_guest_store_u32(value, dialog->selected + 1);
    lx_execution_write(GProcessState.execution, dialog->address + WIN32_OPENFILENAME_FILTER_INDEX,
                       value, sizeof value);
    lx_pm_file_set_filter(dialog->window, dialog->patterns[index]);
}

static BOOL publish_name(FileDialog *dialog, char full[CCHMAXPATH])
{
    uint8 record[WIN32_OPENFILENAME_SIZE];
    if (!read_record(dialog, record))
        return fail(WIN32_CDERR_DIALOGFAILURE);
    ULONG pointer = lx_guest_load_u32(record + WIN32_OPENFILENAME_FILE),
          capacity = lx_guest_load_u32(record + WIN32_OPENFILENAME_MAX_FILE);
    char *base = strrchr(full, '\\');
    base = base ? base + 1 : full;
    char *extension = strrchr(base, '.');
    char *defext =
        copy_text(lx_guest_load_u32(record + WIN32_OPENFILENAME_DEF_EXT), DEFAULT_EXTENSION_LIMIT);
    if (!extension && defext && *defext)
    {
        size_t length = strlen(full), extra = strcspn(defext, ".; ");
        if (extra && length + extra + 2 <= CCHMAXPATH)
        {
            full[length++] = '.';
            memcpy(full + length, defext, extra);
            full[length + extra] = 0;
            extension = full + length - 1;
        }
    }
    ULONG flags =
        lx_guest_load_u32(record + WIN32_OPENFILENAME_FLAGS) & ~WIN32_OFN_EXTENSIONDIFFERENT;
    if (extension && defext && strcasecmp(extension + 1, defext))
        flags |= WIN32_OFN_EXTENSIONDIFFERENT;
    free(defext);
    size_t length = strlen(full);
    if (length + 1 > capacity)
    {
        uint8 size[2];
        lx_guest_store_u16(size, (uint16)(length + 1));
        if (capacity >= 2)
            lx_execution_write(GProcessState.execution, pointer, size, sizeof size);
        common_error = WIN32_FNERR_BUFFERTOOSMALL;
        WinDismissDlg(dialog->window, 0);
        return FALSE;
    }
    ULONG attributes = GetFileAttributes(full);
    if ((flags & WIN32_OFN_FILEMUSTEXIST) && attributes == UINT32_MAX)
    {
        WinSetDlgItemText(dialog->window, DID_DIRECTORY_SELECTED, "The file does not exist");
        return FALSE;
    }
    if ((flags & WIN32_OFN_NOREADONLYRETURN) && attributes != UINT32_MAX &&
        (attributes & WIN32_FILE_ATTRIBUTE_READONLY))
    {
        WinSetDlgItemText(dialog->window, DID_DIRECTORY_SELECTED, "The file is read only");
        return FALSE;
    }
    if (flags & WIN32_OFN_PATHMUSTEXIST)
    {
        char parent[CCHMAXPATH];
        memcpy(parent, full, length + 1);
        size_t part = (size_t)(base - full);
        parent[part > DRIVE_ROOT_LENGTH ? part - 1 : part] = 0;
        ULONG attr = GetFileAttributes(parent);
        if (attr == UINT32_MAX || !(attr & WIN32_FILE_ATTRIBUTE_DIRECTORY))
        {
            WinSetDlgItemText(dialog->window, DID_DIRECTORY_SELECTED, "The folder does not exist");
            return FALSE;
        }
    }
    const char *question = NULL;
    if (dialog->save && (flags & WIN32_OFN_OVERWRITEPROMPT) && attributes != UINT32_MAX)
        question = "The file already exists. Replace it?";
    else if ((flags & WIN32_OFN_CREATEPROMPT) && attributes == UINT32_MAX)
        question = "The file does not exist. Create it?";
    if (question &&
        WinMessageBox(HWND_DESKTOP, dialog->window, (PSZ)question,
                      dialog->save ? "Save As" : "Open", 0, MB_YESNO | MB_QUERY) != MBID_YES)
        return FALSE;
    if (!lx_execution_write(GProcessState.execution, pointer, full, length + 1))
        return fail(WIN32_CDERR_DIALOGFAILURE);
    ULONG title = lx_guest_load_u32(record + WIN32_OPENFILENAME_FILE_TITLE),
          title_capacity = lx_guest_load_u32(record + WIN32_OPENFILENAME_MAX_FILE_TITLE);
    if (title && title_capacity)
    {
        size_t count = strlen(base);
        if (count >= title_capacity)
            count = title_capacity - 1;
        if (!lx_execution_write(GProcessState.execution, title, base, count) ||
            !lx_execution_write(GProcessState.execution, title + (ULONG)count, "", 1))
            return fail(WIN32_CDERR_DIALOGFAILURE);
    }
    lx_guest_store_u32(record + WIN32_OPENFILENAME_FILTER_INDEX,
                       dialog->count ? dialog->selected + 1 : 0);
    lx_guest_store_u32(record + WIN32_OPENFILENAME_FLAGS, flags);
    lx_guest_store_u16(record + WIN32_OPENFILENAME_FILE_OFFSET, (uint16)(base - full));
    lx_guest_store_u16(record + WIN32_OPENFILENAME_FILE_EXTENSION,
                       extension ? (uint16)(extension + 1 - full) : 0);
    /* Flags, nFileOffset and nFileExtension are contiguous: write all three at once. */
    if (!lx_execution_write(GProcessState.execution,
                            dialog->address + WIN32_OPENFILENAME_FILTER_INDEX,
                            record + WIN32_OPENFILENAME_FILTER_INDEX, 4) ||
        !lx_execution_write(GProcessState.execution, dialog->address + WIN32_OPENFILENAME_FLAGS,
                            record + WIN32_OPENFILENAME_FLAGS,
                            WIN32_OPENFILENAME_DEF_EXT - WIN32_OPENFILENAME_FLAGS))
        return fail(WIN32_CDERR_DIALOGFAILURE);
    if (hook(dialog, dialog->file_ok, 0, dialog->address))
        return FALSE;
    dialog->accepted = TRUE;
    return TRUE;
}

static ULONG draw_button(FileDialog *dialog, ULONG id, ULONG pointer)
{
    uint8 button[USERBUTTON_SIZE], item[WIN32_DRAWITEMSTRUCT_SIZE] = {0};
    if (!lx_execution_read(GProcessState.execution, pointer, button, sizeof button))
        return 0;
    ULONG window = lx_guest_load_u32(button + USERBUTTON_WINDOW),
          state = lx_guest_load_u32(button + USERBUTTON_STATE);
    RECTL rect;
    if (!WinQueryWindowRect(window, &rect))
        return 0;
    ULONG dc = GetDC(window);
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                               LX_GUEST_READ | LX_GUEST_WRITE, 0);
    ULONG result = 0;
    if (dc && mapping)
    {
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_CONTROL_TYPE, WIN32_ODT_BUTTON);
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_CONTROL_ID, id);
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_ACTION, WIN32_ODA_DRAWENTIRE);
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_STATE,
                           (state & BDS_HILITED ? WIN32_ODS_SELECTED : 0) |
                               (state & BDS_DISABLED ? WIN32_ODS_DISABLED : 0) |
                               (state & BDS_DEFAULT ? WIN32_ODS_DEFAULT : 0) |
                               (WinQueryFocus(HWND_DESKTOP) == window ? WIN32_ODS_FOCUS : 0));
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_WINDOW, window);
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_DC, dc);
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_RIGHT, (ULONG)(rect.xRight - rect.xLeft));
        lx_guest_store_u32(item + WIN32_DRAWITEMSTRUCT_BOTTOM, (ULONG)(rect.yTop - rect.yBottom));
        memcpy(lx_execution_mapping_host(mapping), item, sizeof item);
        result = hook(dialog, WIN32_WM_DRAWITEM, id, lx_execution_mapping_address(mapping));
    }
    if (mapping)
        lx_execution_unmap(GProcessState.execution, mapping);
    if (dc)
        ReleaseDC(window, dc);
    return result;
}

static uint32 file_dispatch(uint8 *arguments)
{
    ULONG handle = lx_guest_load_u32(arguments), message = lx_guest_load_u32(arguments + 4);
    ULONG first = lx_guest_load_u32(arguments + 8), second = lx_guest_load_u32(arguments + 12);
    FileDialog *dialog = file_dialogs;
    while (dialog && dialog->window && dialog->window != handle)
        dialog = dialog->previous;
    if (!dialog)
        return (ULONG)(uintptr_t)lx_pm_file_procedure(handle, message, (MPARAM)(uintptr_t)first,
                                                      (MPARAM)(uintptr_t)second);
    if (message == WM_INITDLG)
    {
        dialog->window = handle;
        if (dialog->count)
            WinSendDlgItemMsg(handle, DID_FILTER_CB, LM_SELECTITEM,
                              (MPARAM)(uintptr_t)dialog->selected, (MPARAM)(uintptr_t)TRUE);
        if (dialog->flags & WIN32_OFN_HIDEREADONLY)
            WinShowWindow(WinWindowFromID(handle, DID_READ_ONLY), FALSE);
        if (!(dialog->flags & WIN32_OFN_SHOWHELP))
            WinShowWindow(WinWindowFromID(handle, DID_HELP_PB), FALSE);
        hook(dialog, WIN32_WM_INITDIALOG, first, dialog->address);
        select_type(dialog);
        return 0;
    }
    if (message == FDM_VALIDATE)
    {
        char full[CCHMAXPATH];
        if (!lx_execution_read(GProcessState.execution, first, full, sizeof full) ||
            !memchr(full, 0, sizeof full) || !publish_name(dialog, full))
            return FALSE;
        return lx_execution_write(GProcessState.execution, first, full, strlen(full) + 1);
    }
    if (message == WM_COMMAND || message == WM_CONTROL)
    {
        ULONG child = WinWindowFromID(handle, first & 0xffff);
        char name[16];
        /* A BS_USERBUTTON in a custom template is owner-drawn by the Windows hook. */
        if (message == WM_CONTROL && first >> 16 == BN_PAINT &&
            WinQueryClassName(child, sizeof name, name) && !strcmp(name, "#3") &&
            (WinQueryWindowULong(child, QWL_STYLE) & BS_PRIMARYSTYLES) == BS_USERBUTTON)
            return draw_button(dialog, first & 0xffff, second);
        if (hook(dialog, WIN32_WM_COMMAND,
                 message == WM_CONTROL ? open32ControlNotification(child, first) : first, child))
            return 0;
        if (message == WM_CONTROL && (first & 0xffff) == DID_FILTER_CB &&
            first >> 16 == CBN_LBSELECT)
            select_type(dialog);
        if (message == WM_COMMAND && (first & 0xffff) == DID_HELP_PB)
        {
            if (dialog->native->ulUser)
                SendMessage(dialog->native->ulUser, dialog->help, handle, dialog->address);
            return 0;
        }
    }
    else if (message == WM_DESTROY)
        hook(dialog, WIN32_WM_DESTROY, 0, 0);
    else
    {
        QMSG native = {.hwnd = handle,
                       .msg = message,
                       .mp1 = (MPARAM)(uintptr_t)first,
                       .mp2 = (MPARAM)(uintptr_t)second};
        ULONG translated, a, b;
        if (open32PmMessage(&native, &translated, &a, &b) && hook(dialog, translated, a, b))
            return 0;
    }
    return (ULONG)(uintptr_t)lx_pm_file_procedure(handle, message, (MPARAM)(uintptr_t)first,
                                                  (MPARAM)(uintptr_t)second);
}

static BOOL common_dialog(PVOID address, BOOL save)
{
    common_error = 0;
    uintptr_t pointer = (uintptr_t)address;
    uint8 record[WIN32_OPENFILENAME_SIZE];
    if (pointer > UINT32_MAX ||
        !lx_execution_read(GProcessState.execution, (ULONG)pointer, record, sizeof record))
        return fail(WIN32_CDERR_STRUCTSIZE);
    ULONG size = lx_guest_load_u32(record),
          flags = lx_guest_load_u32(record + WIN32_OPENFILENAME_FLAGS);
    TRACE_NATIVE("Open32 file dialog size=%u flags=%08x custom=%08x", (uint)size, (uint)flags,
                 (uint)lx_guest_load_u32(record + WIN32_OPENFILENAME_CUSTOM_FILTER));
    if (size != WIN32_OPENFILENAME_SIZE)
        return fail(WIN32_CDERR_STRUCTSIZE);
    if (!lx_execution_memory(GProcessState.execution, (ULONG)pointer, size, LX_GUEST_WRITE))
        return fail(WIN32_CDERR_DIALOGFAILURE);
    /* The picker offers one file. ALLOWMULTISELECT callers receive the ordinary
       full-path result required for a single selection. */
    ULONG supported = WIN32_OFN_READONLY | WIN32_OFN_OVERWRITEPROMPT | WIN32_OFN_HIDEREADONLY |
                      WIN32_OFN_NOCHANGEDIR | WIN32_OFN_SHOWHELP | WIN32_OFN_ENABLEHOOK |
                      WIN32_OFN_ENABLETEMPLATE | WIN32_OFN_ALLOWMULTISELECT |
                      WIN32_OFN_EXTENSIONDIFFERENT | WIN32_OFN_PATHMUSTEXIST |
                      WIN32_OFN_FILEMUSTEXIST | WIN32_OFN_CREATEPROMPT |
                      WIN32_OFN_NOREADONLYRETURN | WIN32_OFN_NOTESTFILECREATE |
                      WIN32_OFN_NONETWORKBUTTON | WIN32_OFN_NOLONGNAMES;
    if (flags & ~supported || lx_guest_load_u32(record + WIN32_OPENFILENAME_CUSTOM_FILTER))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return fail(WIN32_CDERR_DIALOGFAILURE);
    }
    ULONG callback =
        flags & WIN32_OFN_ENABLEHOOK ? lx_guest_load_u32(record + WIN32_OPENFILENAME_HOOK) : 0;
    if ((flags & WIN32_OFN_ENABLEHOOK) && !open32ValidProcedure(callback))
        return fail(WIN32_CDERR_NOHOOK);
    ULONG file = lx_guest_load_u32(record + WIN32_OPENFILENAME_FILE),
          capacity = lx_guest_load_u32(record + WIN32_OPENFILENAME_MAX_FILE);
    ULONG title = lx_guest_load_u32(record + WIN32_OPENFILENAME_FILE_TITLE),
          title_capacity = lx_guest_load_u32(record + WIN32_OPENFILENAME_MAX_FILE_TITLE);
    if (!capacity || capacity > FILE_BUFFER_LIMIT ||
        !lx_execution_memory(GProcessState.execution, file, capacity,
                             LX_GUEST_READ | LX_GUEST_WRITE) ||
        (title && title_capacity &&
         !lx_execution_memory(GProcessState.execution, title, title_capacity, LX_GUEST_WRITE)))
        return fail(WIN32_FNERR_INVALIDFILENAME);
    char *filename = copy_text(file, capacity),
         *directory =
             copy_text(lx_guest_load_u32(record + WIN32_OPENFILENAME_INITIAL_DIR), CCHMAXPATH);
    char *caption = copy_text(lx_guest_load_u32(record + WIN32_OPENFILENAME_TITLE), CAPTION_LIMIT);
    if (!filename || (lx_guest_load_u32(record + WIN32_OPENFILENAME_INITIAL_DIR) && !directory) ||
        (lx_guest_load_u32(record + WIN32_OPENFILENAME_TITLE) && !caption))
    {
        free(filename);
        free(directory);
        free(caption);
        return fail(WIN32_FNERR_INVALIDFILENAME);
    }
    FileDialog dialog = {.address = (ULONG)pointer,
                         .hook = callback,
                         .flags = flags,
                         .save = save,
                         .previous = file_dialogs};
    dialog.mapping = lx_execution_map(GProcessState.execution, 0, DIALOG_BYTES,
                                      LX_GUEST_READ | LX_GUEST_WRITE, 0);
    dialog.procedure = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                        LX_GUEST_READ | LX_GUEST_WRITE, 0);
    BOOL result = FALSE;
    if (!dialog.mapping || !dialog.procedure)
    {
        common_error = WIN32_CDERR_MEMALLOCFAILURE;
        goto cleanup;
    }
    dialog.native = lx_execution_mapping_host(dialog.mapping);
    memset(dialog.native, 0, sizeof *dialog.native);
    dialog.native->cbSize = sizeof *dialog.native;
    dialog.native->fl = save ? FDS_SAVEAS_DIALOG : FDS_OPEN_DIALOG;
    dialog.native->ulUser = lx_guest_load_u32(record + WIN32_OPENFILENAME_OWNER);
    size_t offset = sizeof *dialog.native + (FILTER_LIMIT + 1) * 4;
    ULONG filter = lx_guest_load_u32(record + WIN32_OPENFILENAME_FILTER);
    uint8 *types = (uint8 *)dialog.native + sizeof *dialog.native;
    while (filter && dialog.count < FILTER_LIMIT)
    {
        char *description = copy_text(filter, FILTER_TEXT_LIMIT);
        if (!description)
        {
            common_error = WIN32_FNERR_INVALIDFILENAME;
            goto cleanup;
        }
        if (!*description)
        {
            free(description);
            break;
        }
        filter += (ULONG)strlen(description) + 1;
        char *pattern = copy_text(filter, CCHMAXPATH);
        ULONG text = add_text(&dialog, &offset, description);
        free(description);
        if (!pattern || !*pattern || !text)
        {
            free(pattern);
            common_error = WIN32_FNERR_INVALIDFILENAME;
            goto cleanup;
        }
        filter += (ULONG)strlen(pattern) + 1;
        lx_guest_store_u32(types + dialog.count * 4, text);
        dialog.patterns[dialog.count++] = pattern;
    }
    lx_guest_store_u32(types + dialog.count * 4, 0);
    if (dialog.count)
    {
        ULONG selected = lx_guest_load_u32(record + WIN32_OPENFILENAME_FILTER_INDEX);
        dialog.selected = selected && selected <= dialog.count ? selected - 1 : 0;
        dialog.native->papszITypeList =
            lx_execution_mapping_address(dialog.mapping) + sizeof *dialog.native;
        dialog.native->pszIType = lx_guest_load_u32(types + dialog.selected * 4);
    }
    if (caption)
        dialog.native->pszTitle = add_text(&dialog, &offset, caption);
    int length;
    if (*filename && (strchr(filename, ':') || *filename == '\\' || *filename == '/'))
        length = snprintf(dialog.native->szFullFile, CCHMAXPATH, "%s", filename);
    else
        length =
            snprintf(dialog.native->szFullFile, CCHMAXPATH, "%s%s%s", directory ? directory : "",
                     directory && *directory && directory[strlen(directory) - 1] != '\\' &&
                             directory[strlen(directory) - 1] != '/'
                         ? "\\"
                         : "",
                     filename);
    if (length < 0 || length >= CCHMAXPATH)
    {
        common_error = WIN32_FNERR_INVALIDFILENAME;
        goto cleanup;
    }
    if (flags & WIN32_OFN_ENABLETEMPLATE)
    {
        ULONG resource = lx_guest_load_u32(record + WIN32_OPENFILENAME_TEMPLATE_NAME);
        if (!resource || resource > UINT16_MAX)
        {
            common_error = WIN32_CDERR_NOTEMPLATE;
            goto cleanup;
        }
        dialog.native->fl |= FDS_CUSTOM;
        dialog.native->hMod = lx_guest_load_u32(record + WIN32_OPENFILENAME_INSTANCE);
        if ((!dialog.native->hMod || dialog.native->hMod == UINT32_MAX) &&
            GProcessState.main_module)
            dialog.native->hMod = GProcessState.main_module->handle;
        dialog.native->usDlgId = (USHORT)resource;
    }
    const LxExecutionApi32 api = {file_dispatch, 16, 0, LX_ABI_STACK32};
    if (!lx_execution_emit_bridge32(GProcessState.execution,
                                    lx_execution_mapping_host(dialog.procedure), OPEN32_PAGE_SIZE,
                                    &api) ||
        !lx_execution_protect(GProcessState.execution, dialog.procedure,
                              LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        common_error = WIN32_CDERR_MEMALLOCFAILURE;
        goto cleanup;
    }
    dialog.native->pfnDlgProc = lx_execution_mapping_address(dialog.procedure);
    dialog.file_ok = RegisterWindowMessage("commdlg_FileNameOK");
    dialog.list_changed = RegisterWindowMessage("commdlg_LBSelChangedNotify");
    dialog.help = RegisterWindowMessage("commdlg_help");
    if (!open32Presentation())
    {
        common_error = WIN32_CDERR_INITIALIZATION;
        goto cleanup;
    }
    file_dialogs = &dialog;
    HWND handle = lx_pm_file_dialog(HWND_DESKTOP, dialog.native->ulUser,
                                    (PVOID)(uintptr_t)lx_execution_mapping_address(dialog.mapping));
    file_dialogs = dialog.previous;
    open32NativeControlsPrune();
    if (!handle && !common_error)
        common_error = WIN32_CDERR_DIALOGFAILURE;
    result = dialog.accepted && dialog.native->lReturn == DID_OK;
cleanup:
    for (ULONG i = 0; i < dialog.count; i++)
        free(dialog.patterns[i]);
    if (dialog.procedure)
        lx_execution_unmap(GProcessState.execution, dialog.procedure);
    if (dialog.mapping)
        lx_execution_unmap(GProcessState.execution, dialog.mapping);
    free(filename);
    free(directory);
    free(caption);
    return result;
}

BOOL GetSaveFileName(PVOID address)
{
    TRACE_NATIVE("GetSaveFileName(%p)", address);
    return common_dialog(address, TRUE);
}

BOOL GetOpenFileName(PVOID address)
{
    TRACE_NATIVE("GetOpenFileName(%p)", address);
    return common_dialog(address, FALSE);
}

SHORT GetFileTitle(PSZ path, PSZ output, USHORT capacity)
{
    if (!path || !output)
        return -1;
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            name = p + 1;
    size_t length = strlen(name) + 1;
    if (!*name || strpbrk(name, "*?"))
        return -1;
    if (length > capacity)
        return (SHORT)(length > INT16_MAX ? INT16_MAX : length);
    memcpy(output, name, length);
    return 0;
}
