/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* DosShutdown: flushes every open file. */
#include "os2/baseos/doscalls/internal.h"

APIRET DosShutdown(ULONG reserved)
{
    if (reserved)
        return ERROR_INVALID_PARAMETER;
    APIRET result = NO_ERROR;
    for (HFILE id = 0; id < MaxHFiles; id++)
    {
        LxHandleBorrow file;
        if (borrowFile(id, &file))
        {
            LxHostFileInfo info;
            if (!lx_host_file_info(file.resource, &info) && info.kind == LX_HOST_FILE_REGULAR)
            {
                APIRET error = fileError(lx_host_file_flush(file.resource));
                if (error && !result)
                    result = error;
            }
            lx_handle_release(&file);
        }
    }
    return result;
}
