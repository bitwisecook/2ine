/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

static USHORT write_verify;

APIRET16 Dos16QVerify(PUSHORT enabled)
{
    if (!enabled)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    *enabled = write_verify;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET16 Dos16SetVerify(USHORT enabled)
{
    if (enabled > 1)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    write_verify = enabled;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}
