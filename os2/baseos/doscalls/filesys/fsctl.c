/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal, ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb,
                        PULONG pcbBuffLength)
{
    TRACE_NATIVE("DosQueryFSAttach('%s', %u, %u, %p, %p)", pszDeviceName, (unsigned int)ulOrdinal,
                 (unsigned int)ulFSAInfoLevel, pfsqb, pcbBuffLength);
    if (!pcbBuffLength)
        return ERROR_INVALID_PARAMETER;
    unsigned drive = 26;
    if (ulFSAInfoLevel == 1)
    {
        if (!pszDeviceName || strlen(pszDeviceName) != 2 || pszDeviceName[1] != ':')
            return ERROR_INVALID_DRIVE;
        unsigned char letter = pszDeviceName[0];
        if (letter >= 'a' && letter <= 'z')
            letter -= 'a' - 'A';
        if (letter >= 'A' && letter <= 'Z')
            drive = letter - 'A';
    }
    else if (ulFSAInfoLevel == 3)
    {
        unsigned ordinal = 0;
        for (unsigned i = 0; i < 26; i++)
            if (GProcessState.disks[i] && ++ordinal == ulOrdinal)
            {
                drive = i;
                break;
            }
    }
    else
        return ERROR_INVALID_LEVEL;
    if (drive >= 26 || !GProcessState.disks[drive])
        return ERROR_INVALID_DRIVE;
    LxHostVolumeInfo volume;
    LxHostFsError volume_error = lx_host_path_volume(GProcessState.disks[drive], &volume);
    if (volume_error)
        return fileError(volume_error);
    const char *filesystem = volume.filesystem;
    const ULONG filesystem_size = (ULONG)strlen(filesystem) + 1;
    const ULONG size = 8 + 3 + filesystem_size;
    ULONG capacity = *pcbBuffLength;
    *pcbBuffLength = size;
    if (!pfsqb || capacity < size)
        return ERROR_BUFFER_OVERFLOW;
    uint8 *output = (uint8 *)pfsqb;
    lx_guest_store_u16(output, 3); /* FSAT_LOCALDRV */
    lx_guest_store_u16(output + 2, 2);
    lx_guest_store_u16(output + 4, filesystem_size - 1);
    lx_guest_store_u16(output + 6, 0);
    output[8] = 'A' + drive;
    output[9] = ':';
    output[10] = 0;
    memcpy(output + 11, filesystem, filesystem_size);
    return NO_ERROR;
}
