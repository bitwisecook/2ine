/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Volume information: allocation, serial number and volume label. */
#include "os2/baseos/doscalls/internal.h"
#include <stddef.h>

/* FSINFO's FDATE/FTIME pair carries the 32-bit volume serial number. */
enum
{
    FSINFO_SERIAL_OFFSET = offsetof(FSINFO, fdateCreation),
    FSINFO_LABEL_LENGTH_OFFSET = offsetof(FSINFO, vol.cch),
    FSINFO_LABEL_OFFSET = offsetof(FSINFO, vol.szVolLabel)
};

static void applyVolumeLabel(ULONG drive, LxHostVolumeInfo *volume);

APIRET16 Dos16QFsInfo(USHORT drivenum, USHORT fslevel, PBYTE infobuf, USHORT infobufsize)
{
    TRACE_NATIVE("Dos16QFsInfo(%d, %d, %p, %d)", drivenum, fslevel, infobuf, infobufsize);
    if (!drivenum)
        drivenum = (USHORT)GProcessState.current_disk;
    if (!drivenum || drivenum > DRIVE_COUNT || !GProcessState.disks[drivenum - 1])
        return ERROR_INVALID_DRIVE;
    LxHostVolumeInfo volume;
    LxHostFsError error = lx_host_path_volume(GProcessState.disks[drivenum - 1], &volume);
    if (error)
        return fileError(error);
    applyVolumeLabel(drivenum - 1, &volume);
    const char *label = volume.label;
    if (fslevel != FSIL_VOLSER)
        return ERROR_INVALID_LEVEL;
    if (infobufsize < FSINFO_LABEL_OFFSET + strlen(label) + 1)
        return ERROR_BUFFER_OVERFLOW;
    lx_guest_store_u32(infobuf + FSINFO_SERIAL_OFFSET, volume.serial);
    infobuf[FSINFO_LABEL_LENGTH_OFFSET] = (uint8)strlen(label);
    memcpy(infobuf + FSINFO_LABEL_OFFSET, label, strlen(label) + 1);
    return NO_ERROR;
}

/* DosSetFSInfo cannot rewrite a host directory's identity or a disk image's root
   entry, so a set label is a ball-wide override read back by every query. A process
   outside a ball keeps its labels for its own lifetime. */
enum
{
    VOLUME_LABEL_MAXIMUM = 11,
    /* A set flag, the label and its terminator. */
    VOLUME_RECORD_BYTES = VOLUME_LABEL_MAXIMUM + 2,
    VOLUME_STORE_BYTES = VOLUME_RECORD_BYTES * DRIVE_COUNT
};

static uint8 process_volume_labels[VOLUME_STORE_BYTES];

static char *volumeLabelStorePath(void)
{
    const char *ball = getenv("TWOINE_BALL");
    if (!ball || !*ball)
        return NULL;
    const char suffix[] = "/volume-labels";
    char *path = malloc(strlen(ball) + sizeof suffix);
    if (path)
    {
        strcpy(path, ball);
        strcat(path, suffix);
    }
    return path;
}

static void applyVolumeLabel(ULONG drive, LxHostVolumeInfo *volume)
{
    uint8 records[VOLUME_STORE_BYTES];
    char *path = volumeLabelStorePath();
    if (path)
    {
        size_t size = 0;
        LxHostFsError error;
        uint8 *stored = lx_host_file_snapshot(path, VOLUME_STORE_BYTES, &size, &error);
        free(path);
        if (!stored)
            return;
        if (size != VOLUME_STORE_BYTES)
        {
            free(stored);
            return;
        }
        memcpy(records, stored, VOLUME_STORE_BYTES);
        free(stored);
    }
    else
    {
        grabLock(&GMutexDosCalls);
        memcpy(records, process_volume_labels, VOLUME_STORE_BYTES);
        ungrabLock(&GMutexDosCalls);
    }
    const uint8 *record = records + drive * VOLUME_RECORD_BYTES;
    if (record[0])
    {
        memcpy(volume->label, record + 1, VOLUME_LABEL_MAXIMUM);
        volume->label[VOLUME_LABEL_MAXIMUM] = 0;
    }
}

static APIRET storeVolumeLabel(ULONG drive, const char *label)
{
    uint8 record[VOLUME_RECORD_BYTES] = {1};
    strncpy((char *)record + 1, label, VOLUME_LABEL_MAXIMUM);
    char *path = volumeLabelStorePath();
    if (!path)
    {
        grabLock(&GMutexDosCalls);
        memcpy(process_volume_labels + drive * VOLUME_RECORD_BYTES, record, sizeof record);
        ungrabLock(&GMutexDosCalls);
        return NO_ERROR;
    }
    LxHostFsError error;
    LxHostFile *file = lx_host_file_open(
        path, LX_HOST_FILE_READ | LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE, 0, &error);
    free(path);
    if (!file)
        return fileError(error);
    /* Read and rewrite under the file lock, so concurrent setters keep each other's records. */
    uint8 records[VOLUME_STORE_BYTES];
    size_t actual = 0;
    error = lx_host_file_lock(file, 1);
    if (!error)
    {
        error = lx_host_file_read(file, records, sizeof records, &actual);
        if (!error && actual != sizeof records)
            memset(records, 0, sizeof records);
        memcpy(records + drive * VOLUME_RECORD_BYTES, record, sizeof record);
        if (!error)
            error = lx_host_file_seek(file, 0, FILE_BEGIN, NULL);
        if (!error)
            error = lx_host_file_write(file, records, sizeof records, &actual);
        if (!error && actual != sizeof records)
            error = LX_HOST_FS_IO;
        lx_host_file_lock(file, 0);
    }
    lx_host_file_close(file);
    return error ? fileError(error) : NO_ERROR;
}

