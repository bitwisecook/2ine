/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosTmrQueryFreq(PULONG frequency)
{
    if (!frequency)
        return ERROR_INVALID_PARAMETER;
    *frequency = 1000000;
    return NO_ERROR;
}

APIRET DosTmrQueryTime(PVOID ticks)
{
    if (!ticks)
        return ERROR_INVALID_PARAMETER;
    uint64 value = lx_host_monotonic_us();
    lx_guest_store_u32(ticks, (ULONG)value);
    lx_guest_store_u32((uint8 *)ticks + sizeof(ULONG), (ULONG)(value >> 32));
    return NO_ERROR;
}
