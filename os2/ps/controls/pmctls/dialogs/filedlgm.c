/* Public domain. No rights reserved. Wholly AI-authored. */
/* File selection uses guest drive mappings and ordinary modal PM controls. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmp.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/bsedos.h"
#include "host/fs.h"
#include <ctype.h>

enum
{
    /* Drives A: to Z:; GProcessState numbers them from 1. */
    DRIVE_COUNT = 26,
    DEFAULT_DRIVE = 2, /* C: */
    /* papszITypeList ends at a null entry or at this many types. */
    FILE_TYPE_LIMIT = 128,
    /* lx_execution_map allocates whole guest pages. */
    GUEST_PAGE_SIZE = 4096,
    /* Room for the built-in template and its strings. */
    TEMPLATE_BUFFER_SIZE = 1024,
    TEMPLATE_CODEPAGE = 437,
    /* A DLGTITEM offset with no presentation parameters or control data. */
    TEMPLATE_NO_DATA = 0xffff,
    TEMPLATE_ITEMS_OFFSET = offsetof(DLGTEMPLATE, adlgti),
    TEMPLATE_ITEM_SIZE = sizeof(DLGTITEM),
    /* adlgti index of the file name entry field, which takes the focus. */
    TEMPLATE_FOCUS_ITEM = 2,
    FILE_DIALOG_WIDTH = 360,
    FILE_DIALOG_HEIGHT = 220,
    /* Text, entry field, push button and list box styles of the built-in template. */
    TEXT_STYLE = SS_TEXT,
    ENTRY_STYLE = WS_TABSTOP | ES_AUTOSCROLL,
    DEFAULT_BUTTON_STYLE = WS_TABSTOP | BS_PUSHBUTTON | BS_DEFAULT,
    BUTTON_STYLE = WS_TABSTOP | BS_PUSHBUTTON,
    LIST_STYLE = WS_TABSTOP,
    DROPDOWN_STYLE = WS_TABSTOP | CBS_DROPDOWNLIST
};

typedef struct PmFileDialog
{
    unsigned references;
    PVOID address;
    LXFILEDLG data;
    HWND window;
    BOOL initialized;
    char directory[CCHMAXPATH], filter[CCHMAXPATH];
} PmFileDialog;

static void release_file(PmFileDialog *dialog)
{
    if (!--dialog->references)
        free(dialog);
}

static void destroyStandardFile(Window *window)
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
    unsigned drive = GProcessState.current_disk >= 1 && GProcessState.current_disk <= DRIVE_COUNT
                         ? (unsigned)GProcessState.current_disk - 1
                         : DEFAULT_DRIVE;
    if (name[0] && name[1] == ':' && isalpha((unsigned char)name[0]))
    {
        drive = (unsigned)(toupper((unsigned char)name[0]) - 'A');
        name += 2;
    }
    const char *cwd = GProcessState.current_dir[drive] ? GProcessState.current_dir[drive] : "";
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

static BOOL wildcard_one(const char *pattern, const char *end, const char *name)
{
    if (end - pattern == 3 && !memcmp(pattern, "*.*", 3))
        return TRUE;
    const char *star = NULL, *retry = NULL;
    while (*name)
    {
        if (pattern < end && *pattern == '*')
        {
            star = pattern++;
            retry = name;
        }
        else if (pattern < end && (*pattern == '?' || toupper((unsigned char)*pattern) ==
                                                          toupper((unsigned char)*name)))
        {
            pattern++;
            name++;
        }
        else if (star)
        {
            pattern = star + 1;
            name = ++retry;
        }
        else
            return FALSE;
    }
    while (pattern < end && *pattern == '*')
        pattern++;
    return pattern == end;
}

/* A filter is one pattern or a semicolon-separated list of them. */
static BOOL wildcard(const char *pattern, const char *name)
{
    for (;;)
    {
        const char *end = strchr(pattern, ';');
        if (wildcard_one(pattern, end ? end : pattern + strlen(pattern), name))
            return TRUE;
        if (!end)
            return FALSE;
        pattern = end + 1;
    }
}

static void file_error(PmFileDialog *dialog, const char *message)
{
    dialog->data.lSRC = FDS_ERR_INVALID_PATHFILE;
    write_file(dialog);
    WinSetDlgItemText(dialog->window, DID_DIRECTORY_SELECTED, (PSZ)message);
}

