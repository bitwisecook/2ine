/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosQueryExtLIBPATH(PSZ path, ULONG flags)
{
    if (!path || flags < BEGIN_LIBPATH || flags > LIBPATHSTRICT)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    if (flags == LIBPATHSTRICT)
        strcpy(path, GProcessState.libpath_strict ? "T" : "F");
    else
        strcpy(path,
               flags == BEGIN_LIBPATH ? GProcessState.begin_libpath : GProcessState.end_libpath);
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET DosSetExtLIBPATH(PSZ path, ULONG flags)
{
    if (flags < BEGIN_LIBPATH || flags > LIBPATHSTRICT || (path && strlen(path) >= 1024))
        return ERROR_INVALID_PARAMETER;
    if (flags == LIBPATHSTRICT && path && *path && strcasecmp(path, "T") && strcasecmp(path, "F"))
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    if (flags == LIBPATHSTRICT)
        GProcessState.libpath_strict = path && !strcasecmp(path, "T");
    else
        strcpy(flags == BEGIN_LIBPATH ? GProcessState.begin_libpath : GProcessState.end_libpath,
               path ? path : "");
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET16 Dos16QueryExtLIBPATH(PCHAR path, ULONG flags)
{
    return DosQueryExtLIBPATH(path, flags);
}

APIRET16 Dos16SetExtLIBPATH(PSZ path, ULONG flags)
{
    return DosSetExtLIBPATH(path, flags);
}
