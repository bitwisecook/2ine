/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Dos16MakePipe: anonymous pipes. */
#include "os2/baseos/doscalls/internal.h"

APIRET16 Dos16MakePipe(PUSHORT reader, PUSHORT writer, USHORT size)
{
    (void)size;
    if (!reader || !writer || reader == writer)
        return ERROR_INVALID_PARAMETER;
    HFILE slots[2] = {UINT32_MAX, UINT32_MAX};
    grabLock(&GMutexDosCalls);
    unsigned count = 0;
    for (HFILE i = 0; i < MaxHFiles && i < UINT16_MAX && count < 2; i++)
        if (!HFiles[i].handle)
            slots[count++] = i;
    if (count != 2)
    {
        ungrabLock(&GMutexDosCalls);
        return ERROR_TOO_MANY_OPEN_FILES;
    }
    HFiles[slots[0]].handle = HFiles[slots[1]].handle = 1;
    ungrabLock(&GMutexDosCalls);
    LxHostFile *input = NULL, *output = NULL;
    APIRET result = fileError(lx_host_file_pipe(&input, &output));
    HFileInfo infos[2] = {{0}};
    if (!result)
    {
        if (!initFileInfo(input, &infos[0], OPEN_ACCESS_READONLY))
        {
            lx_host_file_close(output);
            result = ERROR_NOT_ENOUGH_MEMORY;
        }
        else if (!initFileInfo(output, &infos[1], OPEN_ACCESS_WRITEONLY))
        {
            lx_handle_close(infos[0].handle, LX_HANDLE_FILE);
            result = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    grabLock(&GMutexDosCalls);
    if (result)
        HFiles[slots[0]].handle = HFiles[slots[1]].handle = 0;
    else
    {
        HFiles[slots[0]] = infos[0];
        HFiles[slots[1]] = infos[1];
        *reader = (USHORT)slots[0];
        *writer = (USHORT)slots[1];
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}