static void populate_types(PmFileDialog *dialog)
{
    HWND window = dialog->window;
    if (!WinWindowFromID(window, DID_FILTER_CB))
        return;
    WinSendDlgItemMsg(window, DID_FILTER_CB, LM_DELETEALL, 0, 0);
    unsigned selected = 0, count = 0;
    char *initial =
        dialog->data.pszIType ? controlCopyText((MPARAM)(uintptr_t)dialog->data.pszIType) : NULL;
    if (dialog->data.papszITypeList)
        for (unsigned i = 0; i < FILE_TYPE_LIMIT; i++)
        {
            uint32 address;
            if (!controlRead(
                    (MPARAM)(uintptr_t)(dialog->data.papszITypeList + i * (unsigned)sizeof address),
                    &address, sizeof address) ||
                !address)
                break;
            char *text = controlCopyText((MPARAM)(uintptr_t)address);
            if (!text)
                break;
            if (initial && !strcmp(initial, text))
                selected = i;
            WinSendDlgItemMsg(window, DID_FILTER_CB, LM_INSERTITEM, (MPARAM)(intptr_t)LIT_END,
                              text);
            free(text);
            count++;
        }
    if (!count)
        WinSendDlgItemMsg(window, DID_FILTER_CB, LM_INSERTITEM, (MPARAM)(intptr_t)LIT_END,
                          initial ? initial : "All files");
    WinSendDlgItemMsg(window, DID_FILTER_CB, LM_SELECTITEM, (MPARAM)(uintptr_t)selected,
                      (MPARAM)(uintptr_t)TRUE);
    dialog->data.sEAType = (SHORT)selected;
    free(initial);
}

static void populate_files(PmFileDialog *dialog)
{
    HWND window = dialog->window;
    WinSendDlgItemMsg(window, DID_DIRECTORY_LB, LM_DELETEALL, 0, 0);
    WinSendDlgItemMsg(window, DID_FILES_LB, LM_DELETEALL, 0, 0);
    WinSetDlgItemText(window, DID_DIRECTORY_SELECTED, dialog->directory);
    TRACE_NATIVE("populate_files('%s', '%s')", dialog->directory, dialog->filter);
    if (!GProcessState.makeUnixPath)
        return;
    uint32 error = 0;
    char *host = GProcessState.makeUnixPath(dialog->directory, &error);
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
            WinSendDlgItemMsg(window, DID_DIRECTORY_LB, LM_INSERTITEM,
                              (MPARAM)(intptr_t)LIT_SORTASCENDING, (MPARAM)name);
        else if (info.kind == LX_HOST_FILE_REGULAR && wildcard(dialog->filter, name))
            WinSendDlgItemMsg(window, DID_FILES_LB, LM_INSERTITEM,
                              (MPARAM)(intptr_t)LIT_SORTASCENDING, (MPARAM)name);
    }
    lx_host_directory_close(directory);
}

BOOL lx_pm_file_set_filter(HWND handle, const char *pattern)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    PmFileDialog *dialog = window ? window->standard_file : NULL;
    if (!dialog || !pattern || strlen(pattern) >= sizeof dialog->filter)
        return FALSE;
    snprintf(dialog->filter, sizeof dialog->filter, "%s", pattern);
    populate_files(dialog);
    return TRUE;
}

