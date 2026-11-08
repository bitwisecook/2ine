/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows save, print and destination dialogs; linked with comdlg32 and comctl32. */
#include "host/print.h"
#include "host/win/print/internal.h"
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* The longest Windows path, in UTF-16 units, and the default extension buffer. */
#define PATH_CHARS 32768
#define SUFFIX_CHARS 16
#define DEVICE_BUTTON_PDF 100
#define DEVICE_BUTTON_PCL 101

LxHostSaveResult lx_host_print_save_path(const char *title, const char *extension, char **path)
{
    if (!path || !extension)
        return LX_HOST_SAVE_ERROR;
    *path = NULL;
    const char *override = getenv("TWOINE_PRINT_OUTPUT");
    if (override)
    {
        if (!*override)
            return LX_HOST_SAVE_CANCEL;
        *path = _strdup(override);
        return *path ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
    }
    WCHAR filename[PATH_CHARS] = {0}, suffix[SUFFIX_CHARS] = {0};
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title && *title ? title : "Print Job",
                             -1, filename, PATH_CHARS) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, extension, -1, suffix, SUFFIX_CHARS))
        return LX_HOST_SAVE_ERROR;
    for (WCHAR *p = filename; *p; p++)
        if (*p < 32 || wcschr(L"<>:\"/\\|?*", *p))
            *p = L'-';
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = PATH_CHARS;
    dialog.lpstrDefExt = suffix;
    dialog.lpstrTitle = L"Save Print Output";
    dialog.lpstrFilter = !strcmp(extension, "pdf") ? L"PDF documents\0*.pdf\0All files\0*.*\0\0"
                                                   : L"PCL print files\0*.pcl\0All files\0*.*\0\0";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&dialog))
        return CommDlgExtendedError() ? LX_HOST_SAVE_ERROR : LX_HOST_SAVE_CANCEL;
    int count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, filename, -1, NULL, 0, NULL, NULL);
    if (count <= 0 || !(*path = malloc((size_t)count)))
        return LX_HOST_SAVE_ERROR;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, filename, -1, *path, count, NULL, NULL))
    {
        free(*path);
        *path = NULL;
        return LX_HOST_SAVE_ERROR;
    }
    return LX_HOST_SAVE_OK;
}

LxHostSaveResult lx_host_print_choose_pcl(const char *title, LxHostPrintDestination *destination)
{
    if (!destination)
        return LX_HOST_SAVE_ERROR;
    memset(destination, 0, sizeof *destination);
    destination->copies = 1;
    if (getenv("TWOINE_PRINT_OUTPUT"))
        return lx_host_print_save_path(title, "pcl", &destination->path);
    const char *override = getenv("TWOINE_PRINT_PRINTER");
    if (override)
    {
        if (!*override)
            return LX_HOST_SAVE_CANCEL;
        destination->printer = _strdup(override);
        return destination->printer ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
    }
    PRINTDLGW dialog = {0};
    dialog.lStructSize = sizeof dialog;
    dialog.Flags = PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    dialog.nCopies = 1;
    if (!PrintDlgW(&dialog))
    {
        if (dialog.hDevMode)
            GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames)
            GlobalFree(dialog.hDevNames);
        return CommDlgExtendedError() ? LX_HOST_SAVE_ERROR : LX_HOST_SAVE_CANCEL;
    }
    LxHostSaveResult result = LX_HOST_SAVE_ERROR;
    if (dialog.Flags & PD_PRINTTOFILE)
        result = lx_host_print_save_path(title, "pcl", &destination->path);
    else if (dialog.hDevNames)
    {
        DEVNAMES *names = GlobalLock(dialog.hDevNames);
        if (names)
        {
            destination->printer = lx_win_print_utf8((WCHAR *)names + names->wDeviceOffset);
            GlobalUnlock(dialog.hDevNames);
            result = destination->printer ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
        }
    }
    if (dialog.hDevMode)
    {
        DEVMODEW *mode = GlobalLock(dialog.hDevMode);
        if (mode && (mode->dmFields & DM_COPIES) && mode->dmCopies > 0 &&
            mode->dmCopies <= LX_WIN_PRINT_MAX_COPIES)
            destination->copies = (unsigned)mode->dmCopies;
        if (mode)
            GlobalUnlock(dialog.hDevMode);
        GlobalFree(dialog.hDevMode);
    }
    if (dialog.hDevNames)
        GlobalFree(dialog.hDevNames);
    return result;
}

LxHostSaveResult lx_host_print_choose_device(unsigned *kind)
{
    if (!kind)
        return LX_HOST_SAVE_ERROR;
    const char *override = getenv("TWOINE_PRINT_DEVICE");
    if (override || getenv("TWOINE_PRINT_OUTPUT") || getenv("TWOINE_PRINT_PRINTER"))
    {
        if (override && !*override)
            return LX_HOST_SAVE_CANCEL;
        if (override && strcmp(override, "pdf") && strcmp(override, "pcl"))
            return LX_HOST_SAVE_ERROR;
        *kind = override                         ? (!strcmp(override, "pcl") ? 2 : 1)
                : getenv("TWOINE_PRINT_PRINTER") ? 2
                                                 : 1;
        return LX_HOST_SAVE_OK;
    }
    TASKDIALOG_BUTTON buttons[] = {{DEVICE_BUTTON_PDF, L"PDF"},
                                   {DEVICE_BUTTON_PCL, L"PCL Printer"}};
    TASKDIALOGCONFIG dialog = {0};
    dialog.cbSize = sizeof dialog;
    dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    dialog.pszWindowTitle = L"Print Destination";
    dialog.pszMainInstruction = L"Choose a print destination";
    dialog.pszContent =
        L"PDF saves a document. PCL prints to a compatible system printer or saves a PCL file.";
    dialog.cButtons = 2;
    dialog.pButtons = buttons;
    int selected = 0;
    if (FAILED(TaskDialogIndirect(&dialog, &selected, NULL, NULL)))
        return LX_HOST_SAVE_ERROR;
    if (selected != DEVICE_BUTTON_PDF && selected != DEVICE_BUTTON_PCL)
        return LX_HOST_SAVE_CANCEL;
    *kind = selected == DEVICE_BUTTON_PDF ? 1 : 2;
    return LX_HOST_SAVE_OK;
}
