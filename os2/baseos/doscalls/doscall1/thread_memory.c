/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosAllocThreadLocalMemory(ULONG cb, PULONG *p)
{
    TRACE_NATIVE("DosAllocThreadLocalMemory(%u, %p)", (uint)cb, p);

    if (!cb || cb > 8) // OS/2 allocates one to eight contiguous words.
        return ERROR_INVALID_PARAMETER;
    else if (!p)
        return ERROR_INVALID_PARAMETER;

    APIRET retval = NO_ERROR;
    uint32 mask = 0xFF >> (8 - cb);

    grabLock(&GMutexDosCalls);

    if (GProcessState.tlspage == NULL)
    {
        if ((GProcessState.tlspage =
                 (uint32 *)(uintptr_t)lx_execution_tls_window(GProcessState.execution)) == NULL)
        {
            ungrabLock(&GMutexDosCalls);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    int i;
    for (i = 0; i <= 32 - (int)cb; i++, mask <<= 1)
    {
        if (((GProcessState.tlsmask ^ mask) & mask) == mask)
            break;
    }

    if (i > 32 - (int)cb)
        retval =
            ERROR_NOT_ENOUGH_MEMORY; // there are only 32 slots, couldn't find anything contiguous.
    else
    {
        assert(GProcessState.tlsallocs[i] == 0);
        GProcessState.tlsallocs[i] = (uint8)cb;
        GProcessState.tlsmask |= mask;
        *p = GProcessState.tlspage + i;
    }

    ungrabLock(&GMutexDosCalls);

    return retval;
}

APIRET DosFreeThreadLocalMemory(ULONG *p)
{
    TRACE_NATIVE("DosFreeThreadLocalMemory(%p)", p);

    APIRET retval = ERROR_INVALID_PARAMETER;

    grabLock(&GMutexDosCalls);

    if (GProcessState.tlspage)
    {
        const uintptr_t distance = (uintptr_t)p - (uintptr_t)GProcessState.tlspage;
        const uint32 slot = distance < 128 && !(distance & 3) ? (uint32)distance / 4 : 32;
        if (slot < 32)
        {
            const uint8 slots = GProcessState.tlsallocs[slot];
            if (slots > 0)
            {
                const uint32 mask = ((uint32)(0xFF >> (8 - slots))) << slot;
                assert((GProcessState.tlsmask & mask) == mask);
                GProcessState.tlsmask &= ~mask;
                GProcessState.tlsallocs[slot] = 0;
                retval = NO_ERROR;
            }
        }
    }

    ungrabLock(&GMutexDosCalls);

    return retval;
}
