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

APIRET DosDevConfig(PVOID pdevinfo, ULONG item)
{
    TRACE_NATIVE("DosDevConfig(%p, %u)", pdevinfo, (uint)item);

    if (pdevinfo == NULL)
        return ERROR_INVALID_PARAMETER;

    BYTE *info = (BYTE *)pdevinfo;
    switch (item)
    {
        case DEVINFO_PRINTER:
            *info = 0;
            break; // we don't report any printers.
        case DEVINFO_RS232:
            *info = 0;
            break; // we don't report any serial ports.
        case DEVINFO_FLOPPY:
            *info = 0;
            break; // we don't report any floppies.
        case DEVINFO_COPROCESSOR:
            *info = 1;
            break;
        case DEVINFO_SUBMODEL:
            *info = 1;
            break;
        case DEVINFO_MODEL:
            *info = 252;
            break;
        case DEVINFO_ADAPTER:
            *info = 1;
            break;
        default:
            return ERROR_INVALID_PARAMETER;
    }

    return NO_ERROR;
}

APIRET16 Dos16DevConfig(PVOID pdevinfo, USHORT item, USHORT reserved)
{
    TRACE_NATIVE("Dos16DevConfig(%p, %u, %u)", pdevinfo, (uint)item, (uint)reserved);
    return (APIRET16)DosDevConfig(pdevinfo, (ULONG)item);
}

APIRET16 Dos16GetVersion(PUSHORT pver)
{
    TRACE_NATIVE("Dos16GetVersion(%p)", pver);
    uint8 *bytes = (uint8 *)pver;
    bytes[0] = 45; // OS/2 Warp 4.52 reports this.
    bytes[1] = 20;
    return NO_ERROR;
}

APIRET16 Dos16GetMachineMode(PBYTE pmode)
{
    TRACE_NATIVE("Dos16GetMachineMode(%p)", pmode);
    *pmode = 1; // 1==OS/2 mode (as opposed to zero for DOS mode).
    return NO_ERROR;
}

APIRET16 Dos16GetInfoSeg(PSEL globalseg, PSEL localseg)
{
    TRACE_NATIVE("Dos16GetInfoSeg(%p, %p)", globalseg, localseg);
    if (getenv("TWOINE_TRACE_SEGMENTS"))
        fprintf(stderr, "2INE InfoSeg: ginfosel=%u(%04X) linfosel=%u(%04X) -> %04X/%04X\n",
                (uint)ginfosel, (uint)(ginfosel << 3 | 7), (uint)linfosel,
                (uint)(linfosel << 3 | 7), (uint)(ginfosel << 3 | 7), (uint)(linfosel << 3 | 7));
    if (globalseg)
        *globalseg = ginfosel << 3 | 7;
    if (localseg)
        *localseg = linfosel << 3 | 7;
    return NO_ERROR;
}
