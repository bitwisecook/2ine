/* Public domain. No rights reserved. Wholly AI-authored. */
/* ANSI common file dialogs preserve the caller's native template and Windows hook. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/input.h"
#include "os2libs/pmwin/standard_dialogs.h"
#include "os2libs/pmctls.h"
#include "os2libs/doscalls.h"
#include <stdio.h>

enum
{
    OFN_READONLY = 1,
    OFN_OVERWRITEPROMPT = 2,
    OFN_HIDEREADONLY = 4,
    OFN_NOCHANGEDIR = 8,
    OFN_SHOWHELP = 0x10,
    OFN_ENABLEHOOK = 0x20,
    OFN_ENABLETEMPLATE = 0x40,
    OFN_ALLOWMULTISELECT = 0x200,
    OFN_EXTENSIONDIFFERENT = 0x400,
    OFN_PATHMUSTEXIST = 0x800,
    OFN_FILEMUSTEXIST = 0x1000,
    OFN_CREATEPROMPT = 0x2000,
    OFN_NOREADONLYRETURN = 0x8000,
    OFN_NOTESTFILECREATE = 0x10000,
    OFN_NONETWORKBUTTON = 0x20000,
    OFN_NOLONGNAMES = 0x40000,
    OPENFILENAME_SIZE = 76,
    FILTER_LIMIT = 128,
    DIALOG_BYTES = 65536
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
BOOL open32FileDialogWindow(ULONG handle)
{
    for (FileDialog *dialog = file_dialogs; dialog; dialog = dialog->previous)
        for (ULONG parent = handle; parent; parent = WinQueryWindow(parent, 5))
            if (dialog->window && parent == dialog->window)
                return TRUE;
    return FALSE;
}
static BOOL fail(ULONG error)
{
    common_error = error;
    return FALSE;
}
static BOOL read_record(FileDialog *dialog, uint8 record[OPENFILENAME_SIZE])
{
    return lx_execution_read(GLoaderState.execution, dialog->address, record, OPENFILENAME_SIZE);
}
static char *copy_text(ULONG address, size_t capacity)
{
    return address ? lx_execution_copy_string(GLoaderState.execution, address, capacity) : NULL;
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
    return dialog->hook ? lx_execution_callback32_4(GLoaderState.execution, dialog->hook,
                                                    dialog->window, message, first, second)
                        : 0;
}
static void select_type(FileDialog *dialog)
{
    LONG index =
        (LONG)(intptr_t)WinSendDlgItemMsg(dialog->window, 262, 0x165, (MPARAM)(intptr_t)-1, 0);
    if (index < 0 || (ULONG)index >= dialog->count)
        return;
    dialog->selected = (ULONG)index;
    uint8 value[4];
    lx_guest_store_u32(value, dialog->selected + 1);
    lx_execution_write(GLoaderState.execution, dialog->address + 24, value, sizeof value);
    lx_pm_file_set_filter(dialog->window, dialog->patterns[index]);
}
static BOOL publish_name(FileDialog *dialog, char full[260])
{
    uint8 record[OPENFILENAME_SIZE];
    if (!read_record(dialog, record))
        return fail(0xffff);
    ULONG pointer = lx_guest_load_u32(record + 28), capacity = lx_guest_load_u32(record + 32);
    char *base = strrchr(full, '\\');
    base = base ? base + 1 : full;
    char *extension = strrchr(base, '.');
    char *defext = copy_text(lx_guest_load_u32(record + 60), 256);
    if (!extension && defext && *defext)
    {
        size_t length = strlen(full), extra = strcspn(defext, ".; ");
        if (extra && length + extra + 2 <= 260)
        {
            full[length++] = '.';
            memcpy(full + length, defext, extra);
            full[length + extra] = 0;
            extension = full + length - 1;
        }
    }
    ULONG flags = lx_guest_load_u32(record + 52) & ~OFN_EXTENSIONDIFFERENT;
    if (extension && defext && strcasecmp(extension + 1, defext))
        flags |= OFN_EXTENSIONDIFFERENT;
    free(defext);
    size_t length = strlen(full);
    if (length + 1 > capacity)
    {
        uint8 size[2];
        lx_guest_store_u16(size, (uint16)(length + 1));
        if (capacity >= 2)
            lx_execution_write(GLoaderState.execution, pointer, size, sizeof size);
        common_error = 0x3003;
        WinDismissDlg(dialog->window, 0);
        return FALSE;
    }
    ULONG attributes = GetFileAttributes(full);
    if ((flags & OFN_FILEMUSTEXIST) && attributes == UINT32_MAX)
    {
        WinSetDlgItemText(dialog->window, 270, "The file does not exist");
        return FALSE;
    }
    if ((flags & OFN_NOREADONLYRETURN) && attributes != UINT32_MAX && (attributes & 1))
    {
        WinSetDlgItemText(dialog->window, 270, "The file is read only");
        return FALSE;
    }
    if (flags & OFN_PATHMUSTEXIST)
    {
        char parent[260];
        memcpy(parent, full, length + 1);
        size_t part = (size_t)(base - full);
        parent[part > 3 ? part - 1 : part] = 0;
        ULONG attr = GetFileAttributes(parent);
        if (attr == UINT32_MAX || !(attr & 0x10))
        {
            WinSetDlgItemText(dialog->window, 270, "The folder does not exist");
            return FALSE;
        }
    }
    const char *question = NULL;
    if (dialog->save && (flags & OFN_OVERWRITEPROMPT) && attributes != UINT32_MAX)
        question = "The file already exists. Replace it?";
    else if ((flags & OFN_CREATEPROMPT) && attributes == UINT32_MAX)
        question = "The file does not exist. Create it?";
    if (question && WinMessageBox(HWND_DESKTOP, dialog->window, (PSZ)question,
                                  dialog->save ? "Save As" : "Open", 0, 0x14) != 6)
        return FALSE;
    if (!lx_execution_write(GLoaderState.execution, pointer, full, length + 1))
        return fail(0xffff);
    ULONG title = lx_guest_load_u32(record + 36), title_capacity = lx_guest_load_u32(record + 40);
    if (title && title_capacity)
    {
        size_t count = strlen(base);
        if (count >= title_capacity)
            count = title_capacity - 1;
        if (!lx_execution_write(GLoaderState.execution, title, base, count) ||
            !lx_execution_write(GLoaderState.execution, title + (ULONG)count, "", 1))
            return fail(0xffff);
    }
    lx_guest_store_u32(record + 24, dialog->count ? dialog->selected + 1 : 0);
    lx_guest_store_u32(record + 52, flags);
    lx_guest_store_u16(record + 56, (uint16)(base - full));
    lx_guest_store_u16(record + 58, extension ? (uint16)(extension + 1 - full) : 0);
    if (!lx_execution_write(GLoaderState.execution, dialog->address + 24, record + 24, 4) ||
        !lx_execution_write(GLoaderState.execution, dialog->address + 52, record + 52, 8))
        return fail(0xffff);
    if (hook(dialog, dialog->file_ok, 0, dialog->address))
        return FALSE;
    dialog->accepted = TRUE;
    return TRUE;
}
static ULONG draw_button(FileDialog *dialog, ULONG id, ULONG pointer)
{
    uint8 button[16], item[48] = {0};
    if (!lx_execution_read(GLoaderState.execution, pointer, button, sizeof button))
        return 0;
    ULONG window = lx_guest_load_u32(button), state = lx_guest_load_u32(button + 8);
    RECTL rect;
    if (!WinQueryWindowRect(window, &rect))
        return 0;
    ULONG dc = GetDC(window);
    LxGuestMapping *mapping =
        lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    ULONG result = 0;
    if (dc && mapping)
    {
        lx_guest_store_u32(item, 4); /* ODT_BUTTON */
        lx_guest_store_u32(item + 4, id);
        lx_guest_store_u32(item + 12, 1); /* ODA_DRAWENTIRE */
        lx_guest_store_u32(item + 16, (state & 0x100 ? 1 : 0) | (state & 0x200 ? 4 : 0) |
                                          (state & 0x400 ? 0x20 : 0) |
                                          (WinQueryFocus(HWND_DESKTOP) == window ? 0x10 : 0));
        lx_guest_store_u32(item + 20, window);
        lx_guest_store_u32(item + 24, dc);
        lx_guest_store_u32(item + 36, (ULONG)(rect.xRight - rect.xLeft));
        lx_guest_store_u32(item + 40, (ULONG)(rect.yTop - rect.yBottom));
        memcpy(lx_execution_mapping_host(mapping), item, sizeof item);
        result = hook(dialog, 0x2b, id, lx_execution_mapping_address(mapping));
    }
    if (mapping)
        lx_execution_unmap(GLoaderState.execution, mapping);
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
            WinSendDlgItemMsg(handle, 262, 0x164, (MPARAM)(uintptr_t)dialog->selected,
                              (MPARAM)(uintptr_t)TRUE);
        if (dialog->flags & OFN_HIDEREADONLY)
            WinShowWindow(WinWindowFromID(handle, 269), FALSE);
        if (!(dialog->flags & OFN_SHOWHELP))
            WinShowWindow(WinWindowFromID(handle, 267), FALSE);
        hook(dialog, 0x110, first, dialog->address);
        select_type(dialog);
        return 0;
    }
    if (message == WM_USER + 41)
    {
        char full[260];
        if (!lx_execution_read(GLoaderState.execution, first, full, sizeof full) ||
            !memchr(full, 0, sizeof full) || !publish_name(dialog, full))
            return FALSE;
        return lx_execution_write(GLoaderState.execution, first, full, strlen(full) + 1);
    }
    if (message == WM_COMMAND || message == WM_CONTROL)
    {
        ULONG child = WinWindowFromID(handle, first & 0xffff);
        char name[16];
        if (message == WM_CONTROL && first >> 16 == 3 &&
            WinQueryClassName(child, sizeof name, name) && !strcmp(name, "#3") &&
            (WinQueryWindowULong(child, -2) & 15) == 7)
            return draw_button(dialog, first & 0xffff, second);
        if (hook(dialog, 0x111,
                 message == WM_CONTROL ? open32ControlNotification(child, first) : first, child))
            return 0;
        if (message == WM_CONTROL && (first & 0xffff) == 262 && first >> 16 == 4)
            select_type(dialog);
        if (message == WM_COMMAND && (first & 0xffff) == 267)
        {
            if (dialog->native->ulUser)
                SendMessage(dialog->native->ulUser, dialog->help, handle, dialog->address);
            return 0;
        }
    }
    else if (message == WM_DESTROY)
        hook(dialog, 2, 0, 0);
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
    uint8 record[OPENFILENAME_SIZE];
    if (pointer > UINT32_MAX ||
        !lx_execution_read(GLoaderState.execution, (ULONG)pointer, record, sizeof record))
        return fail(1);
    ULONG size = lx_guest_load_u32(record), flags = lx_guest_load_u32(record + 52);
    TRACE_NATIVE("Open32 file dialog size=%u flags=%08x custom=%08x", (uint)size, (uint)flags,
                 (uint)lx_guest_load_u32(record + 16));
    if (size != OPENFILENAME_SIZE)
        return fail(1);
    if (!lx_execution_memory(GLoaderState.execution, (ULONG)pointer, size, LX_GUEST_WRITE))
        return fail(0xffff);
    /* The picker offers one file. ALLOWMULTISELECT callers receive the ordinary
       full-path result required for a single selection. */
    ULONG supported = OFN_READONLY | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
                      OFN_SHOWHELP | OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ALLOWMULTISELECT |
                      OFN_EXTENSIONDIFFERENT | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |
                      OFN_CREATEPROMPT | OFN_NOREADONLYRETURN | OFN_NOTESTFILECREATE |
                      OFN_NONETWORKBUTTON | OFN_NOLONGNAMES;
    if (flags & ~supported || lx_guest_load_u32(record + 16))
    {
        open32SetError(50);
        return fail(0xffff);
    }
    ULONG callback = flags & OFN_ENABLEHOOK ? lx_guest_load_u32(record + 68) : 0;
    if ((flags & OFN_ENABLEHOOK) && !open32ValidProcedure(callback))
        return fail(11);
    ULONG file = lx_guest_load_u32(record + 28), capacity = lx_guest_load_u32(record + 32);
    ULONG title = lx_guest_load_u32(record + 36), title_capacity = lx_guest_load_u32(record + 40);
    if (!capacity || capacity > 1048576 ||
        !lx_execution_memory(GLoaderState.execution, file, capacity,
                             LX_GUEST_READ | LX_GUEST_WRITE) ||
        (title && title_capacity &&
         !lx_execution_memory(GLoaderState.execution, title, title_capacity, LX_GUEST_WRITE)))
        return fail(0x3002);
    char *filename = copy_text(file, capacity),
         *directory = copy_text(lx_guest_load_u32(record + 44), 260);
    char *caption = copy_text(lx_guest_load_u32(record + 48), 32768);
    if (!filename || (lx_guest_load_u32(record + 44) && !directory) ||
        (lx_guest_load_u32(record + 48) && !caption))
    {
        free(filename);
        free(directory);
        free(caption);
        return fail(0x3002);
    }
    FileDialog dialog = {.address = (ULONG)pointer,
                         .hook = callback,
                         .flags = flags,
                         .save = save,
                         .previous = file_dialogs};
    dialog.mapping = lx_execution_map(GLoaderState.execution, 0, DIALOG_BYTES,
                                      LX_GUEST_READ | LX_GUEST_WRITE, 0);
    dialog.procedure =
        lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    BOOL result = FALSE;
    if (!dialog.mapping || !dialog.procedure)
    {
        common_error = 9;
        goto cleanup;
    }
    dialog.native = lx_execution_mapping_host(dialog.mapping);
    memset(dialog.native, 0, sizeof *dialog.native);
    dialog.native->cbSize = sizeof *dialog.native;
    dialog.native->fl = save ? 0x200 : 0x100;
    dialog.native->ulUser = lx_guest_load_u32(record + 4);
    size_t offset = sizeof *dialog.native + (FILTER_LIMIT + 1) * 4;
    ULONG filter = lx_guest_load_u32(record + 12);
    uint8 *types = (uint8 *)dialog.native + sizeof *dialog.native;
    while (filter && dialog.count < FILTER_LIMIT)
    {
        char *description = copy_text(filter, 1024);
        if (!description)
        {
            common_error = 0x3002;
            goto cleanup;
        }
        if (!*description)
        {
            free(description);
            break;
        }
        filter += (ULONG)strlen(description) + 1;
        char *pattern = copy_text(filter, 260);
        ULONG text = add_text(&dialog, &offset, description);
        free(description);
        if (!pattern || !*pattern || !text)
        {
            free(pattern);
            common_error = 0x3002;
            goto cleanup;
        }
        filter += (ULONG)strlen(pattern) + 1;
        lx_guest_store_u32(types + dialog.count * 4, text);
        dialog.patterns[dialog.count++] = pattern;
    }
    lx_guest_store_u32(types + dialog.count * 4, 0);
    if (dialog.count)
    {
        ULONG selected = lx_guest_load_u32(record + 24);
        dialog.selected = selected && selected <= dialog.count ? selected - 1 : 0;
        dialog.native->papszITypeList =
            lx_execution_mapping_address(dialog.mapping) + sizeof *dialog.native;
        dialog.native->pszIType = lx_guest_load_u32(types + dialog.selected * 4);
    }
    if (caption)
        dialog.native->pszTitle = add_text(&dialog, &offset, caption);
    int length;
    if (*filename && (strchr(filename, ':') || *filename == '\\' || *filename == '/'))
        length = snprintf(dialog.native->szFullFile, 260, "%s", filename);
    else
        length = snprintf(dialog.native->szFullFile, 260, "%s%s%s", directory ? directory : "",
                          directory && *directory && directory[strlen(directory) - 1] != '\\' &&
                                  directory[strlen(directory) - 1] != '/'
                              ? "\\"
                              : "",
                          filename);
    if (length < 0 || length >= 260)
    {
        common_error = 0x3002;
        goto cleanup;
    }
    if (flags & OFN_ENABLETEMPLATE)
    {
        ULONG resource = lx_guest_load_u32(record + 72);
        if (!resource || resource > UINT16_MAX)
        {
            common_error = 3;
            goto cleanup;
        }
        dialog.native->fl |= 2;
        dialog.native->hMod = lx_guest_load_u32(record + 8);
        if ((!dialog.native->hMod || dialog.native->hMod == UINT32_MAX) && GLoaderState.main_module)
            dialog.native->hMod = GLoaderState.main_module->handle;
        dialog.native->usDlgId = (USHORT)resource;
    }
    const LxExecutionApi32 api = {file_dispatch, 16, 0, LX_ABI_STACK32};
    if (!lx_execution_emit_bridge32(GLoaderState.execution,
                                    lx_execution_mapping_host(dialog.procedure), 4096, &api) ||
        !lx_execution_protect(GLoaderState.execution, dialog.procedure,
                              LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        common_error = 9;
        goto cleanup;
    }
    dialog.native->pfnDlgProc = lx_execution_mapping_address(dialog.procedure);
    dialog.file_ok = RegisterWindowMessage("commdlg_FileNameOK");
    dialog.list_changed = RegisterWindowMessage("commdlg_LBSelChangedNotify");
    dialog.help = RegisterWindowMessage("commdlg_help");
    if (!open32Presentation())
    {
        common_error = 2;
        goto cleanup;
    }
    file_dialogs = &dialog;
    HWND handle = lx_pm_file_dialog(HWND_DESKTOP, dialog.native->ulUser,
                                    (PVOID)(uintptr_t)lx_execution_mapping_address(dialog.mapping));
    file_dialogs = dialog.previous;
    open32NativeControlsPrune();
    if (!handle && !common_error)
        common_error = 0xffff;
    result = dialog.accepted && dialog.native->lReturn == 1;
cleanup:
    for (ULONG i = 0; i < dialog.count; i++)
        free(dialog.patterns[i]);
    if (dialog.procedure)
        lx_execution_unmap(GLoaderState.execution, dialog.procedure);
    if (dialog.mapping)
        lx_execution_unmap(GLoaderState.execution, dialog.mapping);
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
