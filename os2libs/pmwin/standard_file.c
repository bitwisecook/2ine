/* Public domain. No rights reserved. Wholly AI-authored. */
/* File selection uses guest drive mappings and ordinary modal PM controls. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/controls.h"
#include "os2libs/pmwin/standard_dialogs.h"
#include "os2libs/pmctls.h"
#include "hal/host_fs.h"
#include <ctype.h>
typedef struct PmFileDialog
{
    unsigned references;
    PVOID address;
    LXFILEDLG data;
    HWND window;
    BOOL initialized;
    char directory[260], filter[260];
} PmFileDialog;
static void release_file(PmFileDialog *dialog)
{
    if (!--dialog->references)
        free(dialog);
}
void destroyStandardFile(Window *window)
{
    if (window->standard_file)
    {
        release_file(window->standard_file);
        window->standard_file = NULL;
    }
}
static BOOL read_file(PmFileDialog *dialog)
{
    return controlRead(dialog->address, &dialog->data, sizeof dialog->data);
}
static BOOL write_file(PmFileDialog *dialog)
{
    return controlWrite(dialog->address, &dialog->data, sizeof dialog->data);
}
static BOOL absolute_name(const char *name, char *full, size_t capacity)
{
    unsigned drive = GLoaderState.current_disk >= 1 && GLoaderState.current_disk <= 26
                         ? (unsigned)GLoaderState.current_disk - 1
                         : 2;
    if (name[0] && name[1] == ':' && isalpha((unsigned char)name[0]))
    {
        drive = (unsigned)(toupper((unsigned char)name[0]) - 'A');
        name += 2;
    }
    const char *cwd = GLoaderState.current_dir[drive] ? GLoaderState.current_dir[drive] : "";
    BOOL rooted = *name == '\\' || *name == '/';
    if (rooted)
        cwd = "";
    while (*name == '\\' || *name == '/')
        name++;
    int count = snprintf(full, capacity, "%c:\\%s%s%s", 'A' + drive, cwd, *cwd ? "\\" : "", name);
    if (count < 0 || (size_t)count >= capacity)
        return FALSE;
    for (char *p = full; *p; p++)
        if (*p == '/')
            *p = '\\';
    return TRUE;
}
static BOOL wildcard(const char *pattern, const char *name)
{
    if (!strcmp(pattern, "*.*"))
        return TRUE;
    const char *star = NULL, *retry = NULL;
    while (*name)
    {
        if (*pattern == '?' || toupper((unsigned char)*pattern) == toupper((unsigned char)*name))
        {
            pattern++;
            name++;
        }
        else if (*pattern == '*')
        {
            star = pattern++;
            retry = name;
        }
        else if (star)
        {
            pattern = star + 1;
            name = ++retry;
        }
        else
            return FALSE;
    }
    while (*pattern == '*')
        pattern++;
    return !*pattern;
}
static void file_error(PmFileDialog *dialog, const char *message)
{
    dialog->data.lSRC = 6;
    write_file(dialog);
    WinSetDlgItemText(dialog->window, 270, (PSZ)message);
}
static void populate_types(PmFileDialog *dialog)
{
    HWND window = dialog->window;
    if (!WinWindowFromID(window, 262))
        return;
    WinSendDlgItemMsg(window, 262, 0x16e, 0, 0);
    unsigned selected = 0, count = 0;
    char *initial =
        dialog->data.pszIType ? controlCopyText((MPARAM)(uintptr_t)dialog->data.pszIType) : NULL;
    if (dialog->data.papszITypeList)
        for (unsigned i = 0; i < 128; i++)
        {
            uint32 address;
            if (!controlRead((MPARAM)(uintptr_t)(dialog->data.papszITypeList + i * 4), &address,
                             4) ||
                !address)
                break;
            char *text = controlCopyText((MPARAM)(uintptr_t)address);
            if (!text)
                break;
            if (initial && !strcmp(initial, text))
                selected = i;
            WinSendDlgItemMsg(window, 262, 0x161, (MPARAM)(intptr_t)-1, text);
            free(text);
            count++;
        }
    if (!count)
        WinSendDlgItemMsg(window, 262, 0x161, (MPARAM)(intptr_t)-1,
                          initial ? initial : "All files");
    WinSendDlgItemMsg(window, 262, 0x164, (MPARAM)(uintptr_t)selected, (MPARAM)(uintptr_t)TRUE);
    dialog->data.sEAType = (SHORT)selected;
    free(initial);
}
static void populate_files(PmFileDialog *dialog)
{
    HWND window = dialog->window;
    WinSendDlgItemMsg(window, 264, 0x16e, 0, 0);
    WinSendDlgItemMsg(window, 266, 0x16e, 0, 0);
    WinSetDlgItemText(window, 270, dialog->directory);
    if (!GLoaderState.makeUnixPath)
        return;
    uint32 error = 0;
    char *host = GLoaderState.makeUnixPath(dialog->directory, &error);
    if (!host)
        return;
    LxHostFsError status;
    LxHostDirectory *directory = lx_host_directory_open(host, &status);
    free(host);
    if (!directory)
    {
        file_error(dialog, "Folder is unavailable");
        return;
    }
    const char *name;
    LxHostFileInfo info;
    while (lx_host_directory_read(directory, &name, &info, &status) > 0)
    {
        if (!strcmp(name, "."))
            continue;
        if (info.kind == LX_HOST_FILE_DIRECTORY)
            WinSendDlgItemMsg(window, 264, 0x161, (MPARAM)(intptr_t)-2, (MPARAM)name);
        else if (info.kind == LX_HOST_FILE_REGULAR && wildcard(dialog->filter, name))
            WinSendDlgItemMsg(window, 266, 0x161, (MPARAM)(intptr_t)-2, (MPARAM)name);
    }
    lx_host_directory_close(directory);
}
static void initialize_file(PmFileDialog *dialog)
{
    if (dialog->initialized)
        return;
    dialog->initialized = TRUE;
    read_file(dialog);
    char full[260];
    if (!absolute_name(dialog->data.szFullFile, full, sizeof full))
    {
        file_error(dialog, "File path is too long");
        return;
    }
    char *last = strrchr(full, '\\');
    char filename[260];
    snprintf(filename, sizeof filename, "%s", last ? last + 1 : full);
    if (last)
    {
        last[1] = 0;
        snprintf(dialog->directory, sizeof dialog->directory, "%s", full);
    }
    else
        snprintf(dialog->directory, sizeof dialog->directory, "%c:\\",
                 'A' + GLoaderState.current_disk - 1);
    snprintf(dialog->filter, sizeof dialog->filter, "%s",
             strpbrk(filename, "*?") ? filename : "*.*");
    if (strpbrk(filename, "*?"))
        filename[0] = 0;
    WinSetDlgItemText(dialog->window, 258, filename);
    WinSendDlgItemMsg(dialog->window, 258, 0x143, (MPARAM)(uintptr_t)259, 0);
    if (dialog->data.pszTitle)
    {
        char *title = controlCopyText((MPARAM)(uintptr_t)dialog->data.pszTitle);
        if (title)
        {
            WinSetWindowText(dialog->window, title);
            free(title);
        }
    }
    else
        WinSetWindowText(dialog->window, (dialog->data.fl & 0x200) ? "Save As" : "Open");
    char *ok = dialog->data.pszOKButton
                   ? controlCopyText((MPARAM)(uintptr_t)dialog->data.pszOKButton)
                   : NULL;
    WinSetDlgItemText(dialog->window, 1, ok ? ok : (dialog->data.fl & 0x200) ? "Save" : "Open");
    free(ok);
    if (WinWindowFromID(dialog->window, 260))
    {
        WinSendDlgItemMsg(dialog->window, 260, 0x16e, 0, 0);
        unsigned selected = 0, count = 0;
        for (unsigned i = 0; i < 26; i++)
            if (GLoaderState.disks[i])
            {
                char drive[3] = {(char)('A' + i), ':', 0};
                if (i + 1 == (unsigned)GLoaderState.current_disk)
                    selected = count;
                WinSendDlgItemMsg(dialog->window, 260, 0x161, (MPARAM)(intptr_t)-1, drive);
                count++;
            }
        if (count)
            WinSendDlgItemMsg(dialog->window, 260, 0x164, (MPARAM)(uintptr_t)selected,
                              (MPARAM)(uintptr_t)TRUE);
    }
    populate_types(dialog);
    populate_files(dialog);
    write_file(dialog);
    WinSendDlgItemMsg(dialog->window, 258, 0x142, (MPARAM)(uintptr_t)0xffff0000, 0);
    WinSetFocus(HWND_DESKTOP, WinWindowFromID(dialog->window, 258));
}
static BOOL select_file(PmFileDialog *dialog)
{
    read_file(dialog);
    char name[260], full[260];
    WinQueryDlgItemText(dialog->window, 258, sizeof name, name);
    if (!*name)
    {
        file_error(dialog, "Enter a file name");
        return FALSE;
    }
    if (strchr(name, ':') || *name == '\\' || *name == '/')
    {
        if (!absolute_name(name, full, sizeof full))
        {
            file_error(dialog, "File path is too long");
            return FALSE;
        }
    }
    else if (snprintf(full, sizeof full, "%s%s", dialog->directory, name) >= (int)sizeof full)
    {
        file_error(dialog, "File path is too long");
        return FALSE;
    }
    if (strpbrk(full, "*?"))
    {
        char *last = strrchr(full, '\\');
        if (!last)
            return FALSE;
        snprintf(dialog->filter, sizeof dialog->filter, "%s", last + 1);
        last[1] = 0;
        snprintf(dialog->directory, sizeof dialog->directory, "%s", full);
        populate_files(dialog);
        return FALSE;
    }
    uint32 error = 0;
    char *host = GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(full, &error) : NULL;
    if (!host)
    {
        file_error(dialog, "Invalid file path");
        return FALSE;
    }
    LxHostFileInfo info;
    LxHostFsError status = lx_host_path_info(host, &info);
    if (status == LX_HOST_FS_OK && info.kind == LX_HOST_FILE_DIRECTORY)
    {
        size_t length = strlen(full);
        if (length + 1 >= sizeof dialog->directory)
        {
            free(host);
            file_error(dialog, "Folder path is too long");
            return FALSE;
        }
        memcpy(dialog->directory, full, length);
        dialog->directory[length] = '\\';
        dialog->directory[length + 1] = 0;
        free(host);
        populate_files(dialog);
        return FALSE;
    }
    free(host);
    if (status != LX_HOST_FS_OK && status != LX_HOST_FS_MISSING)
    {
        file_error(dialog, "File is unavailable");
        return FALSE;
    }
    Window *window = getWindowFromHWND(getAnchorBlockNoHAB(), dialog->window);
    if (!window)
        return FALSE;
    LxExecution *execution = GLoaderState.execution;
    BOOL guest =
        execution && (uintptr_t)window->window_proc <= UINT32_MAX &&
        lx_execution_memory(execution, (uint32)(uintptr_t)window->window_proc, 1, LX_GUEST_EXECUTE);
    LxGuestMapping *mapping =
        guest ? lx_execution_map(execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0) : NULL;
    if (guest && !mapping)
        return FALSE;
    PVOID validation = full;
    if (mapping)
    {
        memcpy(lx_execution_mapping_host(mapping), full, strlen(full) + 1);
        validation = (PVOID)(uintptr_t)lx_execution_mapping_address(mapping);
    }
    BOOL accepted = WinSendMsg(dialog->window, WM_USER + 41, validation, 0) != 0;
    if (mapping)
    {
        if (accepted)
        {
            const char *changed = lx_execution_mapping_host(mapping);
            if (!memchr(changed, 0, sizeof full))
                accepted = FALSE;
            else
                snprintf(full, sizeof full, "%s", changed);
        }
        lx_execution_unmap(execution, mapping);
    }
    TRACE_NATIVE("File validation accepted=%u name='%s'", (uint)accepted, full);
    if (!accepted || !read_file(dialog))
        return FALSE;
    snprintf(dialog->data.szFullFile, sizeof dialog->data.szFullFile, "%s", full);
    dialog->data.lSRC = 0;
    dialog->data.ulFQFCount = 1;
    dialog->data.sEAType =
        (SHORT)(uintptr_t)WinSendDlgItemMsg(dialog->window, 262, 0x165, (MPARAM)(intptr_t)-1, 0);
    dialog->data.lReturn = 1;
    write_file(dialog);
    WinDismissDlg(dialog->window, 1);
    return TRUE;
}
MRESULT lx_pm_file_procedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    PmFileDialog *dialog = window ? window->standard_file : NULL;
    if (!dialog)
        return WinDefDlgProc(handle, message, first, second);
    if (message == WM_INITDLG)
    {
        initialize_file(dialog);
        return 0;
    }
    if (message == WM_USER + 40 || message == WM_USER + 41)
        return (MRESULT)(uintptr_t)TRUE;
    if (message == WM_COMMAND)
    {
        USHORT id = (USHORT)(uintptr_t)first;
        if (id == 1)
        {
            select_file(dialog);
            return 0;
        }
        if (id == 2)
        {
            read_file(dialog);
            dialog->data.lReturn = 2;
            dialog->data.lSRC = 0;
            write_file(dialog);
            WinDismissDlg(handle, 2);
            return 0;
        }
    }
    if (message == WM_CLOSE)
    {
        read_file(dialog);
        dialog->data.lReturn = 2;
        write_file(dialog);
        WinDismissDlg(handle, 2);
        return 0;
    }
    if (message == WM_CONTROL)
    {
        ULONG packed = (ULONG)(uintptr_t)first;
        USHORT id = (USHORT)packed, code = (USHORT)(packed >> 16);
        if ((id == 264 || id == 266) && code == 1)
        {
            LONG selected =
                (LONG)(intptr_t)WinSendDlgItemMsg(handle, id, 0x165, (MPARAM)(intptr_t)-1, 0);
            if (selected >= 0)
            {
                Window *list = getWindowFromHWND(anchor, WinWindowFromID(handle, id));
                if (list && list->control && (size_t)selected < list->control->count)
                {
                    const char *name = list->control->items[selected].text;
                    if (id == 266)
                        WinSetDlgItemText(handle, 258, (PSZ)name);
                    else
                    {
                        char full[260];
                        if (snprintf(full, sizeof full, "%s%s", dialog->directory, name) <
                            (int)sizeof full)
                        {
                            WinSetDlgItemText(handle, 258, full);
                            select_file(dialog);
                        }
                    }
                }
            }
        }
    }
    return WinDefDlgProc(handle, message, first, second);
}
static MRESULT OS2API file_dispatch(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return 0;
    if (message == WM_INITDLG && !window->standard_file && anchor->pending_file)
    {
        PmFileDialog *dialog = anchor->pending_file;
        window->standard_file = dialog;
        dialog->references++;
        dialog->window = handle;
        WinSetWindowPtr(handle, 0, dialog->address);
        initialize_file(dialog);
        if (dialog->data.pfnDlgProc)
        {
            WinSubclassWindow(handle, (PFNWP)(uintptr_t)dialog->data.pfnDlgProc);
            MRESULT result = WinSendMsg(handle, message, first, second);
            if (WinIsWindow(anchor->hab, handle) && WinWindowFromID(handle, 262) &&
                !(uintptr_t)WinSendDlgItemMsg(handle, 262, 0x160, 0, 0))
                populate_types(dialog);
            return result;
        }
    }
    return lx_pm_file_procedure(handle, message, first, second);
}
static PDLGTEMPLATE file_template(uint8 *bytes, size_t capacity)
{
    typedef struct
    {
        USHORT id, kind;
        SHORT x, y, w, h;
        ULONG style;
        const char *text;
    } Item;
    static const Item controls[] = {{257, 5, 8, 196, 250, 12, 1, "File name:"},
                                    {258, 6, 8, 180, 250, 14, 0x20004, ""},
                                    {1, 3, 270, 182, 80, 14, 0x20400, "Save"},
                                    {2, 3, 270, 162, 80, 14, 0x20000, "Cancel"},
                                    {263, 5, 8, 148, 145, 12, 1, "Folders"},
                                    {264, 7, 8, 36, 145, 110, 0x20000, ""},
                                    {265, 5, 164, 148, 188, 12, 1, "Files"},
                                    {266, 7, 164, 36, 188, 110, 0x20000, ""},
                                    {261, 5, 164, 20, 188, 12, 1, "File type:"},
                                    {262, 2, 164, 2, 188, 120, 0x20004, ""},
                                    {259, 5, 8, 20, 145, 12, 1, "Drive:"},
                                    {260, 2, 8, 2, 145, 120, 0x20004, ""},
                                    {270, 5, 8, 126, 344, 12, 1, ""}};
    unsigned count = sizeof controls / sizeof *controls;
    size_t offset = 14 + (count + 1) * 30;
    if (capacity < offset)
        return NULL;
    memset(bytes, 0, capacity);
    DLGTEMPLATE *t = (void *)bytes;
    t->codepage = 437;
    t->offadlgti = 14;
    t->iItemFocus = 2;
    DLGTITEM *items = (void *)(bytes + 14);
    items[0].cChildren = count;
    items[0].offClassName = 1;
    items[0].id = 256;
    items[0].cx = 360;
    items[0].cy = 220;
    items[0].flStyle = WS_VISIBLE;
    for (unsigned i = 0; i <= count; i++)
    {
        DLGTITEM *item = &items[i];
        item->offPresParams = item->offCtlData = 0xffff;
        if (!i)
            continue;
        const Item *c = &controls[i - 1];
        item->offClassName = c->kind;
        item->id = c->id;
        item->x = c->x;
        item->y = c->y;
        item->cx = c->w;
        item->cy = c->h;
        item->flStyle = WS_VISIBLE | c->style;
        size_t length = strlen(c->text);
        if (offset + length + 1 > capacity)
            return NULL;
        item->cchText = (USHORT)length;
        item->offText = (USHORT)offset;
        memcpy(bytes + offset, c->text, length + 1);
        offset += length + 1;
    }
    t->cbTemplate = (USHORT)offset;
    return t;
}
HWND lx_pm_file_dialog(HWND parent, HWND owner, PVOID address)
{
    TRACE_NATIVE("WinFileDlg(%u, %u, %p)", (uint)parent, (uint)owner, address);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PmFileDialog *dialog = calloc(1, sizeof *dialog);
    if (!anchor || !dialog)
    {
        free(dialog);
        return 0;
    }
    dialog->references = 1;
    dialog->address = address;
    if (!read_file(dialog) || dialog->data.cbSize != sizeof dialog->data ||
        !memchr(dialog->data.szFullFile, 0, 260))
    {
        release_file(dialog);
        return 0;
    }
    if (!(dialog->data.fl & 0x300) || (dialog->data.fl & 0x300) == 0x300)
    {
        dialog->data.lSRC = 3;
        dialog->data.lReturn = 0;
        write_file(dialog);
        release_file(dialog);
        return 0;
    }
    TRACE_NATIVE("FILEDLG flags=%08X template=%u callback=%08X name='%s'", (uint)dialog->data.fl,
                 (uint)dialog->data.usDlgId, (uint)dialog->data.pfnDlgProc,
                 dialog->data.szFullFile);
    dialog->data.lReturn = 0;
    dialog->data.lSRC = 0;
    write_file(dialog);
    PmFileDialog *previous = anchor->pending_file;
    anchor->pending_file = dialog;
    uint8 resource[1024];
    HWND handle = (dialog->data.fl & 2)
                      ? WinLoadDlg(parent, owner, file_dispatch, dialog->data.hMod,
                                   dialog->data.usDlgId, address)
                      : WinCreateDlg(parent, owner, file_dispatch,
                                     file_template(resource, sizeof resource), address);
    anchor->pending_file = previous;
    if (!handle)
    {
        dialog->data.lSRC = 12;
        write_file(dialog);
        release_file(dialog);
        return 0;
    }
    Window *window = getWindowFromHWND(anchor, handle);
    if (window && !(dialog->data.fl & 2))
    {
        LONG x = dialog->data.x, y = dialog->data.y;
        if ((dialog->data.fl & 1) || (!x && !y))
        {
            x = (desktop_window.w - window->w) / 2;
            y = (desktop_window.h - window->h) / 2;
        }
        WinSetWindowPos(handle, HWND_TOP, x, y, window->w, window->h,
                        SWP_MOVE | SWP_SIZE | SWP_SHOW);
    }
    if (dialog->data.fl & 0x40)
    {
        release_file(dialog);
        return handle;
    }
    ULONG result = WinProcessDlg(handle);
    if (read_file(dialog))
    {
        dialog->data.lReturn = (LONG)result;
        write_file(dialog);
    }
    if (WinIsWindow(WinQueryAnchorBlock(handle), handle))
        WinDestroyWindow(handle);
    release_file(dialog);
    return TRUE;
}