static void initialize_file(PmFileDialog *dialog)
{
    if (dialog->initialized)
        return;
    dialog->initialized = TRUE;
    read_file(dialog);
    char full[CCHMAXPATH];
    if (!absolute_name(dialog->data.szFullFile, full, sizeof full))
    {
        file_error(dialog, "File path is too long");
        return;
    }
    char *last = strrchr(full, '\\');
    char filename[CCHMAXPATH];
    snprintf(filename, sizeof filename, "%s", last ? last + 1 : full);
    if (last)
    {
        last[1] = 0;
        snprintf(dialog->directory, sizeof dialog->directory, "%s", full);
    }
    else
        snprintf(dialog->directory, sizeof dialog->directory, "%c:\\",
                 'A' + GProcessState.current_disk - 1);
    snprintf(dialog->filter, sizeof dialog->filter, "%s",
             strpbrk(filename, "*?") ? filename : "*.*");
    if (strpbrk(filename, "*?"))
        filename[0] = 0;
    WinSetDlgItemText(dialog->window, DID_FILENAME_ED, filename);
    WinSendDlgItemMsg(dialog->window, DID_FILENAME_ED, EM_SETTEXTLIMIT,
                      (MPARAM)(uintptr_t)(CCHMAXPATH - 1), 0);
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
        WinSetWindowText(dialog->window,
                         (dialog->data.fl & FDS_SAVEAS_DIALOG) ? "Save As" : "Open");
    char *ok = dialog->data.pszOKButton
                   ? controlCopyText((MPARAM)(uintptr_t)dialog->data.pszOKButton)
                   : NULL;
    WinSetDlgItemText(dialog->window, DID_OK,
                      ok                                      ? ok
                      : (dialog->data.fl & FDS_SAVEAS_DIALOG) ? "Save"
                                                              : "Open");
    free(ok);
    if (WinWindowFromID(dialog->window, DID_DRIVE_CB))
    {
        WinSendDlgItemMsg(dialog->window, DID_DRIVE_CB, LM_DELETEALL, 0, 0);
        unsigned selected = 0, count = 0;
        for (unsigned i = 0; i < DRIVE_COUNT; i++)
            if (GProcessState.disks[i])
            {
                char drive[3] = {(char)('A' + i), ':', 0};
                if (i + 1 == (unsigned)GProcessState.current_disk)
                    selected = count;
                WinSendDlgItemMsg(dialog->window, DID_DRIVE_CB, LM_INSERTITEM,
                                  (MPARAM)(intptr_t)LIT_END, drive);
                count++;
            }
        if (count)
            WinSendDlgItemMsg(dialog->window, DID_DRIVE_CB, LM_SELECTITEM,
                              (MPARAM)(uintptr_t)selected, (MPARAM)(uintptr_t)TRUE);
    }
    populate_types(dialog);
    populate_files(dialog);
    write_file(dialog);
    WinSendDlgItemMsg(dialog->window, DID_FILENAME_ED, EM_SETSEL,
                      (MPARAM)(uintptr_t)0xffff0000 /* the whole name */, 0);
    WinSetFocus(HWND_DESKTOP, WinWindowFromID(dialog->window, DID_FILENAME_ED));
}

