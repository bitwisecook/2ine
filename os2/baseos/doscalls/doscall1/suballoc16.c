/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Family API suballocation (Dos16Sub*) within one 16-bit segment. */
#include "os2/baseos/doscalls/internal.h"

/* Family API suballocation reserves the segment's first eight bytes and rounds blocks
   to four bytes; each four-byte unit has one used flag. */
enum
{
    SUBHEAP16_UNIT = 4,
    SUBHEAP16_RESERVED = 8,
    SUBHEAP16_UNITS = SEGMENT_SIZE_MAXIMUM / SUBHEAP16_UNIT,
    /* Dos16SubSet flag 1 initialises the heap; zero resizes it. */
    SUBSET16_INITIALIZE = 1
};

typedef struct SubHeap16
{
    USHORT selector;
    ULONG size;
    unsigned char used[SUBHEAP16_UNITS];
    struct SubHeap16 *next;
} SubHeap16;

static SubHeap16 *subheaps16;

static SubHeap16 *subheap16(USHORT selector)
{
    for (SubHeap16 *heap = subheaps16; heap; heap = heap->next)
        if (heap->selector == selector)
            return heap;
    return NULL;
}

APIRET16 Dos16SubSet(USHORT selector, USHORT flags, USHORT size)
{
    TRACE_NATIVE("Dos16SubSet(%u, %u, %u)", selector, flags, size);
    if (flags > SUBSET16_INITIALIZE)
        return ERROR_DOSSUB_BADFLAG;
    const ULONG extent = size ? size & ~(SUBHEAP16_UNIT - 1u) : SEGMENT_SIZE_MAXIMUM;
    if (extent <= SUBHEAP16_RESERVED)
        return ERROR_DOSSUB_BADSIZE;
    void *base = GProcessState.convert1616to32((ULONG)selector << SELECTOR_OFFSET_BITS);
    uint32 address;
    if (!base || (GProcessState.execution &&
                  !lx_execution_address(GProcessState.execution, base, extent,
                                        LX_GUEST_READ | LX_GUEST_WRITE, &address)))
        return ERROR_DOSSUB_BADSELECTOR;
    grabLock(&GMutexDosCalls);
    SubHeap16 *heap = subheap16(selector);
    APIRET16 result = NO_ERROR;
    if (!heap)
    {
        if (!flags)
            result = ERROR_DOSSUB_BADSELECTOR;
        else if (!(heap = calloc(1, sizeof *heap)))
            result = ERROR_NOT_ENOUGH_MEMORY;
        else
        {
            heap->selector = selector;
            heap->next = subheaps16;
            subheaps16 = heap;
        }
    }
    if (!result)
    {
        if (flags)
        {
            memset(heap->used, 0, sizeof heap->used);
            heap->size = extent;
        }
        else if (extent < heap->size)
            result = ERROR_DOSSUB_SHRINK;
        else
            heap->size = extent;
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET16 Dos16SubAlloc(USHORT selector, PUSHORT offset, USHORT size)
{
    TRACE_NATIVE("Dos16SubAlloc(%u, %p, %u)", selector, offset, size);
    if (!offset)
        return ERROR_INVALID_PARAMETER;
    const ULONG count = ((ULONG)size + SUBHEAP16_UNIT - 1) / SUBHEAP16_UNIT;
    if (!count || count > (SEGMENT_SIZE_MAXIMUM - SUBHEAP16_RESERVED) / SUBHEAP16_UNIT)
        return ERROR_DOSSUB_BADSIZE;
    grabLock(&GMutexDosCalls);
    SubHeap16 *heap = subheap16(selector);
    APIRET16 result = heap ? ERROR_DOSSUB_NOMEM : ERROR_DOSSUB_BADSELECTOR;
    if (heap)
    {
        ULONG available = 0;
        for (ULONG i = SUBHEAP16_RESERVED / SUBHEAP16_UNIT; i < heap->size / SUBHEAP16_UNIT; i++)
        {
            available = heap->used[i] ? 0 : available + 1;
            if (available == count)
            {
                const ULONG start = i + 1 - count;
                memset(heap->used + start, 1, count);
                *offset = start * SUBHEAP16_UNIT;
                result = NO_ERROR;
                break;
            }
        }
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET16 Dos16SubFree(USHORT selector, USHORT offset, USHORT size)
{
    TRACE_NATIVE("Dos16SubFree(%u, %u, %u)", selector, offset, size);
    const ULONG extent = ((ULONG)size + SUBHEAP16_UNIT - 1) & ~(SUBHEAP16_UNIT - 1u);
    if (!size || offset < SUBHEAP16_RESERVED || (offset & (SUBHEAP16_UNIT - 1)) ||
        (ULONG)offset + extent > SEGMENT_SIZE_MAXIMUM)
        return ERROR_DOSSUB_BADSIZE;
    grabLock(&GMutexDosCalls);
    SubHeap16 *heap = subheap16(selector);
    APIRET16 result = NO_ERROR;
    if (!heap)
        result = ERROR_DOSSUB_BADSELECTOR;
    else if ((ULONG)offset + extent > heap->size)
        result = ERROR_DOSSUB_BADSIZE;
    else
    {
        for (ULONG i = offset / SUBHEAP16_UNIT; i < (offset + extent) / SUBHEAP16_UNIT; i++)
            if (!heap->used[i])
                result = ERROR_DOSSUB_OVERLAP;
        if (!result)
            memset(heap->used + offset / SUBHEAP16_UNIT, 0, extent / SUBHEAP16_UNIT);
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}
