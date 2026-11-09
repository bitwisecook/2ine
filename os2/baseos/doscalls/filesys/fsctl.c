/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* DosQueryFSAttach: the file system attached to each drive. */
#include "os2/baseos/doscalls/internal.h"

/* FSQBUFFER2 as returned: four USHORT lengths, then the "X:" name and the FSD name,
   each NUL-terminated; 2ine attaches no FSA data. */
enum
{
    FSQBUFFER2_TYPE_OFFSET = 0,
    FSQBUFFER2_NAME_LENGTH_OFFSET = 2,
    FSQBUFFER2_FSD_LENGTH_OFFSET = 4,
    FSQBUFFER2_DATA_LENGTH_OFFSET = 6,
    FSQBUFFER2_NAME_OFFSET = 8,
    DRIVE_NAME_LENGTH = 2, /* "X:" */
    FSQBUFFER2_FSD_OFFSET = FSQBUFFER2_NAME_OFFSET + DRIVE_NAME_LENGTH + 1
};

APIRET DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal, ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb,
                        PULONG pcbBuffLength)
{
    TRACE_NATIVE("DosQueryFSAttach('%s', %u, %u, %p, %p)", pszDeviceName, (unsigned int)ulOrdinal,
                 (unsigned int)ulFSAInfoLevel, pfsqb, pcbBuffLength);
    if (!pcbBuffLength)
        return ERROR_INVALID_PARAMETER;
    unsigned drive = DRIVE_COUNT;
    if (ulFSAInfoLevel == FSAIL_QUERYNAME)
    {
        if (!pszDeviceName || strlen(pszDeviceName) != DRIVE_NAME_LENGTH || pszDeviceName[1] != ':')
            return ERROR_INVALID_DRIVE;
        unsigned char letter = pszDeviceName[0];
        if (letter >= 'a' && letter <= 'z')
            letter -= 'a' - 'A';
        if (letter >= 'A' && letter <= 'Z')
            drive = letter - 'A';
    }
    else if (ulFSAInfoLevel == FSAIL_DRVNUMBER)
    {
        unsigned ordinal = 0;
        for (unsigned i = 0; i < DRIVE_COUNT; i++)
            if (GProcessState.disks[i] && ++ordinal == ulOrdinal)
            {
                drive = i;
                break;
            }
    }
    else
        return ERROR_INVALID_LEVEL;
    if (drive >= DRIVE_COUNT || !GProcessState.disks[drive])
        return ERROR_INVALID_DRIVE;
    LxHostVolumeInfo volume;
    LxHostFsError volume_error = lx_host_path_volume(GProcessState.disks[drive], &volume);
    if (volume_error)
        return fileError(volume_error);
    const char *filesystem = volume.filesystem;
    const ULONG filesystem_size = (ULONG)strlen(filesystem) + 1;
    const ULONG size = FSQBUFFER2_FSD_OFFSET + filesystem_size;
    ULONG capacity = *pcbBuffLength;
    *pcbBuffLength = size;
    if (!pfsqb || capacity < size)
        return ERROR_BUFFER_OVERFLOW;
    uint8 *output = (uint8 *)pfsqb;
    lx_guest_store_u16(output + FSQBUFFER2_TYPE_OFFSET, FSAT_LOCALDRV);
    lx_guest_store_u16(output + FSQBUFFER2_NAME_LENGTH_OFFSET, DRIVE_NAME_LENGTH);
    lx_guest_store_u16(output + FSQBUFFER2_FSD_LENGTH_OFFSET, filesystem_size - 1);
    lx_guest_store_u16(output + FSQBUFFER2_DATA_LENGTH_OFFSET, 0);
    output[FSQBUFFER2_NAME_OFFSET] = 'A' + drive;
    output[FSQBUFFER2_NAME_OFFSET + 1] = ':';
    output[FSQBUFFER2_NAME_OFFSET + DRIVE_NAME_LENGTH] = 0;
    memcpy(output + FSQBUFFER2_FSD_OFFSET, filesystem, filesystem_size);
    return NO_ERROR;
}

APIRET16 Dos16QFSAttach(PSZ device, USHORT ordinal, USHORT level, PVOID buffer, PUSHORT size,
                        ULONG reserved)
{
    if (reserved || !size)
        return ERROR_INVALID_PARAMETER;
    ULONG length = *size;
    APIRET result = DosQueryFSAttach(device, ordinal, level, buffer, &length);
    *size = length > UINT16_MAX ? UINT16_MAX : (USHORT)length;
    return result;
}
