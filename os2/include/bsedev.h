/* Public domain. No rights reserved. Wholly AI-authored. */
/* DosDevIOCtl categories, functions and parameter records. */
#ifndef _INCL_BSEDEV_H_
#define _INCL_BSEDEV_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        IOCTL_DISK = 0x0008
    };

    enum
    {
        DSK_GETDEVICEPARAMS = 0x0063
    };

    enum
    {
        GET_BPB_FROM_DEVICE = 0x00,
        GET_BPB_FROM_MEDIUM = 0x01
    };

#pragma pack(push, 1)

    typedef struct _BIOSPARAMETERBLOCK
    {
        USHORT usBytesPerSector;
        BYTE bSectorsPerCluster;
        USHORT usReservedSectors;
        BYTE cFATs;
        USHORT cRootEntries;
        USHORT cSectors;
        BYTE bMedia;
        USHORT usSectorsPerFAT;
        USHORT usSectorsPerTrack;
        USHORT cHeads;
        ULONG cHiddenSectors;
        ULONG cLargeSectors;
        BYTE abReserved[6];
        USHORT cCylinders;
        BYTE bDeviceType;
        USHORT fsDeviceAttr;
    } BIOSPARAMETERBLOCK, *PBIOSPARAMETERBLOCK;

#pragma pack(pop)

    enum
    {
        DEVTYPE_48TPI = 0x0000,
        DEVTYPE_96TPI = 0x0001,
        DEVTYPE_35 = 0x0002,
        DEVTYPE_8SD = 0x0003,
        DEVTYPE_8DD = 0x0004,
        DEVTYPE_FIXED = 0x0005,
        DEVTYPE_TAPE = 0x0006,
        DEVTYPE_UNKNOWN = 0x0007
    };

#ifdef __cplusplus
}
#endif

#endif
