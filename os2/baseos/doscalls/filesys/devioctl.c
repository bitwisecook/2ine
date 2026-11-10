/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* DosDevIOCtl: the disk parameter request, for drives backed by host directories. */
#include "os2/baseos/doscalls/internal.h"
#include "os2/include/bsedev.h"
#include <stddef.h>

_Static_assert(sizeof(BIOSPARAMETERBLOCK) == 36, "BIOSPARAMETERBLOCK is the 36-byte BPB record");

APIRET16 Dos16DevIOCtl(PVOID pdata, PVOID pparms, USHORT fun, USHORT category, USHORT hdev)
{
    TRACE_NATIVE("Dos16DevIOCtl(%p, %p, %u, %u, %u)", pdata, pparms, (uint)fun, (uint)category,
                 (uint)hdev);
    return ERROR_INVALID_FUNCTION;
}

/* Fixed disks report the BIOS's LBA-assisted geometry and media descriptor. */
enum
{
    FIXED_DISK_SECTORS_PER_TRACK = 63,
    FIXED_DISK_HEADS = 255,
    FIXED_DISK_MEDIA = 0xF8
};

/* DSK_GETDEVICEPARAMS parameter packet: a command byte, then the drive (0 = A:). */
enum
{
    DEVICEPARAMS_COMMAND_OFFSET = 0,
    DEVICEPARAMS_DRIVE_OFFSET = 1,
    DEVICEPARAMS_PACKET_SIZE = 2
};

/* fsDeviceAttr bit 0: the medium cannot be removed. */
enum
{
    DEVICE_ATTR_NONREMOVABLE = 0x0001
};

APIRET DosDevIOCtl(HFILE device, ULONG category, ULONG function, PVOID parameters,
                   ULONG parameter_max, PULONG parameter_size, PVOID data, ULONG data_max,
                   PULONG data_size)
{
    TRACE_NATIVE("DosDevIOCtl(%u, %u, %u, %p, %u, %p, %p, %u, %p)", (uint)device, (uint)category,
                 (uint)function, parameters, (uint)parameter_max, parameter_size, data,
                 (uint)data_max, data_size);
    /* Disk utilities pass no handle for drive parameters; 2ine has no device handles. */
    if (device == (HFILE)-1 && category == IOCTL_DISK && function == DSK_GETDEVICEPARAMS)
    {
        if (!parameters || parameter_max < DEVICEPARAMS_PACKET_SIZE || !data || !data_size)
            return ERROR_INVALID_PARAMETER;
        const uint8 *input = parameters;
        const uint8 command = input[DEVICEPARAMS_COMMAND_OFFSET];
        const uint8 drive = input[DEVICEPARAMS_DRIVE_OFFSET];
        TRACE_NATIVE("DSK_GETDEVICEPARAMS command=%u drive=%u", command, drive);
        if (command > GET_BPB_FROM_MEDIUM || drive >= DRIVE_COUNT || !GProcessState.disks[drive])
            return ERROR_INVALID_DRIVE;
        if (data_max < sizeof(BIOSPARAMETERBLOCK))
        {
            *data_size = sizeof(BIOSPARAMETERBLOCK);
            return ERROR_BUFFER_OVERFLOW;
        }
        uint64_t total, available;
        LxHostFsError error = lx_host_path_space(GProcessState.disks[drive], &total, &available);
        if (error)
            return fileError(error);
        LxHostVolumeInfo volume;
        error = lx_host_path_volume(GProcessState.disks[drive], &volume);
        if (error)
            return fileError(error);
        int floppy = volume.removable && !strcmp(volume.filesystem, "FAT");
        unsigned sector_bytes = volume.sector_bytes;
        unsigned track = floppy ? volume.sectors_per_track : FIXED_DISK_SECTORS_PER_TRACK;
        unsigned heads = floppy ? volume.heads : FIXED_DISK_HEADS;
        uint64_t sectors = total / sector_bytes;
        uint64_t cylinders = track && heads ? sectors / ((uint64_t)track * heads) : 0;
        uint8 *output = data;
        memset(output, 0, sizeof(BIOSPARAMETERBLOCK));
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, usBytesPerSector), sector_bytes);
        output[offsetof(BIOSPARAMETERBLOCK, bSectorsPerCluster)] =
            volume.cluster_bytes / sector_bytes;
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, usReservedSectors),
                           volume.reserved_sectors);
        output[offsetof(BIOSPARAMETERBLOCK, cFATs)] = volume.fat_count;
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, cRootEntries),
                           volume.root_entries);
        /* Volumes too large for the 16-bit count report zero here and use cLargeSectors. */
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, cSectors),
                           sectors <= UINT16_MAX ? sectors : 0);
        output[offsetof(BIOSPARAMETERBLOCK, bMedia)] = floppy ? volume.media : FIXED_DISK_MEDIA;
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, usSectorsPerFAT),
                           volume.fat_sectors);
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, usSectorsPerTrack), track);
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, cHeads), heads);
        lx_guest_store_u32(output + offsetof(BIOSPARAMETERBLOCK, cHiddenSectors),
                           volume.hidden_sectors);
        lx_guest_store_u32(output + offsetof(BIOSPARAMETERBLOCK, cLargeSectors),
                           sectors > UINT32_MAX ? UINT32_MAX : sectors);
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, cCylinders),
                           cylinders > UINT16_MAX ? UINT16_MAX : cylinders);
        output[offsetof(BIOSPARAMETERBLOCK, bDeviceType)] = floppy             ? DEVTYPE_35
                                                            : volume.removable ? DEVTYPE_UNKNOWN
                                                                               : DEVTYPE_FIXED;
        lx_guest_store_u16(output + offsetof(BIOSPARAMETERBLOCK, fsDeviceAttr),
                           volume.removable ? 0 : DEVICE_ATTR_NONREMOVABLE);
        *data_size = sizeof(BIOSPARAMETERBLOCK);
        if (parameter_size)
            *parameter_size = DEVICEPARAMS_PACKET_SIZE;
        return NO_ERROR;
    }
    LxHandleBorrow file;
    if (!borrowFile(device, &file))
        return ERROR_INVALID_HANDLE;
    lx_handle_release(&file);
    return ERROR_INVALID_FUNCTION;
}

APIRET16 Dos16DevIOCtl2(PVOID data, USHORT data_size, PVOID parameters, USHORT parameter_size,
                        USHORT function, USHORT category, USHORT device)
{
    ULONG ds = data_size, ps = parameter_size;
    return DosDevIOCtl(device == UINT16_MAX ? UINT32_MAX : device, category, function, parameters,
                       ps, &ps, data, ds, &ds);
}
