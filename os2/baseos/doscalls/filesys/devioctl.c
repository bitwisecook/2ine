/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET16 Dos16DevIOCtl(PVOID pdata, PVOID pparms, USHORT fun, USHORT category, USHORT hdev)
{
    TRACE_NATIVE("Dos16DevIOCtl(%p, %p, %u, %u, %u)", pdata, pparms, (uint)fun, (uint)category,
                 (uint)hdev);
    return ERROR_INVALID_FUNCTION;
}

APIRET DosDevIOCtl(HFILE device, ULONG category, ULONG function, PVOID parameters,
                   ULONG parameter_max, PULONG parameter_size, PVOID data, ULONG data_max,
                   PULONG data_size)
{
    TRACE_NATIVE("DosDevIOCtl(%u, %u, %u, %p, %u, %p, %p, %u, %p)", (uint)device, (uint)category,
                 (uint)function, parameters, (uint)parameter_max, parameter_size, data,
                 (uint)data_max, data_size);
    if (device == (HFILE)-1 && category == 8 && function == 0x63)
    {
        if (!parameters || parameter_max < 2 || !data || !data_size)
            return ERROR_INVALID_PARAMETER;
        const uint8 *input = parameters;
        TRACE_NATIVE("DSK_GETDEVICEPARAMS command=%u drive=%u", input[0], input[1]);
        if (input[0] > 1 || input[1] >= 26 || !GProcessState.disks[input[1]])
            return ERROR_INVALID_DRIVE;
        if (data_max < 36)
        {
            *data_size = 36;
            return ERROR_BUFFER_OVERFLOW;
        }
        uint64_t total, available;
        LxHostFsError error = lx_host_path_space(GProcessState.disks[input[1]], &total, &available);
        if (error)
            return fileError(error);
        LxHostVolumeInfo volume;
        error = lx_host_path_volume(GProcessState.disks[input[1]], &volume);
        if (error)
            return fileError(error);
        int floppy = volume.removable && !strcmp(volume.filesystem, "FAT");
        unsigned sector_bytes = volume.sector_bytes;
        unsigned track = floppy ? volume.sectors_per_track : 63;
        unsigned heads = floppy ? volume.heads : 255;
        uint64_t sectors = total / sector_bytes;
        uint64_t cylinders = track && heads ? sectors / ((uint64_t)track * heads) : 0;
        uint8 *output = data;
        memset(output, 0, 36);
        lx_guest_store_u16(output, sector_bytes);
        output[2] = volume.cluster_bytes / sector_bytes;
        lx_guest_store_u16(output + 3, volume.reserved_sectors);
        output[5] = volume.fat_count;
        lx_guest_store_u16(output + 6, volume.root_entries);
        lx_guest_store_u16(output + 8, sectors <= 65535 ? sectors : 0);
        output[10] = floppy ? volume.media : 0xf8;
        lx_guest_store_u16(output + 11, volume.fat_sectors);
        lx_guest_store_u16(output + 13, track);
        lx_guest_store_u16(output + 15, heads);
        lx_guest_store_u32(output + 17, volume.hidden_sectors);
        lx_guest_store_u32(output + 21, sectors > UINT32_MAX ? UINT32_MAX : sectors);
        lx_guest_store_u16(output + 31, cylinders > 65535 ? 65535 : cylinders);
        /* Toolkit bsedev.h: 3.5-inch = 2, fixed = 5, unknown = 7. */
        output[33] = floppy ? 2 : volume.removable ? 7 : 5;
        lx_guest_store_u16(output + 34, volume.removable ? 0 : 1);
        *data_size = 36;
        if (parameter_size)
            *parameter_size = 2;
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
