/* Public domain. No rights reserved. Wholly AI-authored. */
/* Whole-file operations by path: copy, move, delete, directories and attributes. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/bsedos.h"
#include <stddef.h>

BOOL CopyFile(PSZ source, PSZ destination, BOOL exclusive)
{
    typedef APIRET(OS2API * Fn)(PSZ, PSZ, ULONG);
    Fn call = (Fn)open32DosService("DosCopy");
    ULONG error = !source || !destination ? ERROR_INVALID_PARAMETER
                  : call                  ? call(source, destination, exclusive ? 0 : DCPY_EXISTING)
                                          : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
        open32SetError(error);
    return !error;
}

static BOOL path_operation(PSZ path, const char *name)
{
    typedef APIRET(OS2API * Fn)(PSZ);
    Fn call = (Fn)open32DosService(name);
    ULONG error = !path ? ERROR_INVALID_PARAMETER : call ? call(path) : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
        open32SetError(error);
    return !error;
}

BOOL DeleteFile(PSZ path)
{
    return path_operation(path, "DosDelete");
}

BOOL RemoveDirectory(PSZ path)
{
    return path_operation(path, "DosDeleteDir");
}

BOOL MoveFile(PSZ source, PSZ destination)
{
    typedef APIRET(OS2API * Fn)(PSZ, PSZ);
    Fn call = (Fn)open32DosService("DosMove");
    ULONG error = !source || !destination ? ERROR_INVALID_PARAMETER
                  : call                  ? call(source, destination)
                                          : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
        open32SetError(error);
    return !error;
}

BOOL CreateDirectory(PSZ path, PVOID security)
{
    (void)security; /* Open32 passes no security information to DosCreateDir. */
    typedef APIRET(OS2API * Fn)(PSZ, PVOID);
    Fn call = (Fn)open32DosService("DosCreateDir");
    ULONG error = !path  ? ERROR_INVALID_PARAMETER
                  : call ? call(path, NULL)
                         : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
        open32SetError(error);
    return !error;
}

BOOL SetFileAttributes(PSZ path, ULONG attributes)
{
    typedef APIRET(OS2API * Query)(PSZ, ULONG, PVOID, ULONG);
    typedef APIRET(OS2API * Set)(PSZ, ULONG, PVOID, ULONG, ULONG);
    Query query = (Query)open32DosService("DosQueryPathInfo");
    Set set = (Set)open32DosService("DosSetPathInfo");
    uint8 status[sizeof(FILESTATUS3)] = {0};
    /* FILE_ATTRIBUTE_NORMAL stands alone; TEMPORARY is accepted and has no OS/2 bit. The
       READONLY, HIDDEN, SYSTEM and ARCHIVE bits have the same values in both APIs. */
    const unsigned accepted = WIN32_FILE_ATTRIBUTE_READONLY | WIN32_FILE_ATTRIBUTE_HIDDEN |
                              WIN32_FILE_ATTRIBUTE_SYSTEM | WIN32_FILE_ATTRIBUTE_ARCHIVE |
                              WIN32_FILE_ATTRIBUTE_NORMAL | WIN32_FILE_ATTRIBUTE_TEMPORARY;
    const unsigned settable = FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED;
    ULONG error = !path || (attributes & ~accepted) ||
                          ((attributes & WIN32_FILE_ATTRIBUTE_NORMAL) &&
                           (attributes & ~(unsigned)WIN32_FILE_ATTRIBUTE_NORMAL))
                      ? ERROR_INVALID_PARAMETER
                  : !query || !set ? ERROR_CALL_NOT_IMPLEMENTED
                                   : query(path, FIL_STANDARD, status, sizeof status);
    if (!error)
    {
        lx_guest_store_u32(status + offsetof(FILESTATUS3, attrFile), attributes & settable);
        error = set(path, FIL_STANDARD, status, sizeof status, 0);
    }
    if (error)
        open32SetError(error);
    return !error;
}
