/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Private and shared memory objects: DosAllocMem, DosAllocSharedMem and their queries. */
#include "os2/baseos/doscalls/internal.h"

enum
{
    /* OS/2 memory objects start on 64 KB boundaries. */
    OBJECT_ALIGNMENT = 0x10000,
    /* A tiled object's 64 KB tile n is addressed by LDT selector index n. */
    TILE_SHIFT = 16
};

/* Shared objects take one address in every process, from this range. */
static const uint64_t SHARED_ARENA_BASE = 0x30000000;
static const uint64_t SHARED_ARENA_LIMIT = 0x70000000;

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
            if (offset || selector != address >> TILE_SHIFT)
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
            GProcessState.freeSelector((base + (uint32)installed) >> TILE_SHIFT);
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

    const size_t pagesize = GUEST_PAGE_SIZE;
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
                                                 OBJECT_ALIGNMENT);
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
    uint64_t hash = FNV1A_64_OFFSET_BASIS;
    const char *ball = getenv("TWOINE_BALL");
    if (ball)
    {
        for (const char *p = ball; *p; p++)
            hash = (hash ^ (unsigned char)*p) * FNV1A_64_PRIME;
        hash = (hash ^ FNV_SCOPE_SEPARATOR) * FNV1A_64_PRIME;
    }
    for (const char *ptr = os2name; *ptr; ptr++)
    {
        unsigned char ch = (unsigned char)*ptr;
        if ((ch >= 'A') && (ch <= 'Z'))
            ch += 'a' - 'A';
        hash ^= ch;
        hash *= FNV1A_64_PRIME;
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
                APIRET result = DosGetSharedMemAddress(address, flags & fPERM);
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
            ((flags & fPERM) & ~(attributes & fPERM)))
        {
            lx_host_shared_close(object);
            return ERROR_ACCESS_DENIED;
        }
    }
    /* OS/2 shared pointers retain one linear address in every attached process. */
    uint64_t address = 0;
    LxGuestMapping *mapping =
        lx_host_shared_slot(object, SHARED_ARENA_BASE, SHARED_ARENA_LIMIT, OBJECT_ALIGNMENT,
                            &address)
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
    block->flags = (flags & fPERM) | (attributes & (OBJ_GETTABLE | OBJ_GIVEABLE));
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
    if (!size || size > UINT32_MAX - (GUEST_PAGE_SIZE - 1))
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
        return mapSharedMemory(output, generated,
                               (size + GUEST_PAGE_SIZE - 1) & ~(GUEST_PAGE_SIZE - 1u), flags, TRUE);
    }
    return mapNamedSharedMemory(output, name,
                                (size + GUEST_PAGE_SIZE - 1) & ~(GUEST_PAGE_SIZE - 1u), flags);
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
                        flag = block->flags & fPERM;
                    result = (flag & ~(block->flags & fPERM)) ? ERROR_ACCESS_DENIED : NO_ERROR;
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
        permission = attributes & fPERM;
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
            *pflag = PAG_READ | PAG_WRITE | PAG_COMMIT | (block->shared_name[0] ? PAG_SHARED : 0) |
                     (address == base ? PAG_BASE : 0);
            ungrabLock(&GMutexDosCalls);
            TRACE_RET("DosQueryMem(%p) -> size=%u flags=%x", pb, (uint)*pcb, (uint)*pflag);
            return NO_ERROR;
        }
    }
    ungrabLock(&GMutexDosCalls);
    *pflag = PAG_READ | PAG_WRITE | PAG_COMMIT;
    return NO_ERROR;
}
