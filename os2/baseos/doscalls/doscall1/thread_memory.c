/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Thread-local memory: slots in the per-thread area the TIB points at. */
#include "os2/baseos/doscalls/internal.h"

/* The thread-local area holds 32 ULONG slots; one allocation takes one to eight
   contiguous slots. */
enum
{
    TLS_SLOT_COUNT = 32,
    TLS_ALLOCATION_MAXIMUM = 8,
    TLS_SLOT_SIZE = sizeof(ULONG),
    TLS_ALLOCATION_BITS = 0xFF
};

APIRET DosAllocThreadLocalMemory(ULONG cb, PULONG *p)
{
    TRACE_NATIVE("DosAllocThreadLocalMemory(%u, %p)", (uint)cb, p);

    if (!cb || cb > TLS_ALLOCATION_MAXIMUM)
        return ERROR_INVALID_PARAMETER;
    else if (!p)
        return ERROR_INVALID_PARAMETER;

    APIRET retval = NO_ERROR;
    uint32 mask = TLS_ALLOCATION_BITS >> (TLS_ALLOCATION_MAXIMUM - cb);

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
    for (i = 0; i <= TLS_SLOT_COUNT - (int)cb; i++, mask <<= 1)
    {
        if (((GProcessState.tlsmask ^ mask) & mask) == mask)
            break;
    }

    if (i > TLS_SLOT_COUNT - (int)cb)
        retval = ERROR_NOT_ENOUGH_MEMORY;
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
        const uint32 slot =
            distance < TLS_SLOT_COUNT * TLS_SLOT_SIZE && !(distance & (TLS_SLOT_SIZE - 1))
                ? (uint32)distance / TLS_SLOT_SIZE
                : TLS_SLOT_COUNT;
        if (slot < TLS_SLOT_COUNT)
        {
            const uint8 slots = GProcessState.tlsallocs[slot];
            if (slots > 0)
            {
                const uint32 mask =
                    ((uint32)(TLS_ALLOCATION_BITS >> (TLS_ALLOCATION_MAXIMUM - slots))) << slot;
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
