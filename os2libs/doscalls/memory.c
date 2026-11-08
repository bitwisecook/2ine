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
    return GProcessState.convert32to1616(ptr);
}

static LxGuestMapping *allocateTiledMemory(size_t size)
{
    /* Data selectors encode 64-KiB bases; the final LDT tile is the near sentinel. */
    const uint32 first = 0x10000000, limit = 0x1fff0000, tile_size = 0x10000;
    if (size > limit - first)
        return NULL;
    for (uint32 base = first; base <= limit - size; base += tile_size)
    {
        LxGuestMapping *mapping =
            lx_execution_map(GProcessState.execution, base, size,
                             LX_GUEST_READ | LX_GUEST_WRITE | LX_GUEST_EXECUTE, 1);
        if (!mapping)
            continue;
        size_t installed = 0;
        for (; installed < size; installed += tile_size)
        {
            uint16 selector, offset;
            uint32 address = base + (uint32)installed;
            if (!GProcessState.findSelector(address, &selector, &offset, 0))
                break;
            if (offset || selector != address >> 16)
            {
                GProcessState.freeSelector(selector);
                break;
            }
        }
        if (installed >= size)
            return mapping;
        while (installed)
        {
            installed -= tile_size;
            GProcessState.freeSelector((base + (uint32)installed) >> 16);
        }
        lx_execution_unmap(GProcessState.execution, mapping);
    }
    return NULL;
}

