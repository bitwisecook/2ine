/* Public domain. No rights reserved. Wholly AI-authored. */
/* ShellExecute opens a program through PMSHAPI or a document through its WPS object or
   association. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/private/pmp.h"
#include <stdio.h>
#include <strings.h>

/* ShellExecute's error results (values up to 32), and PMSHAPI's error for a file that is not
   a program. */
enum
{
    SE_FILE_NOT_FOUND = 2,
    SE_BAD_FORMAT = 11,
    SE_NO_ASSOCIATION = 31,
    PMERR_INVALID_APPL_ID = 0x1530
};

/* Win32 SW_* show commands, by number. */
enum
{
    SW_HIDE = 0,
    SW_SHOWNORMAL = 1,
    SW_SHOWMINIMIZED = 2,
    SW_SHOWMAXIMIZED = 3,
    SW_SHOWNOACTIVATE = 4,
    SW_SHOW = 5,
    SW_MINIMIZE = 6,
    SW_SHOWMINNOACTIVE = 7,
    SW_SHOWNA = 8,
    SW_RESTORE = 9,
    SW_SHOWDEFAULT = 10
};

/* The PM SWP_* flags of a program's initial window. */
static ULONG show_flags(LONG show)
{
    switch (show)
    {
        case SW_HIDE:
            return SWP_HIDE;
        case SW_SHOWNORMAL:
        case SW_RESTORE:
        case SW_SHOWDEFAULT:
            return SWP_RESTORE | SWP_ACTIVATE | SWP_SHOW;
        case SW_SHOWMINIMIZED:
            return SWP_MINIMIZE | SWP_ACTIVATE | SWP_SHOW;
        case SW_SHOWMAXIMIZED:
            return SWP_MAXIMIZE | SWP_ACTIVATE | SWP_SHOW;
        case SW_SHOWNOACTIVATE:
        case SW_SHOWNA:
            return SWP_SHOW;
        case SW_SHOW:
            return SWP_ACTIVATE | SWP_SHOW;
        case SW_MINIMIZE:
        case SW_SHOWMINNOACTIVE:
            return SWP_MINIMIZE;
        default:
            return 0;
    }
}

static ULONG start_program(PSZ executable, PSZ path, PSZ parameters, PSZ directory, LONG show)
{
    PROGDETAILS details = {.Length = 72,
                           .fbVisible = show == 0,
                           .pszExecutable = executable,
                           .pszParameters = parameters,
                           .pszStartupDir = directory};
    details.swpInitial.fl = show_flags(show);
    ULONG error;
    if (lx_pmshapi_start_app(0, &details, parameters, 0, &error))
        return UINT32_MAX;
    typedef APIRET(OS2API * InfoFn)(PSZ, ULONG, PVOID, ULONG);
    InfoFn query = (InfoFn)open32DosService("DosQueryPathInfo");
    uint8 information[24];
    ULONG missing = query ? query(path, 1, information, sizeof information) : 0;
    if (missing)
        return missing;
    return error == PMERR_INVALID_APPL_ID ? SE_BAD_FORMAT : SE_NO_ASSOCIATION;
}

static ULONG open_document(PSZ path)
{
    ULONG object = WinQueryObject(path);
    if (object)
        return WinOpenObject(object, 0, FALSE) ? UINT32_MAX : SE_NO_ASSOCIATION;
    typedef APIRET(OS2API * InfoFn)(PSZ, ULONG, PVOID, ULONG);
    InfoFn query = (InfoFn)open32DosService("DosQueryPathInfo");
    uint8 information[24];
    if (!query || query(path, 1, information, sizeof information))
        return SE_FILE_NOT_FOUND;
    return lx_pmwp_open_associated(path) ? UINT32_MAX : SE_NO_ASSOCIATION;
}

ULONG ShellExecute(ULONG window, PSZ operation, PSZ file, PSZ parameters, PSZ directory, LONG show)
{
    (void)window;
    if ((operation && strcasecmp(operation, "open")) || !file)
        return SE_NO_ASSOCIATION;
    size_t base = directory ? strlen(directory) : 0;
    size_t size = base + strlen(file) + 6;
    char *path = malloc(size), *executable = malloc(strlen(file) + 5);
    ULONG result = 8;
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
