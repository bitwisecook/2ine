/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

/* Family API suballocation reserves eight bytes and rounds blocks to four bytes. */
typedef struct SubHeap16
{
    USHORT selector;
    ULONG size;
    unsigned char used[65536 / 4];
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
    if (flags > 1)
        return ERROR_DOSSUB_BADFLAG;
    const ULONG extent = size ? size & ~3u : 65536;
    if (extent <= 8)
        return ERROR_DOSSUB_BADSIZE;
    void *base = GProcessState.convert1616to32((ULONG)selector << 16);
    uint32 address;
    if (!base || (GProcessState.execution &&
                  !lx_execution_address(GProcessState.execution, base, extent, 3, &address)))
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
    const ULONG count = ((ULONG)size + 3) / 4;
    if (!count || count > (65536 - 8) / 4)
        return ERROR_DOSSUB_BADSIZE;
    grabLock(&GMutexDosCalls);
    SubHeap16 *heap = subheap16(selector);
    APIRET16 result = heap ? ERROR_DOSSUB_NOMEM : ERROR_DOSSUB_BADSELECTOR;
    if (heap)
    {
        ULONG available = 0;
        for (ULONG i = 2; i < heap->size / 4; i++)
        {
            available = heap->used[i] ? 0 : available + 1;
            if (available == count)
            {
                const ULONG start = i + 1 - count;
                memset(heap->used + start, 1, count);
                *offset = start * 4;
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
    const ULONG extent = ((ULONG)size + 3) & ~3u;
    if (!size || offset < 8 || (offset & 3) || (ULONG)offset + extent > 65536)
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
        for (ULONG i = offset / 4; i < (offset + extent) / 4; i++)
            if (!heap->used[i])
                result = ERROR_DOSSUB_OVERLAP;
        if (!result)
            memset(heap->used + offset / 4, 0, extent / 4);
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}
