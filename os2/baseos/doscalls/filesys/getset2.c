/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

static USHORT write_verify;

APIRET16 Dos16QVerify(PUSHORT enabled)
{
    if (!enabled)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    *enabled = write_verify;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET16 Dos16SetVerify(USHORT enabled)
{
    if (enabled > 1)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    write_verify = enabled;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET DosQueryCurrentDisk_implementation(PULONG pdisknum, PULONG pdiskmap)
{
    *pdisknum = GProcessState.current_disk;
    *pdiskmap = GProcessState.diskmap;
    return NO_ERROR;
}

APIRET DosQueryCurrentDisk(PULONG pdisknum, PULONG pdiskmap)
{
    TRACE_NATIVE("DosQueryCurrentDisk(%p, %p)", pdisknum, pdiskmap);
    return DosQueryCurrentDisk_implementation(pdisknum, pdiskmap);
}

APIRET16 Dos16SelectDisk(USHORT drive)
{
    TRACE_NATIVE("Dos16SelectDisk(%u)", (uint)drive);
    return (APIRET16)DosSetDefaultDisk((ULONG)drive);
}

APIRET16 Dos16QCurDisk(PUSHORT drivenum, PULONG drivemap)
{
    TRACE_NATIVE("Dos16QCurDisk(%p, %p)", drivenum, drivemap);
    ULONG uldrivenum;
    const APIRET rc = DosQueryCurrentDisk_implementation(&uldrivenum, drivemap);
    *drivenum = (USHORT)uldrivenum;
    return (APIRET16)rc;
}

APIRET DosSetDefaultDisk(ULONG disknum)
{
    TRACE_NATIVE("DosSetDefaultDisk(%u)", (uint)disknum);

    if ((disknum < 1) || (disknum > 26) || (!GProcessState.disks[disknum - 1]))
        return ERROR_INVALID_DRIVE;

    GProcessState.current_disk = disknum;
    return NO_ERROR;
}
