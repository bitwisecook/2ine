/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* 16-bit segments: allocation, sizing and flat/16:16 address conversion. */
#include "os2/baseos/doscalls/internal.h"

ULONG _DosFlatToSel(PVOID ptr)
{
    TRACE_NATIVE("DosFlatToSel(%p)", ptr);
    return GProcessState.convert32to1616(ptr);
}

ULONG _DosSelToFlat(void *ptr)
{
    TRACE_NATIVE("DosSelToFlat(%p)", ptr);
    return (ULONG)GProcessState.convert1616to32((uint32)ptr);
}

APIRET16 Dos16CreateCSAlias(USHORT selDS, PUSHORT pselCS)
{
    TRACE_NATIVE("Dos16CreateCSAlias(%u, %p)", (uint)selDS, pselCS);
    uint16 alias = selDS;
    if (GProcessState.createCodeAlias)
        GProcessState.createCodeAlias(selDS, &alias);
    if (pselCS)
        *pselCS = alias;
    return NO_ERROR;
}

/* Dos16AllocSeg flag bits above SEG_DISCARDABLE and its sizeable bit are reserved. */
enum
{
    ALLOCSEG_RESERVED_BITS = 0xFFF0,
    NO_SELECTOR = 0xFFFF
};

static ULONG segment_sizes[LX_MAX_LDT_SLOTS];

static ULONG segment_capacities[LX_MAX_LDT_SLOTS];

static unsigned char owned_segments[LX_MAX_LDT_SLOTS];

APIRET16 Dos16SizeSeg(USHORT selector, PULONG size)
{
    if (!size || !(selector & SELECTOR_TABLE_LDT))
        return ERROR_INVALID_PARAMETER;
    unsigned index = selector >> SELECTOR_INDEX_SHIFT;
    grabLock(&GMutexDosCalls);
    ULONG length = segment_sizes[index];
    if (!length)
        for (LxModule *module = GProcessState.loaded_modules; module && !length;
             module = module->next)
            for (ULONG i = 0; i < module->num_mmaps; i++)
                if (module->mmaps[i].alias == index)
                    length = module->mmaps[i].size > SEGMENT_SIZE_MAXIMUM ? SEGMENT_SIZE_MAXIMUM
                                                                          : module->mmaps[i].size;
    if (!length && GProcessState.main_module)
        for (ULONG i = 0; i < GProcessState.main_module->num_mmaps; i++)
            if (GProcessState.main_module->mmaps[i].alias == index)
            {
                size_t extent = GProcessState.main_module->mmaps[i].size;
                length = extent > SEGMENT_SIZE_MAXIMUM ? SEGMENT_SIZE_MAXIMUM : extent;
                break;
            }
    if (length && !segment_capacities[index])
        segment_capacities[index] = length;
    ungrabLock(&GMutexDosCalls);
    if (!length)
        return ERROR_INVALID_SEGMENT_NUMBER;
    *size = length;
    return NO_ERROR;
}

