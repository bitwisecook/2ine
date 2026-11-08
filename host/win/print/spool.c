/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows spooler: printer list and raw PCL jobs; linked with winspool. */
#include "host/print.h"
#include "host/win/print/internal.h"
#include <windows.h>
#include <winspool.h>
#include <stdlib.h>
#include <string.h>

#define DOCUMENT_NAME_CHARS 4096
#define COPY_BUFFER_BYTES 65536
/* EnumPrinters level 4: PRINTER_INFO_4W names only, without querying each printer. */
#define PRINTER_INFO_NAMES 4

char *lx_win_print_utf8(const WCHAR *name)
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
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, PRINTER_INFO_NAMES, NULL, 0,
                  &bytes, &returned);
    if (!bytes)
        return NULL;
    BYTE *data = malloc(bytes);
    if (!data || !EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL,
                                PRINTER_INFO_NAMES, data, bytes, &bytes, &returned))
    {
        free(data);
        return NULL;
    }
    char **printers = calloc(returned ? returned : 1, sizeof *printers);
    if (printers)
        for (DWORD i = 0; i < returned; i++)
        {
            char *name = lx_win_print_utf8(((PRINTER_INFO_4W *)data)[i].pPrinterName);
            if (name)
                printers[(*count)++] = name;
        }
    free(data);
    return printers;
}

int lx_host_print_submit_pcl(const char *printer, const char *title, unsigned copies, FILE *source)
{
    if (!printer || !*printer || !source || !copies || copies > LX_WIN_PRINT_MAX_COPIES)
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
    WCHAR document[DOCUMENT_NAME_CHARS] = L"2ine Print Job";
    if (title && *title)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title, -1, document,
                            DOCUMENT_NAME_CHARS);
    DOC_INFO_1W info = {document, NULL, L"RAW"};
    for (unsigned copy = 0; ok && copy < copies; copy++)
    {
        ok = !fflush(source) && !fseek(source, 0, SEEK_SET) &&
             StartDocPrinterW(handle, 1, (LPBYTE)&info) != 0;
        int started = ok;
        if (ok)
            ok = StartPagePrinter(handle) != 0;
        unsigned char buffer[COPY_BUFFER_BYTES];
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