APIRET DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosAllocMem(%p, %u, %u)", ppb, (uint)cb, (uint)flag);

    if ((!ppb) || (cb == 0))
        return ERROR_INVALID_PARAMETER;

    const size_t pagesize = 4096;
    if (cb > UINT32_MAX - (pagesize - 1))
        return ERROR_NOT_ENOUGH_MEMORY;
    size_t size = (size_t)cb;
    if ((size % pagesize) != 0)
        size += pagesize - (size % pagesize);

    LxGuestMapping *mapping = NULL;
    void *addr;
    if (GProcessState.execution)
    {
        /* OS/2 object bases are 64-KiB aligned, independently of page-sized lengths
           and OBJ_TILE selector allocation. Original SmartHeap uses this contract
           to recover a pool header from an interior pointer. The original 386 page
           tables cannot inhibit instruction fetch from readable memory. */
        mapping = flag & OBJ_TILE
                      ? allocateTiledMemory(size)
                      : lx_execution_map_aligned(GProcessState.execution, 0, size,
                                                 LX_GUEST_READ | LX_GUEST_WRITE | LX_GUEST_EXECUTE,
                                                 0x10000);
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
            lx_execution_unmap(GProcessState.execution, mapping);
        else
            lx_host_memory_free(addr, size);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    block->addr = addr;
    block->size = size;
    block->mapping = mapping;
    block->freed = 0;
    block->flags = flag;

    grabLock(&GMutexDosCalls);
    block->next = GSharedMem;
    GSharedMem = block;
    ungrabLock(&GMutexDosCalls);

    *ppb = addr;
    TRACE_RET("DosAllocMem(%u) -> %p", (uint)cb, addr);
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
    const char *ball = getenv("TWOINE_BALL");
    if (ball)
    {
        for (const char *p = ball; *p; p++)
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

static APIRET mapSharedMemory(PPVOID output, const char *name, ULONG size, ULONG flags,
                              BOOL anonymous)
{
    if (!output || !name || !*name ||
        (flags & ~(PAG_READ | PAG_WRITE | PAG_EXECUTE | PAG_COMMIT | OBJ_TILE | OBJ_GETTABLE |
                   OBJ_GIVEABLE)))
        return ERROR_INVALID_PARAMETER;
    char identifier[128];
    makeSharedMemName(name, identifier, sizeof identifier);
    if (!size)
    {
        grabLock(&GMutexDosCalls);
        for (SharedMemBlock *block = GSharedMem; block; block = block->next)
            if (!strcmp(block->shared_name, identifier))
            {
                ULONG address = lx_execution_mapping_address(block->mapping);
                void *host = block->addr;
                ungrabLock(&GMutexDosCalls);
                APIRET result = DosGetSharedMemAddress(address, flags & 7u);
                if (!result)
                    *output = host;
                return result;
            }
        ungrabLock(&GMutexDosCalls);
    }
    LxHostShared *object = NULL;
    const LxHostSharedResult opened = lx_host_shared_open(identifier, size, &object);
    if (opened != LX_HOST_SHARED_OK)
        return opened == LX_HOST_SHARED_MISSING  ? ERROR_FILE_NOT_FOUND
               : opened == LX_HOST_SHARED_EXISTS ? ERROR_ALREADY_EXISTS
                                                 : ERROR_NOT_ENOUGH_MEMORY;
    ULONG attributes = flags | (anonymous ? 0 : OBJ_GETTABLE);
    if (size && !lx_host_shared_publish(object, GProcessState.pib.process_id, attributes))
    {
        lx_host_shared_close(object);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!size)
    {
        ULONG owner, permissions;
        if (!lx_host_shared_query_access(object, GProcessState.pib.process_id, &owner, &attributes,
                                         &permissions) ||
            ((flags & 7u) & ~(attributes & 7u)))
        {
            lx_host_shared_close(object);
            return ERROR_ACCESS_DENIED;
        }
    }
    /* OS/2 shared pointers retain one linear address in every attached process. */
    uint64_t address = 0;
    LxGuestMapping *mapping =
        lx_host_shared_slot(object, 0x30000000, 0x70000000, 65536, &address)
            ? lx_execution_map_shared_at(GProcessState.execution, object, (uint32)address)
            : NULL;
    lx_host_shared_close(object);
    if (!mapping)
        return ERROR_NOT_ENOUGH_MEMORY;
    unsigned protection = flags & (PAG_READ | PAG_WRITE | PAG_EXECUTE);
    if (protection & (PAG_READ | PAG_WRITE))
        protection |= LX_GUEST_READ | LX_GUEST_EXECUTE;
    if (!lx_execution_protect(GProcessState.execution, mapping, protection))
    {
        lx_execution_unmap(GProcessState.execution, mapping);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    SharedMemBlock *block = calloc(1, sizeof *block);
    if (!block)
    {
        lx_execution_unmap(GProcessState.execution, mapping);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    strcpy(block->shared_name, identifier);
    block->mapping = mapping;
    block->addr = lx_execution_mapping_host(mapping);
    block->size = lx_execution_mapping_size(mapping);
    block->flags = (flags & 7u) | (attributes & (OBJ_GETTABLE | OBJ_GIVEABLE));
    grabLock(&GMutexDosCalls);
    block->next = GSharedMem;
    GSharedMem = block;
    ungrabLock(&GMutexDosCalls);
    *output = block->addr;
    return NO_ERROR;
}

APIRET mapNamedSharedMemory(PPVOID output, const char *name, ULONG size, ULONG flags)
{
    return mapSharedMemory(output, name, size, flags, FALSE);
}

BOOL lx_dos_shared_address(ULONG address, ULONG size)
{
    BOOL shared = FALSE;
    grabLock(&GMutexDosCalls);
    for (SharedMemBlock *block = GSharedMem; block; block = block->next)
    {
        if (!block->mapping || !block->shared_name[0] || block->freed)
            continue;
        uint32 first = lx_execution_mapping_address(block->mapping);
        if (address >= first && (uint64)address - first <= block->size &&
            size <= block->size - ((uint64)address - first))
        {
            shared = TRUE;
            break;
        }
    }
    ungrabLock(&GMutexDosCalls);
    return shared;
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
    {
        static ULONG serial;
        grabLock(&GMutexDosCalls);
        ULONG identity = ++serial;
        ungrabLock(&GMutexDosCalls);
        char generated[96];
        snprintf(generated, sizeof generated, "\\SHAREMEM\\2INE-ANON-%08x-%08x",
                 (unsigned)GProcessState.pib.process_id, (unsigned)identity);
        return mapSharedMemory(output, generated, (size + 4095) & ~4095u, flags, TRUE);
    }
    return mapNamedSharedMemory(output, name, (size + 4095) & ~4095u, flags);
}

APIRET DosGiveSharedMem(PVOID pb, PID pid, ULONG flag)
{
    TRACE_NATIVE("DosGiveSharedMem(%p, %u, %u)", pb, (uint)pid, (uint)flag);
    if ((flag & ~(PAG_READ | PAG_WRITE | PAG_EXECUTE | PAG_DEFAULT)) ||
        ((flag & PAG_DEFAULT) && flag != PAG_DEFAULT))
        return ERROR_INVALID_PARAMETER;
    ULONG address;
    if (!pid)
        return ERROR_INVALID_PROCID;
    if (!GProcessState.execution ||
        !lx_execution_address(GProcessState.execution, pb, 1, 0, &address))
        return ERROR_INVALID_ADDRESS;
    APIRET result = ERROR_INVALID_ADDRESS;
    char name[128] = {0};
    grabLock(&GMutexDosCalls);
    BOOL known = pid == GProcessState.pib.process_id || pid == GProcessState.pib.parent_id;
    for (ChildProcess *child = GChildren; child && !known; child = child->next)
        known = lx_host_child_id(child->host) == pid;
    if (!known)
        known = lxIsProcessPeer(pid);
    if (!known)
        result = ERROR_INVALID_PROCID;
    else
        for (SharedMemBlock *block = GSharedMem; block; block = block->next)
            if (block->mapping && block->shared_name[0] && !block->freed &&
                lx_execution_mapping_address(block->mapping) == address)
            {
                if (!(block->flags & (OBJ_GIVEABLE | OBJ_GETTABLE)))
                    result = ERROR_ACCESS_DENIED;
                else
                {
                    if (flag & PAG_DEFAULT)
                        flag = block->flags & 7u;
                    result = (flag & ~(block->flags & 7u)) ? ERROR_ACCESS_DENIED : NO_ERROR;
                    strcpy(name, block->shared_name);
                }
                break;
            }
    ungrabLock(&GMutexDosCalls);
    if (result)
        return result;
    LxHostShared *object = NULL;
    if (lx_host_shared_open(name, 0, &object) != LX_HOST_SHARED_OK)
        return ERROR_INVALID_ADDRESS;
    result = lx_host_shared_grant(object, pid, flag) ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
    lx_host_shared_close(object);
    return result;
}

APIRET DosGetSharedMemAddress(ULONG address, ULONG flags)
{
    if ((flags & ~(PAG_READ | PAG_WRITE | PAG_EXECUTE | PAG_DEFAULT)) ||
        ((flags & PAG_DEFAULT) && flags != PAG_DEFAULT))
        return ERROR_INVALID_PARAMETER;
    if (!GProcessState.execution || !address)
        return ERROR_INVALID_ADDRESS;
    LxHostShared *object = NULL;
    char name[128];
    LxHostSharedResult found = lx_host_shared_find(address, &object, name, sizeof name);
    if (found != LX_HOST_SHARED_OK)
        return found == LX_HOST_SHARED_DENIED ? ERROR_ACCESS_DENIED : ERROR_INVALID_ADDRESS;
    ULONG owner, attributes, permission;
    APIRET result = ERROR_ACCESS_DENIED;
    if (!lx_host_shared_query_access(object, GProcessState.pib.process_id, &owner, &attributes,
                                     &permission))
        goto done;
    if (owner == GProcessState.pib.process_id || (attributes & OBJ_GETTABLE))
        permission = attributes & 7u;
    else if (!permission)
        goto done;
    if (flags & PAG_DEFAULT)
        flags = permission;
    if (flags & ~permission)
        goto done;
    grabLock(&GMutexDosCalls);
    for (SharedMemBlock *existing = GSharedMem; existing; existing = existing->next)
        if (existing->mapping && existing->shared_name[0] &&
            lx_execution_mapping_address(existing->mapping) == address)
        {
            unsigned protection = (existing->flags | flags) & 7u;
            if (protection & (PAG_READ | PAG_WRITE))
                protection |= LX_GUEST_READ | LX_GUEST_EXECUTE;
            result = lx_execution_protect(GProcessState.execution, existing->mapping, protection)
                         ? NO_ERROR
                         : ERROR_ACCESS_DENIED;
            if (!result)
            {
                existing->flags |= flags;
                existing->freed = 0;
            }
            ungrabLock(&GMutexDosCalls);
            goto done;
        }
    SharedMemBlock *block = calloc(1, sizeof *block);
    if (!block)
    {
        ungrabLock(&GMutexDosCalls);
        result = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    LxGuestMapping *mapping = lx_execution_map_shared_at(GProcessState.execution, object, address);
    if (!mapping)
    {
        ungrabLock(&GMutexDosCalls);
        free(block);
        result = ERROR_INVALID_ADDRESS;
        goto done;
    }
    /* Readable original x86 pages also permit instruction fetch. */
    unsigned protection = flags;
    if (protection & (PAG_READ | PAG_WRITE))
        protection |= LX_GUEST_READ | LX_GUEST_EXECUTE;
    if (!lx_execution_protect(GProcessState.execution, mapping, protection))
    {
        lx_execution_unmap(GProcessState.execution, mapping);
        ungrabLock(&GMutexDosCalls);
        free(block);
        result = ERROR_ACCESS_DENIED;
        goto done;
    }
    block->mapping = mapping;
    block->addr = lx_execution_mapping_host(mapping);
    block->size = lx_execution_mapping_size(mapping);
    block->flags = flags | (attributes & (OBJ_GETTABLE | OBJ_GIVEABLE));
    strcpy(block->shared_name, name);
    block->next = GSharedMem;
    GSharedMem = block;
    ungrabLock(&GMutexDosCalls);
    result = NO_ERROR;
done:
    lx_host_shared_close(object);
    return result;
}

APIRET DosGetSharedMem(PVOID pointer, ULONG flags)
{
    ULONG address;
    if (GProcessState.execution &&
        lx_execution_address(GProcessState.execution, pointer, 1, 0, &address))
        return DosGetSharedMemAddress(address, flags);
    return (uintptr_t)pointer <= UINT32_MAX
               ? DosGetSharedMemAddress((ULONG)(uintptr_t)pointer, flags)
               : ERROR_INVALID_ADDRESS;
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

static ULONG segment_sizes[LX_MAX_LDT_SLOTS];
static ULONG segment_capacities[LX_MAX_LDT_SLOTS];
static unsigned char owned_segments[LX_MAX_LDT_SLOTS];

APIRET16 Dos16SizeSeg(USHORT selector, PULONG size)
{
    if (!size || !(selector & 4))
        return ERROR_INVALID_PARAMETER;
    unsigned index = selector >> 3;
    grabLock(&GMutexDosCalls);
    ULONG length = segment_sizes[index];
    if (!length)
        for (LxModule *module = GProcessState.loaded_modules; module && !length;
             module = module->next)
            for (ULONG i = 0; i < module->num_mmaps; i++)
                if (module->mmaps[i].alias == index)
                    length = module->mmaps[i].size > 65536 ? 65536 : module->mmaps[i].size;
    if (!length && GProcessState.main_module)
        for (ULONG i = 0; i < GProcessState.main_module->num_mmaps; i++)
            if (GProcessState.main_module->mmaps[i].alias == index)
            {
                size_t extent = GProcessState.main_module->mmaps[i].size;
                length = extent > 65536 ? 65536 : extent;
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
    *selector = pointer >> 16;
    grabLock(&GMutexDosCalls);
    for (SharedMemBlock *block = GSharedMem; block; block = block->next)
        if (block->addr == memory)
        {
            segment_sizes[*selector >> 3] = block->size > 65536 ? 65536 : block->size;
            segment_capacities[*selector >> 3] = segment_sizes[*selector >> 3];
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
    else if ((flags & 0xFFF0) != 0)
    { // these bits are reserved and must be zero.
        return ERROR_INVALID_PARAMETER;
    }

    uint16 sel = 0xFFFF;

    void *addr = GProcessState.allocSegment(&sel, 0);
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
    grabLock(&GMutexDosCalls);
    segment_sizes[sel] = size ? size : 65536;
    segment_capacities[sel] = 65536;
    owned_segments[sel] = 1;
    ungrabLock(&GMutexDosCalls);
    *psel = (USHORT)((sel << 3) | 7);
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
    ULONG requested = size ? size : 65536;
    if (requested > segment_capacities[sel >> 3])
    {
        ungrabLock(&GMutexDosCalls);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    segment_sizes[sel >> 3] = requested;
    ungrabLock(&GMutexDosCalls);
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
    if (!(sel & 4))
        return ERROR_INVALID_SEGMENT_NUMBER;
    unsigned index = sel >> 3;
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
