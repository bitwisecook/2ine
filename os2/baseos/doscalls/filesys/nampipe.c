/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosWaitNPipe(PSZ pszName, ULONG ulTimeOut)
{
    TRACE_NATIVE("DosWaitNPipe('%s', %u)", pszName ? pszName : "", (uint)ulTimeOut);

    if ((!pszName) || (!*pszName))
        return ERROR_INVALID_PARAMETER;

    /* No named-pipe provider exists, so waiting cannot make an instance available. */
    return ERROR_PIPE_NOT_CONNECTED;
}