static BOOL select_file(PmFileDialog *dialog)
{
    read_file(dialog);
    char name[CCHMAXPATH], full[CCHMAXPATH];
    WinQueryDlgItemText(dialog->window, DID_FILENAME_ED, sizeof name, name);
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
    char *host = GProcessState.makeUnixPath ? GProcessState.makeUnixPath(full, &error) : NULL;
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
    LxExecution *execution = GProcessState.execution;
    BOOL guest =
        execution && (uintptr_t)window->window_proc <= UINT32_MAX &&
        lx_execution_memory(execution, (uint32)(uintptr_t)window->window_proc, 1, LX_GUEST_EXECUTE);
    LxGuestMapping *mapping =
        guest ? lx_execution_map(execution, 0, GUEST_PAGE_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 0)
              : NULL;
    if (guest && !mapping)
        return FALSE;
    PVOID validation = full;
    if (mapping)
    {
        memcpy(lx_execution_mapping_host(mapping), full, strlen(full) + 1);
        validation = (PVOID)(uintptr_t)lx_execution_mapping_address(mapping);
    }
    BOOL accepted = WinSendMsg(dialog->window, FDM_VALIDATE, validation, 0) != 0;
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
    dialog->data.lSRC = FDS_SUCCESSFUL;
    dialog->data.ulFQFCount = 1;
    dialog->data.sEAType = (SHORT)(uintptr_t)WinSendDlgItemMsg(
        dialog->window, DID_FILTER_CB, LM_QUERYSELECTION, (MPARAM)(intptr_t)LIT_FIRST, 0);
    dialog->data.lReturn = DID_OK;
    write_file(dialog);
    WinDismissDlg(dialog->window, DID_OK);
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
    if (message == FDM_FILTER || message == FDM_VALIDATE)
        return (MRESULT)(uintptr_t)TRUE;
    if (message == WM_COMMAND)
    {
        USHORT id = (USHORT)(uintptr_t)first;
        if (id == DID_OK)
        {
            select_file(dialog);
            return 0;
        }
        if (id == DID_CANCEL)
        {
            read_file(dialog);
            dialog->data.lReturn = DID_CANCEL;
            dialog->data.lSRC = FDS_SUCCESSFUL;
            write_file(dialog);
            WinDismissDlg(handle, DID_CANCEL);
            return 0;
        }
    }
    if (message == WM_CLOSE)
    {
        read_file(dialog);
        dialog->data.lReturn = DID_CANCEL;
        write_file(dialog);
        WinDismissDlg(handle, DID_CANCEL);
        return 0;
    }
    if (message == WM_CONTROL)
    {
        ULONG packed = (ULONG)(uintptr_t)first;
        USHORT id = (USHORT)packed, code = (USHORT)(packed >> 16);
        if ((id == DID_DIRECTORY_LB || id == DID_FILES_LB) && code == LN_SELECT)
        {
            LONG selected = (LONG)(intptr_t)WinSendDlgItemMsg(handle, id, LM_QUERYSELECTION,
                                                              (MPARAM)(intptr_t)LIT_FIRST, 0);
            if (selected >= 0)
            {
                Window *list = getWindowFromHWND(anchor, WinWindowFromID(handle, id));
                if (list && list->control && (size_t)selected < list->control->count)
                {
                    const char *name = list->control->items[selected].text;
                    if (id == DID_FILES_LB)
                        WinSetDlgItemText(handle, DID_FILENAME_ED, (PSZ)name);
                    else
                    {
                        char full[CCHMAXPATH];
                        if (snprintf(full, sizeof full, "%s%s", dialog->directory, name) <
                            (int)sizeof full)
                        {
                            WinSetDlgItemText(handle, DID_FILENAME_ED, full);
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
        WinSetWindowPtr(handle, QWL_USER, dialog->address);
        initialize_file(dialog);
        if (dialog->data.pfnDlgProc)
        {
            WinSubclassWindow(handle, (PFNWP)(uintptr_t)dialog->data.pfnDlgProc);
            MRESULT result = WinSendMsg(handle, message, first, second);
            if (WinIsWindow(anchor->hab, handle) && WinWindowFromID(handle, DID_FILTER_CB) &&
                !(uintptr_t)WinSendDlgItemMsg(handle, DID_FILTER_CB, LM_QUERYITEMCOUNT, 0, 0))
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

    enum
    {
        TEXT = PM_CLASS_STATIC,
        ENTRY = PM_CLASS_ENTRYFIELD,
        BUTTON = PM_CLASS_BUTTON,
        LIST = PM_CLASS_LISTBOX,
        COMBO = PM_CLASS_COMBOBOX
    };

    static const Item controls[] = {
        {DID_FILENAME_TXT, TEXT, 8, 196, 250, 12, TEXT_STYLE, "File name:"},
        {DID_FILENAME_ED, ENTRY, 8, 180, 250, 14, ENTRY_STYLE, ""},
        {DID_OK, BUTTON, 270, 182, 80, 14, DEFAULT_BUTTON_STYLE, "Save"},
        {DID_CANCEL, BUTTON, 270, 162, 80, 14, BUTTON_STYLE, "Cancel"},
        {DID_DIRECTORY_TXT, TEXT, 8, 148, 145, 12, TEXT_STYLE, "Folders"},
        {DID_DIRECTORY_LB, LIST, 8, 36, 145, 110, LIST_STYLE, ""},
        {DID_FILES_TXT, TEXT, 164, 148, 188, 12, TEXT_STYLE, "Files"},
        {DID_FILES_LB, LIST, 164, 36, 188, 110, LIST_STYLE, ""},
        {DID_FILTER_TXT, TEXT, 164, 20, 188, 12, TEXT_STYLE, "File type:"},
        {DID_FILTER_CB, COMBO, 164, 2, 188, 120, DROPDOWN_STYLE, ""},
        {DID_DRIVE_TXT, TEXT, 8, 20, 145, 12, TEXT_STYLE, "Drive:"},
        {DID_DRIVE_CB, COMBO, 8, 2, 145, 120, DROPDOWN_STYLE, ""},
        {DID_DIRECTORY_SELECTED, TEXT, 8, 126, 344, 12, TEXT_STYLE, ""}};
    unsigned count = sizeof controls / sizeof *controls;
    size_t offset = TEMPLATE_ITEMS_OFFSET + (count + 1) * TEMPLATE_ITEM_SIZE;
    if (capacity < offset)
        return NULL;
    memset(bytes, 0, capacity);
    DLGTEMPLATE *t = (void *)bytes;
    t->codepage = TEMPLATE_CODEPAGE;
    t->offadlgti = TEMPLATE_ITEMS_OFFSET;
    t->iItemFocus = TEMPLATE_FOCUS_ITEM;
    DLGTITEM *items = (void *)(bytes + TEMPLATE_ITEMS_OFFSET);
    items[0].cChildren = count;
    items[0].offClassName = PM_CLASS_FRAME;
    items[0].id = DID_FILE_DIALOG;
    items[0].cx = FILE_DIALOG_WIDTH;
    items[0].cy = FILE_DIALOG_HEIGHT;
    items[0].flStyle = WS_VISIBLE;
    for (unsigned i = 0; i <= count; i++)
    {
        DLGTITEM *item = &items[i];
        item->offPresParams = item->offCtlData = TEMPLATE_NO_DATA;
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
        !memchr(dialog->data.szFullFile, 0, sizeof dialog->data.szFullFile))
    {
        release_file(dialog);
        return 0;
    }
    /* Exactly one of the open and save-as kinds. */
    ULONG kind = dialog->data.fl & (FDS_OPEN_DIALOG | FDS_SAVEAS_DIALOG);
    if (!kind || kind == (FDS_OPEN_DIALOG | FDS_SAVEAS_DIALOG))
    {
        dialog->data.lSRC = FDS_ERR_INVALID_DIALOG;
        dialog->data.lReturn = 0;
        write_file(dialog);
        release_file(dialog);
        return 0;
    }
    TRACE_NATIVE("FILEDLG flags=%08X template=%u callback=%08X name='%s'", (uint)dialog->data.fl,
                 (uint)dialog->data.usDlgId, (uint)dialog->data.pfnDlgProc,
                 dialog->data.szFullFile);
    dialog->data.lReturn = 0;
    dialog->data.lSRC = FDS_SUCCESSFUL;
    write_file(dialog);
    PmFileDialog *previous = anchor->pending_file;
    anchor->pending_file = dialog;
    uint8 resource[TEMPLATE_BUFFER_SIZE];
    HWND handle = (dialog->data.fl & FDS_CUSTOM)
                      ? WinLoadDlg(parent, owner, file_dispatch, dialog->data.hMod,
                                   dialog->data.usDlgId, address)
                      : WinCreateDlg(parent, owner, file_dispatch,
                                     file_template(resource, sizeof resource), address);
    anchor->pending_file = previous;
    if (!handle)
    {
        dialog->data.lSRC = FDS_ERR_DIALOG_LOAD_ERROR;
        write_file(dialog);
        release_file(dialog);
        return 0;
    }
    Window *window = getWindowFromHWND(anchor, handle);
    if (window && !(dialog->data.fl & FDS_CUSTOM))
    {
        LONG x = dialog->data.x, y = dialog->data.y;
        if ((dialog->data.fl & FDS_CENTER) || (!x && !y))
        {
            x = (desktop_window.w - window->w) / 2;
            y = (desktop_window.h - window->h) / 2;
        }
        WinSetWindowPos(handle, HWND_TOP, x, y, window->w, window->h,
                        SWP_MOVE | SWP_SIZE | SWP_SHOW);
    }
    if (dialog->data.fl & FDS_MODELESS)
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

HWND WinFileDlg(HWND parent, HWND owner, PVOID data)
{
    return lx_pm_file_dialog(parent, owner, data);
}

MRESULT WinDefFileDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    TRACE_NATIVE("WinDefFileDlgProc(%u, %u, %p, %p)", (uint)window, (uint)message, first, second);
    return lx_pm_file_procedure(window, message, first, second);
}

BOOL WinFreeFileDlgList(PVOID list)
{
    return list == NULL;
}

LX_NATIVE_CONSTRUCTOR(file_dialog)
{
    lx_pm_add_window_destroy_hook(destroyStandardFile);
}