APIRET16 Dos16GetShrSeg(PSZ name, PUSHORT selector)
{
    if (!name || !selector)
        return ERROR_INVALID_PARAMETER;
    PVOID memory = NULL;
    APIRET result = DosGetNamedSharedMem(&memory, name, PAG_READ | PAG_WRITE);
    if (result)
        return result;
    ULONG pointer = GProcessState.convert32to1616(memory);
    if (!pointer || (pointer & UINT16_MAX))
        return ERROR_INVALID_ADDRESS;
    *selector = pointer >> SELECTOR_OFFSET_BITS;
    grabLock(&GMutexDosCalls);
    for (SharedMemBlock *block = GSharedMem; block; block = block->next)
        if (block->addr == memory)
        {
            const unsigned index = *selector >> SELECTOR_INDEX_SHIFT;
            segment_sizes[index] =
                block->size > SEGMENT_SIZE_MAXIMUM ? SEGMENT_SIZE_MAXIMUM : block->size;
            segment_capacities[index] = segment_sizes[index];
            break;
        }
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET16 Dos16AllocSeg(USHORT size, PUSHORT psel, USHORT flags)
{
    TRACE_NATIVE("Dos16AllocSeg(%u, %p, %u)", (uint)size, psel, (uint)flags);
    if (psel == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    else if ((flags & ALLOCSEG_RESERVED_BITS) != 0)
    {
        return ERROR_INVALID_PARAMETER;
    }

    uint16 sel = NO_SELECTOR;

    void *addr = GProcessState.allocSegment(&sel, 0);
    if (!addr)
    {
        assert(sel == NO_SELECTOR);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    assert(sel != NO_SELECTOR);

    if (flags & SEG_GIVEABLE)
    {
        FIXME("segment is meant to be shareable through Dos16GiveSeg");
    }

    if (flags & SEG_GETTABLE)
    {
        FIXME("segment is meant to be shareable through Dos16GetSeg");
    }

    /* OS/2 exposes the tiled segment value through this API. */
    grabLock(&GMutexDosCalls);
    segment_sizes[sel] = size ? size : SEGMENT_SIZE_MAXIMUM;
    segment_capacities[sel] = SEGMENT_SIZE_MAXIMUM;
    owned_segments[sel] = 1;
    ungrabLock(&GMutexDosCalls);
    *psel = ldtSelector(sel);
    return NO_ERROR;
}

APIRET16 Dos16ReallocSeg(USHORT size, USHORT sel)
{
    TRACE_NATIVE("Dos16ReallocSeg(%u, %u)", (uint)size, (uint)sel);

    ULONG previous;
    APIRET result = Dos16SizeSeg(sel, &previous);
    if (result)
        return result;
    grabLock(&GMutexDosCalls);
    ULONG requested = size ? size : SEGMENT_SIZE_MAXIMUM;
    if (requested > segment_capacities[sel >> SELECTOR_INDEX_SHIFT])
    {
        ungrabLock(&GMutexDosCalls);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    segment_sizes[sel >> SELECTOR_INDEX_SHIFT] = requested;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET16 Dos16GetSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16GetSeg(%u)", (uint)sel);
    return NO_ERROR;
}

APIRET16 Dos16LockSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16LockSeg(%u)", (uint)sel);
    return NO_ERROR;
}

APIRET16 Dos16UnlockSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16UnlockSeg(%u)", (uint)sel);
    return NO_ERROR;
}

APIRET16 Dos16HugeShift(PUSHORT pshift)
{
    TRACE_NATIVE("Dos16HugeShift(%p)", pshift);
    if (pshift)
        *pshift = 0;
    return NO_ERROR;
}

APIRET16 Dos16HugeIncr(USHORT sel, PULONG psize)
{
    TRACE_NATIVE("Dos16HugeIncr(%u, %p)", (uint)sel, psize);
    return NO_ERROR;
}

APIRET16 Dos16GetHugeShift(PUSHORT pcount)
{
    TRACE_NATIVE("Dos16GetHugeShift(%p)", pcount);
    *pcount = 0;
    FIXME("maybe?");
    return NO_ERROR;
}

APIRET16 Dos16FreeSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16FreeSeg(%u)", (uint)sel);
    if (!(sel & SELECTOR_TABLE_LDT))
        return ERROR_INVALID_SEGMENT_NUMBER;
    unsigned index = sel >> SELECTOR_INDEX_SHIFT;
    grabLock(&GMutexDosCalls);
    if (owned_segments[index])
    {
        owned_segments[index] = 0;
        segment_sizes[index] = 0;
        segment_capacities[index] = 0;
        ungrabLock(&GMutexDosCalls);
        GProcessState.freeSegment(index);
        return NO_ERROR;
    }
    /* Module segments and shared mappings retain backing owned by their loader. */
    ULONG size;
    ungrabLock(&GMutexDosCalls);
    APIRET result = Dos16SizeSeg(sel, &size);
    return result;
}
