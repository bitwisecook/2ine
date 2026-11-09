/* Public domain. No rights reserved. Wholly AI-authored. */
/* Whole-file operations by path: copy, move, delete, directories and attributes. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"

BOOL CopyFile(PSZ source, PSZ destination, BOOL exclusive)
{
    typedef APIRET(OS2API * Fn)(PSZ, PSZ, ULONG);
    Fn call = (Fn)open32DosService("DosCopy");
    ULONG error = !source || !destination ? 87
                  : call                  ? call(source, destination, exclusive ? 0 : 1)
                                          : 120;
    if (error)
        open32SetError(error);
    return !error;
}

static BOOL path_operation(PSZ path, const char *name)
{
    typedef APIRET(OS2API * Fn)(PSZ);
    Fn call = (Fn)open32DosService(name);
    ULONG error = !path ? 87 : call ? call(path) : 120;
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
    ULONG error = !source || !destination ? 87 : call ? call(source, destination) : 120;
    if (error)
        open32SetError(error);
    return !error;
}

BOOL CreateDirectory(PSZ path, PVOID security)
{
    (void)security; /* Open32 passes no security information to DosCreateDir. */
    typedef APIRET(OS2API * Fn)(PSZ, PVOID);
    Fn call = (Fn)open32DosService("DosCreateDir");
    ULONG error = !path ? 87 : call ? call(path, NULL) : 120;
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
    uint8 status[24] = {0};
    /* FILE_ATTRIBUTE_NORMAL stands alone; TEMPORARY is accepted and has no OS/2 bit. */
    ULONG error = !path || (attributes & ~0x1A7u) || ((attributes & 0x80) && (attributes & ~0x80u))
                      ? 87
                  : !query || !set ? 120
                                   : query(path, 1, status, sizeof status);
    if (!error)
    {
        lx_guest_store_u32(status + 20, attributes & 0x27);
        error = set(path, 1, status, sizeof status, 0);
    }
    if (error)
        open32SetError(error);
    return !error;
}
