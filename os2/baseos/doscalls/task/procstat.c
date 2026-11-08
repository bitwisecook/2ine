/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET16 Dos16QProcStatus(PVOID buffer, USHORT size)
{
    TRACE_NATIVE("Dos16QProcStatus(%p, %u)", buffer, (uint)size);
    /* No complete cross-process resource snapshot is available. Leave the
       caller's buffer untouched instead of returning an empty process list. */
    return ERROR_NOT_SUPPORTED;
}
