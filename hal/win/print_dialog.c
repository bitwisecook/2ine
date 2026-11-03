/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows common save dialog adapter; linked with comdlg32 by a Windows host. */
#include "hal/print.h"
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <winspool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
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
    WCHAR filename[32768] = {0}, suffix[16] = {0};
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title && *title ? title : "Print Job",
                             -1, filename, 32768) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, extension, -1, suffix, 16))
        return LX_HOST_SAVE_ERROR;
    for (WCHAR *p = filename; *p; p++)
        if (*p < 32 || wcschr(L"<>:\"/\\|?*", *p))
            *p = L'-';
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof dialog;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = 32768;
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

void lx_host_print_destination_clear(LxHostPrintDestination *destination)
{
    if (destination)
    {
        free(destination->path);
        free(destination->printer);
        memset(destination, 0, sizeof *destination);
    }
}
static char *printer_utf8(const WCHAR *name)
{
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1, NULL, 0, NULL, NULL);
    char *result = bytes > 0 ? malloc((size_t)bytes) : NULL;
    if (result &&
        !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1, result, bytes, NULL, NULL))
    {
        free(result);
        result = NULL;
    }
    return result;
}
char **lx_host_print_printers(size_t *count)
{
    if (!count)
        return NULL;
    *count = 0;
    DWORD bytes = 0, returned = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 4, NULL, 0, &bytes,
                  &returned);
    if (!bytes)
        return NULL;
    BYTE *data = malloc(bytes);
    if (!data || !EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 4, data, bytes,
                                &bytes, &returned))
    {
        free(data);
        return NULL;
    }
    char **printers = calloc(returned ? returned : 1, sizeof *printers);
    if (printers)
        for (DWORD i = 0; i < returned; i++)
        {
            char *name = printer_utf8(((PRINTER_INFO_4W *)data)[i].pPrinterName);
            if (name)
                printers[(*count)++] = name;
        }
    free(data);
    return printers;
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
            destination->printer = printer_utf8((WCHAR *)names + names->wDeviceOffset);
            GlobalUnlock(dialog.hDevNames);
            result = destination->printer ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
        }
    }
    if (dialog.hDevMode)
    {
        DEVMODEW *mode = GlobalLock(dialog.hDevMode);
        if (mode && (mode->dmFields & DM_COPIES) && mode->dmCopies > 0 && mode->dmCopies <= 999)
            destination->copies = (unsigned)mode->dmCopies;
        if (mode)
            GlobalUnlock(dialog.hDevMode);
        GlobalFree(dialog.hDevMode);
    }
    if (dialog.hDevNames)
        GlobalFree(dialog.hDevNames);
    return result;
}
int lx_host_print_submit_pcl(const char *printer, const char *title, unsigned copies, FILE *source)
{
    if (!printer || !*printer || !source || !copies || copies > 999)
        return 0;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, printer, -1, NULL, 0);
    WCHAR *name = count > 0 ? malloc((size_t)count * sizeof *name) : NULL;
    if (!name || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, printer, -1, name, count))
    {
        free(name);
        return 0;
    }
    HANDLE handle = NULL;
    int ok = OpenPrinterW(name, &handle, NULL) != 0;
    free(name);
    if (!ok)
        return 0;
    WCHAR document[4096] = L"2ine Print Job";
    if (title && *title)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title, -1, document, 4096);
    DOC_INFO_1W info = {document, NULL, L"RAW"};
    for (unsigned copy = 0; ok && copy < copies; copy++)
    {
        ok = !fflush(source) && !fseek(source, 0, SEEK_SET) &&
             StartDocPrinterW(handle, 1, (LPBYTE)&info) != 0;
        int started = ok;
        if (ok)
            ok = StartPagePrinter(handle) != 0;
        unsigned char buffer[65536];
        size_t bytes;
        while (ok && (bytes = fread(buffer, 1, sizeof buffer, source)))
        {
            DWORD written = 0;
            ok = WritePrinter(handle, buffer, (DWORD)bytes, &written) && written == bytes;
        }
        ok = ok && !ferror(source) && EndPagePrinter(handle) && EndDocPrinter(handle);
        if (!ok && started)
            AbortPrinter(handle);
    }
    ClosePrinter(handle);
    return ok;
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
    TASKDIALOG_BUTTON buttons[] = {{100, L"PDF"}, {101, L"PCL Printer"}};
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
    if (selected != 100 && selected != 101)
        return LX_HOST_SAVE_CANCEL;
    *kind = selected == 100 ? 1 : 2;
    return LX_HOST_SAVE_OK;
}

int lx_host_print_submit_helper(const char *program, const char *printer, const char *title,
                                unsigned copies, FILE *source)
{
    (void)program;
    (void)printer;
    (void)title;
    (void)copies;
    (void)source;
    return 0; /* Windows submits through the spool API, not an external helper. */
}
