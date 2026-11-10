/* Public domain. No rights reserved. Wholly AI-authored. */
/* ShellExecute opens a program through PMSHAPI or a document through its WPS object or
   association. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/bsedos.h"
#include "os2/include/private/pmp.h"
#include <stdio.h>
#include <strings.h>

/* PMSHAPI's error for a file that is not a program. */
enum
{
    PMERR_INVALID_APPL_ID = 0x1530
};

enum
{
    /* PROGDETAILS and FILESTATUS3 as a 32-bit caller lays them out. */
    PROGDETAILS_SIZE = 72,
    FILESTATUS3_SIZE = 24,
    /* Room for an appended ".exe" and a joining backslash. */
    EXE_SUFFIX_LENGTH = 4
};

/* The PM SWP_* flags of a program's initial window. */
static ULONG show_flags(LONG show)
{
    switch (show)
    {
        case WIN32_SW_HIDE:
            return SWP_HIDE;
        case WIN32_SW_SHOWNORMAL:
        case WIN32_SW_RESTORE:
        case WIN32_SW_SHOWDEFAULT:
            return SWP_RESTORE | SWP_ACTIVATE | SWP_SHOW;
        case WIN32_SW_SHOWMINIMIZED:
            return SWP_MINIMIZE | SWP_ACTIVATE | SWP_SHOW;
        case WIN32_SW_SHOWMAXIMIZED:
            return SWP_MAXIMIZE | SWP_ACTIVATE | SWP_SHOW;
        case WIN32_SW_SHOWNOACTIVATE:
        case WIN32_SW_SHOWNA:
            return SWP_SHOW;
        case WIN32_SW_SHOW:
            return SWP_ACTIVATE | SWP_SHOW;
        case WIN32_SW_MINIMIZE:
        case WIN32_SW_SHOWMINNOACTIVE:
            return SWP_MINIMIZE;
        default:
            return 0;
    }
}

static ULONG start_program(PSZ executable, PSZ path, PSZ parameters, PSZ directory, LONG show)
{
    PROGDETAILS details = {.Length = PROGDETAILS_SIZE,
                           .fbVisible = show == WIN32_SW_HIDE,
                           .pszExecutable = executable,
                           .pszParameters = parameters,
                           .pszStartupDir = directory};
    details.swpInitial.fl = show_flags(show);
    ULONG error;
    if (lx_pmshapi_start_app(0, &details, parameters, 0, &error))
        return UINT32_MAX;
    typedef APIRET(OS2API * InfoFn)(PSZ, ULONG, PVOID, ULONG);
    InfoFn query = (InfoFn)open32DosService("DosQueryPathInfo");
    uint8 information[FILESTATUS3_SIZE];
    ULONG missing = query ? query(path, FIL_STANDARD, information, sizeof information) : 0;
    /* ShellExecute reports a missing file with the DosQueryPathInfo error itself. */
    if (missing)
        return missing;
    return error == PMERR_INVALID_APPL_ID ? ERROR_BAD_FORMAT : WIN32_SE_ERR_NOASSOC;
}

static ULONG open_document(PSZ path)
{
    ULONG object = WinQueryObject(path);
    if (object)
        return WinOpenObject(object, 0, FALSE) ? UINT32_MAX : WIN32_SE_ERR_NOASSOC;
    typedef APIRET(OS2API * InfoFn)(PSZ, ULONG, PVOID, ULONG);
    InfoFn query = (InfoFn)open32DosService("DosQueryPathInfo");
    uint8 information[FILESTATUS3_SIZE];
    if (!query || query(path, FIL_STANDARD, information, sizeof information))
        return WIN32_SE_ERR_FNF;
    return lx_pmwp_open_associated(path) ? UINT32_MAX : WIN32_SE_ERR_NOASSOC;
}

ULONG ShellExecute(ULONG window, PSZ operation, PSZ file, PSZ parameters, PSZ directory, LONG show)
{
    (void)window;
    if ((operation && strcasecmp(operation, "open")) || !file)
        return WIN32_SE_ERR_NOASSOC;
    size_t base = directory ? strlen(directory) : 0;
    size_t size = base + strlen(file) + EXE_SUFFIX_LENGTH + 2;
    char *path = malloc(size), *executable = malloc(strlen(file) + EXE_SUFFIX_LENGTH + 1);
    ULONG result = WIN32_SE_ERR_OOM;
    if (path && executable)
    {
        BOOL separator = base && directory[base - 1] != '\\';
        snprintf(path, size, "%s%s%s", directory ? directory : "", separator ? "\\" : "", file);
        strcpy(executable, file);
        const char *extension = strrchr(path, '.');
        if (extension && strcasecmp(extension, ".exe"))
            result = open_document(path);
        else
        {
            if (!extension)
            {
                strcat(path, ".exe");
                strcat(executable, ".exe");
            }
            result = start_program(executable, path, parameters, directory, show);
        }
    }
    free(executable);
    free(path);
    return result;
}
