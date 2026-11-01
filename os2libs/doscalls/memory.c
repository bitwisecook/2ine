/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
ULONG _DosFlatToSel(PVOID ptr)
{
    TRACE_NATIVE("DosFlatToSel(%p)", ptr);
    return GLoaderState.convert32to1616(ptr);
}

APIRET DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosAllocMem(%p, %u, %u)", ppb, (uint)cb, (uint)flag);

    if ((!ppb) || (cb == 0))
        return ERROR_INVALID_PARAMETER;

    const size_t pagesize = 4096;
    size_t size = (size_t)cb;
    if ((size % pagesize) != 0)
        size += pagesize - (size % pagesize);

    LxGuestMapping *mapping = NULL;
    void *addr;
    if (GLoaderState.execution)
    {
        mapping = lx_execution_map(GLoaderState.execution, 0, size, 3 | ((flag & 4) ? 4 : 0), 0);
        if (!mapping)
            return ERROR_NOT_ENOUGH_MEMORY;
        addr = lx_execution_mapping_host(mapping);
    }
    else
    {
        addr = lx_host_memory_alloc(size);
        if (!addr)
            return ERROR_NOT_ENOUGH_MEMORY;
    }

    SharedMemBlock *block = (SharedMemBlock *)calloc(1, sizeof(*block));
    if (!block)
    {
        if (mapping)
            lx_execution_unmap(GLoaderState.execution, mapping);
        else
            lx_host_memory_free(addr, size);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    block->addr = addr;
    block->size = size;
    block->mapping = mapping;
    block->freed = 0;

    grabLock(&GMutexDosCalls);
    block->next = GSharedMem;
    GSharedMem = block;
    ungrabLock(&GMutexDosCalls);

    *ppb = addr;
    TRACE_RET("DosAllocMem(%u) -> %p", (uint)cb, addr);
    return NO_ERROR;
}

APIRET DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb)
{
    TRACE_NATIVE("DosSubSetMem(%p, %u, %u)", pbBase, (uint)flag, (uint)cb);
    TRACE_RET("DosSubSetMem(%p, %u, %u) -> 0", pbBase, (uint)flag, (uint)cb);
    return NO_ERROR;
}