/* The thunked 16-bit service takes USHORT drive, level and size. */
APIRET DosSetFSInfo(ULONG drive, ULONG level, PVOID data, ULONG size)
{
    TRACE_NATIVE("DosSetFSInfo(%u, %u, %p, %u)", (uint)drive, (uint)level, data, (uint)size);
    ULONG disk = (USHORT)drive;
    if (!disk)
        disk = GProcessState.current_disk;
    if (disk > DRIVE_COUNT || !disk || !GProcessState.disks[disk - 1])
        return ERROR_INVALID_DRIVE;
    if ((USHORT)level != FSIL_VOLSER)
        return ERROR_INVALID_LEVEL;
    const ULONG capacity = (USHORT)size;
    if (!capacity)
        return ERROR_INSUFFICIENT_BUFFER;
    const uint8 *input = data;
    if (!input)
        return ERROR_INVALID_PARAMETER;
    const unsigned length = input[0];
    if (length > VOLUME_LABEL_MAXIMUM)
        return ERROR_LABEL_TOO_LONG;
    if (length >= capacity)
        return ERROR_INSUFFICIENT_BUFFER;
    char label[VOLUME_LABEL_MAXIMUM + 1] = {0};
    for (unsigned i = 0; i < length; i++)
    {
        char c = (char)input[1 + i];
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (c == '*' || c == '?' || c == '.')
            return ERROR_INVALID_NAME;
        label[i] = c;
    }
    LxHostVolumeInfo volume;
    LxHostFsError error = lx_host_path_volume(GProcessState.disks[disk - 1], &volume);
    if (error)
        return fileError(error);
    if (volume.readonly)
        return ERROR_WRITE_PROTECT;
    return storeVolumeLabel(disk - 1, label);
}

APIRET DosQueryFSInfo(ULONG drive, ULONG level, PVOID data, ULONG capacity)
{
    TRACE_NATIVE("DosQueryFSInfo(%u, %u, %p, %u)", (uint)drive, (uint)level, data, (uint)capacity);
    if (!drive)
        drive = GProcessState.current_disk;
    if (drive > DRIVE_COUNT || !drive || !GProcessState.disks[drive - 1])
        return ERROR_INVALID_DRIVE;
    uint8 *output = data;
    if (!output)
        return ERROR_INVALID_PARAMETER;
    if (level == FSIL_ALLOC)
    {
        if (capacity < sizeof(FSALLOCATE))
            return ERROR_BUFFER_OVERFLOW;
        uint64_t total, available;
        LxHostFsError error =
            lx_host_path_space(GProcessState.disks[drive - 1], &total, &available);
        if (error)
            return fileError(error);
        LxHostVolumeInfo volume;
        error = lx_host_path_volume(GProcessState.disks[drive - 1], &volume);
        if (error)
            return fileError(error);
        uint64_t units = total / volume.cluster_bytes,
                 free_units = available / volume.cluster_bytes;
        lx_guest_store_u32(output + offsetof(FSALLOCATE, idFileSystem), 0);
        lx_guest_store_u32(output + offsetof(FSALLOCATE, cSectorUnit),
                           volume.cluster_bytes / volume.sector_bytes);
        lx_guest_store_u32(output + offsetof(FSALLOCATE, cUnit),
                           units > UINT32_MAX ? UINT32_MAX : units);
        lx_guest_store_u32(output + offsetof(FSALLOCATE, cUnitAvail),
                           free_units > UINT32_MAX ? UINT32_MAX : free_units);
        lx_guest_store_u16(output + offsetof(FSALLOCATE, cbSector), volume.sector_bytes);
        return NO_ERROR;
    }
    if (level == FSIL_VOLSER)
    {
        if (capacity < sizeof(FSINFO))
            return ERROR_BUFFER_OVERFLOW;
        memset(output, 0, sizeof(FSINFO));
        LxHostVolumeInfo volume;
        LxHostFsError error = lx_host_path_volume(GProcessState.disks[drive - 1], &volume);
        if (error)
            return fileError(error);
        applyVolumeLabel(drive - 1, &volume);
        lx_guest_store_u32(output + FSINFO_SERIAL_OFFSET, volume.serial);
        output[FSINFO_LABEL_LENGTH_OFFSET] = (uint8)strlen(volume.label);
        memcpy(output + FSINFO_LABEL_OFFSET, volume.label, strlen(volume.label) + 1);
        return NO_ERROR;
    }
    return ERROR_INVALID_LEVEL;
}