APIRET DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb)
{
    TRACE_NATIVE("DosSubAllocMem(%p, %p, %u)", pbBase, ppb, (uint)cb);
    *ppb = malloc(cb);
    if (!*ppb)
    {
        TRACE_RET("DosSubAllocMem(%p, %u) -> ENOMEM", pbBase, (uint)cb);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    TRACE_RET("DosSubAllocMem(%p, %u) -> %p", pbBase, (uint)cb, *ppb);
    return NO_ERROR;
}

APIRET DosSetMem(PVOID pb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosSetMem(%p, %u, %u)", pb, (uint)cb, (uint)flag);
    return NO_ERROR;
}

void makeSharedMemName(const char *os2name, char *buf, const size_t buflen)
{
    /* Case-fold OS/2 names into stable, collision-resistant host object names. */
    uint64_t hash = 1469598103934665603ULL; // FNV-1a 64-bit offset basis.
    const char *bottle = getenv("TWOINE_BOTTLE");
    if (bottle)
    {
        for (const char *p = bottle; *p; p++)
            hash = (hash ^ (unsigned char)*p) * 1099511628211ULL;
        hash = (hash ^ 0xff) * 1099511628211ULL;
    }
    for (const char *ptr = os2name; *ptr; ptr++)
    {
        unsigned char ch = (unsigned char)*ptr;
        if ((ch >= 'A') && (ch <= 'Z'))
            ch += 'a' - 'A';
        hash ^= ch;
        hash *= 1099511628211ULL; // FNV-1a 64-bit prime.
    }
    snprintf(buf, buflen, "/2ine-shm2-%016llx", (unsigned long long)hash);
}

APIRET mapNamedSharedMemory(PPVOID output, const char *name, ULONG size, ULONG flags)
{
    if (!output || !name || !*name ||
        (flags & ~(PAG_READ | PAG_WRITE | PAG_COMMIT | OBJ_TILE | OBJ_GETTABLE | OBJ_GIVEABLE)))
        return ERROR_INVALID_PARAMETER;
    char identifier[128];
    makeSharedMemName(name, identifier, sizeof identifier);
    if (!size)
    {
        grabLock(&GMutexDosCalls);
        for (SharedMemBlock *block = GSharedMem; block; block = block->next)
            if (!strcmp(block->shared_name, identifier))
            {
                block->freed = 0;
                *output = block->addr;
                ungrabLock(&GMutexDosCalls);
                return NO_ERROR;
            }
        ungrabLock(&GMutexDosCalls);
    }
    LxHostShared *object = NULL;
    const LxHostSharedResult opened = lx_host_shared_open(identifier, size, &object);
    if (opened != LX_HOST_SHARED_OK)
        return opened == LX_HOST_SHARED_MISSING  ? ERROR_FILE_NOT_FOUND
               : opened == LX_HOST_SHARED_EXISTS ? ERROR_DUPLICATE_NAME
                                                 : ERROR_NOT_ENOUGH_MEMORY;
    /* OS/2 shared pointers retain one linear address in every attached process. */
    uint64_t address = 0;
    LxGuestMapping *mapping =
        lx_host_shared_slot(object, 0x30000000, 0x70000000, 65536, &address)
            ? lx_execution_map_shared_at(GLoaderState.execution, object, (uint32)address)
            : NULL;
    lx_host_shared_close(object);
    if (!mapping)
        return ERROR_NOT_ENOUGH_MEMORY;
    SharedMemBlock *block = calloc(1, sizeof *block);
    if (!block)
    {
        lx_execution_unmap(GLoaderState.execution, mapping);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    strcpy(block->shared_name, identifier);
    block->mapping = mapping;
    block->addr = lx_execution_mapping_host(mapping);
    block->size = lx_execution_mapping_size(mapping);
    grabLock(&GMutexDosCalls);
    block->next = GSharedMem;
    GSharedMem = block;
    ungrabLock(&GMutexDosCalls);
    *output = block->addr;
    return NO_ERROR;
}

APIRET DosGetNamedSharedMem(PPVOID output, PSZ name, ULONG flags)
{
    TRACE_NATIVE("DosGetNamedSharedMem(%p, '%s', %u)", output, name ? name : "", (uint)flags);
    return mapNamedSharedMemory(output, name, 0, flags);
}

APIRET DosAllocSharedMem(PPVOID output, PSZ name, ULONG size, ULONG flags)
{
    TRACE_NATIVE("DosAllocSharedMem(%p, '%s', %u, %u)", output, name ? name : "", (uint)size,
                 (uint)flags);
    if (!size || size > UINT32_MAX - 4095)
        return ERROR_INVALID_PARAMETER;
    if (!name || !*name)
        return DosAllocMem(output, size, flags);
    return mapNamedSharedMemory(output, name, (size + 4095) & ~4095u, flags);
}

APIRET DosGiveSharedMem(PVOID pb, PID pid, ULONG flag)
{
    TRACE_NATIVE("DosGiveSharedMem(%p, %u, %u)", pb, (uint)pid, (uint)flag);
    /* Named shared mappings need no additional per-process host grant. */
    return NO_ERROR;
}

APIRET DosFreeMem(PVOID pb)
{
    TRACE_NATIVE("DosFreeMem(%p)", pb);

    /* Keep retired mappings until exit because CPPRMI36 still reads freed errno storage. */
    grabLock(&GMutexDosCalls);
    for (SharedMemBlock *block = GSharedMem; block; block = block->next)
    {
        if (block->addr == pb)
        {
            block->freed = 1;
            ungrabLock(&GMutexDosCalls);
            return NO_ERROR;
        }
    }
    ungrabLock(&GMutexDosCalls);

    return ERROR_INVALID_ADDRESS;
}

APIRET DosSubFreeMem(PVOID pbBase, PVOID pb, ULONG cb)
{
    TRACE_NATIVE("DosSubFreeMem(%p, %p, %u)", pbBase, pb, (uint)cb);
    free(pb);
    return NO_ERROR;
}

ULONG _DosSelToFlat(void *ptr)
{
    TRACE_NATIVE("DosSelToFlat(%p)", ptr);
    return (ULONG)GLoaderState.convert1616to32((uint32)ptr);
}

APIRET16 Dos16CreateCSAlias(USHORT selDS, PUSHORT pselCS)
{
    TRACE_NATIVE("Dos16CreateCSAlias(%u, %p)", (uint)selDS, pselCS);
    uint16 alias = selDS;
    if (GLoaderState.createCodeAlias)
        GLoaderState.createCodeAlias(selDS, &alias);
    if (pselCS)
        *pselCS = alias;
    return NO_ERROR;
}

APIRET16 Dos16AllocSeg(USHORT size, PUSHORT psel, USHORT flags)
{
    TRACE_NATIVE("Dos16AllocSeg(%u, %p, %u)", (uint)size, psel, (uint)flags);
    if (psel == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    else if ((flags & 0xFFF0) != 0)
    { // these bits are reserved and must be zero.
        return ERROR_INVALID_PARAMETER;
    }

    uint16 sel = 0xFFFF;

    void *addr = GLoaderState.allocSegment(&sel, 0);
    if (!addr)
    {
        assert(sel == 0xFFFF);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    assert(sel != 0xFFFF);

    if (flags & (1 << 0))
    {
        FIXME("segment is meant to be shareable through Dos16GiveSeg");
    }

    if (flags & (1 << 1))
    {
        FIXME("segment is meant to be shareable through Dos16GetSeg");
    }

    /* OS/2 exposes the tiled segment value through this API. */
    *psel = (USHORT)((sel << 3) | 7);
    return NO_ERROR;
}

APIRET16 Dos16ReallocSeg(USHORT size, USHORT sel)
{
    TRACE_NATIVE("Dos16ReallocSeg(%u, %u)", (uint)size, (uint)sel);

    /* Segments keep their full 64-KiB allocation until process exit. */
    FIXME("this implementation is severely lacking");

    return NO_ERROR;
}

APIRET DosQueryMem(PVOID pb, PULONG pcb, PULONG pflag)
{
    TRACE_NATIVE("DosQueryMem(%p, %p, %p)", pb, pcb, pflag);

    if (!pcb || !pflag || !*pcb)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    const uintptr_t address = (uintptr_t)pb;
    for (SharedMemBlock *block = GSharedMem; block; block = block->next)
    {
        const uintptr_t base = (uintptr_t)block->addr;
        if (address >= base && address - base < block->size)
        {
            size_t remaining = block->size - (address - base);
            if (*pcb > remaining)
                *pcb = remaining;
            *pflag = PAG_READ | PAG_WRITE | PAG_COMMIT |
                     (block->shared_name[0] ? 0x2000u : 0) | /* PAG_SHARED */
                     (address == base ? 0x10000u : 0);       /* PAG_BASE */
            ungrabLock(&GMutexDosCalls);
            TRACE_RET("DosQueryMem(%p) -> size=%u flags=%x", pb, (uint)*pcb, (uint)*pflag);
            return NO_ERROR;
        }
    }
    ungrabLock(&GMutexDosCalls);
    *pflag = PAG_READ | PAG_WRITE | PAG_COMMIT;
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
    FIXME("this needs to deal with reference counting and shared segments");
    /* OS/2 exposes the tiled segment value through this API. */
    LxModule *mod = GLoaderState.main_module;
    if (mod)
    {
        const uint16 alias = (uint16)(sel >> 3);
        for (uint32 i = 0; i < mod->num_mmaps; i++)
        {
            if (mod->mmaps[i].alias == alias)
                return NO_ERROR;
        }
    } /* CS aliases share data backing; retaining mappings keeps both aliases valid. */
    (void)sel;
    return NO_ERROR;
}

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
    void *base = GLoaderState.convert1616to32((ULONG)selector << 16);
    uint32 address;
    if (!base || (GLoaderState.execution &&
                  !lx_execution_address(GLoaderState.execution, base, extent, 3, &address)))
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
