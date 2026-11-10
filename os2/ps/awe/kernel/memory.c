/* Public domain. No rights reserved. Wholly AI-authored. */
/* Local memory handles retain their mappings and movable-block lock counts. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/bsedos.h"
#include "host/sync.h"

enum
{
    /* LocalAlloc keeps the moveable bit and the discard level of its flags. */
    LOCAL_KEPT_FLAGS = WIN32_LMEM_DISCARDABLE | WIN32_LMEM_MOVEABLE,
    LOCAL_REALLOC_FLAGS = WIN32_LMEM_VALID_FLAGS | WIN32_LMEM_MODIFY,
    /* GlobalAlloc and GlobalReAlloc pass on the flags with a LocalAlloc meaning. */
    GLOBAL_ALLOC_FLAGS = WIN32_GMEM_MOVEABLE | WIN32_GMEM_NOCOMPACT | WIN32_GMEM_NODISCARD |
                         WIN32_GMEM_ZEROINIT | WIN32_GMEM_DISCARDABLE,
    GLOBAL_REALLOC_FLAGS = GLOBAL_ALLOC_FLAGS | WIN32_GMEM_MODIFY,
    GLOBAL_REALLOC_VALID_FLAGS = WIN32_GMEM_VALID_FLAGS | WIN32_GMEM_MODIFY,
    GLOBAL_FLAGS_MASK = WIN32_GMEM_INVALID_HANDLE | WIN32_GMEM_DISCARDED | WIN32_GMEM_DISCARDABLE |
                        WIN32_GMEM_LOCKCOUNT
};

typedef struct LocalMemory
{
    ULONG handle, size, flags, locks;
    LxGuestMapping *mapping;
    struct LocalMemory *next;
} LocalMemory;

static LocalMemory *blocks;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;

static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}

static LocalMemory *find_block(ULONG handle)
{
    for (LocalMemory *block = blocks; block; block = block->next)
        if (block->handle == handle)
            return block;
    return NULL;
}

static void lock_heap(void)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
}

static LxGuestMapping *allocate(ULONG size)
{
    if (size > UINT32_MAX - (OPEN32_PAGE_SIZE - 1))
        return NULL;
    size_t capacity =
        ((size_t)(size ? size : 1) + OPEN32_PAGE_SIZE - 1) & ~(size_t)(OPEN32_PAGE_SIZE - 1);
    return lx_execution_map(GProcessState.execution, 0, capacity, LX_GUEST_READ | LX_GUEST_WRITE,
                            0);
}

ULONG LocalAlloc(ULONG flags, ULONG size)
{
    TRACE_NATIVE("LocalAlloc(%X, %u)", flags, size);
    if (flags & ~(unsigned)WIN32_LMEM_VALID_FLAGS)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LocalMemory *block = calloc(1, sizeof *block);
    if (!block)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    block->size = size;
    block->flags = flags & LOCAL_KEPT_FLAGS;
    /* A moveable block of size 0 starts discarded. */
    if (size || !(flags & WIN32_LMEM_MOVEABLE))
    {
        block->mapping = allocate(size);
        if (!block->mapping)
        {
            free(block);
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
    }
    block->handle = flags & WIN32_LMEM_MOVEABLE
                        ? lx_handle_create(LX_HANDLE_OPEN32_MEMORY, block, NULL)
                        : lx_execution_mapping_address(block->mapping);
    if (!block->handle)
    {
        if (block->mapping)
            lx_execution_unmap(GProcessState.execution, block->mapping);
        free(block);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    lock_heap();
    block->next = blocks;
    blocks = block;
    lx_host_monitor_unlock(monitor);
    return block->handle;
}

ULONG LocalFree(ULONG handle)
{
    if (!handle)
        return 0;
    lock_heap();
    LocalMemory **link = &blocks;
    while (*link && (*link)->handle != handle)
        link = &(*link)->next;
    LocalMemory *block = *link;
    ULONG result = handle;
    if (!block)
        open32SetError(ERROR_INVALID_HANDLE);
    else if (block->locks)
        open32SetError(ERROR_LOCKED);
    else if (block->mapping && !lx_execution_unmap(GProcessState.execution, block->mapping))
        open32SetError(ERROR_LOCKED);
    else
    {
        *link = block->next;
        if (block->flags & WIN32_LMEM_MOVEABLE)
            lx_handle_close(handle, LX_HANDLE_OPEN32_MEMORY);
        free(block);
        result = 0;
    }
    lx_host_monitor_unlock(monitor);
    return result;
}

ULONG LocalLock(ULONG handle)
{
    lock_heap();
    LocalMemory *block = find_block(handle);
    ULONG result = 0;
    if (!block)
        open32SetError(ERROR_INVALID_HANDLE);
    else if (!block->mapping)
        open32SetError(ERROR_DISCARDED);
    else if ((block->flags & WIN32_LMEM_MOVEABLE) && block->locks == WIN32_LMEM_LOCKCOUNT)
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    else
    {
        result = lx_execution_mapping_address(block->mapping);
        if (block->flags & WIN32_LMEM_MOVEABLE)
            block->locks++;
    }
    lx_host_monitor_unlock(monitor);
    return result;
}

BOOL LocalUnlock(ULONG handle)
{
    lock_heap();
    LocalMemory *block = find_block(handle);
    BOOL result = FALSE;
    if (!block)
        open32SetError(ERROR_INVALID_HANDLE);
    else if (!(block->flags & WIN32_LMEM_MOVEABLE))
        open32SetError(ERROR_SUCCESS);
    else if (!block->locks)
        open32SetError(ERROR_NOT_LOCKED);
    else
    {
        result = --block->locks != 0;
        if (!result)
            open32SetError(ERROR_SUCCESS);
    }
    lx_host_monitor_unlock(monitor);
    return result;
}

ULONG LocalSize(ULONG handle)
{
    lock_heap();
    LocalMemory *block = find_block(handle);
    ULONG result = block ? block->size : 0;
    if (!block)
        open32SetError(ERROR_INVALID_HANDLE);
    lx_host_monitor_unlock(monitor);
    return result;
}

ULONG LocalFlags(ULONG handle)
{
    lock_heap();
    LocalMemory *block = find_block(handle);
    ULONG result = block ? (block->flags & WIN32_LMEM_DISCARDABLE) | block->locks |
                               (block->mapping ? 0 : WIN32_LMEM_DISCARDED)
                         : WIN32_LMEM_INVALID_HANDLE;
    if (!block)
        open32SetError(ERROR_INVALID_HANDLE);
    lx_host_monitor_unlock(monitor);
    return result;
}

ULONG LocalHandle(ULONG pointer)
{
    lock_heap();
    ULONG result = 0;
    for (LocalMemory *block = blocks; block; block = block->next)
        if (block->mapping && pointer == lx_execution_mapping_address(block->mapping))
        {
            result = block->handle;
            break;
        }
    if (!result)
        open32SetError(ERROR_INVALID_HANDLE);
    lx_host_monitor_unlock(monitor);
    return result;
}

ULONG LocalReAlloc(ULONG handle, ULONG size, ULONG flags)
{
    if (flags & ~(unsigned)LOCAL_REALLOC_FLAGS)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    lock_heap();
    LocalMemory *block = find_block(handle);
    ULONG result = 0;
    if (!block)
        open32SetError(ERROR_INVALID_HANDLE);
    else if (flags & WIN32_LMEM_MODIFY)
    {
        block->flags = (block->flags & WIN32_LMEM_MOVEABLE) | (flags & WIN32_LMEM_DISCARDABLE);
        result = handle;
    }
    else if (!size && (block->flags & WIN32_LMEM_MOVEABLE))
    {
        if (block->locks ||
            (block->mapping && !lx_execution_unmap(GProcessState.execution, block->mapping)))
            open32SetError(ERROR_LOCKED);
        else
        {
            block->mapping = NULL;
            block->size = 0;
            result = handle;
        }
    }
    else if (block->mapping && size <= lx_execution_mapping_size(block->mapping))
    {
        if ((flags & WIN32_LMEM_ZEROINIT) && size > block->size)
            memset((uint8 *)lx_execution_mapping_host(block->mapping) + block->size, 0,
                   size - block->size);
        block->size = size;
        result = handle;
    }
    else if (block->locks ||
             (!(block->flags & WIN32_LMEM_MOVEABLE) && !(flags & WIN32_LMEM_MOVEABLE)))
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    else
    {
        LxGuestMapping *replacement = allocate(size);
        if (!replacement)
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        else
        {
            if (block->mapping)
                memcpy(lx_execution_mapping_host(replacement),
                       lx_execution_mapping_host(block->mapping),
                       block->size < size ? block->size : size);
            if (block->mapping && !lx_execution_unmap(GProcessState.execution, block->mapping))
            {
                lx_execution_unmap(GProcessState.execution, replacement);
                open32SetError(ERROR_LOCKED);
            }
            else
            {
                block->mapping = replacement;
                block->size = size;
                if (!(block->flags & WIN32_LMEM_MOVEABLE))
                    block->handle = lx_execution_mapping_address(replacement);
                result = block->handle;
            }
        }
    }
    lx_host_monitor_unlock(monitor);
    return result;
}

void open32MemoryCleanup(void)
{
    lock_heap();
    while (blocks)
    {
        LocalMemory *block = blocks;
        blocks = block->next;
        if (block->mapping)
            lx_execution_unmap(GProcessState.execution, block->mapping);
        if (block->flags & WIN32_LMEM_MOVEABLE)
            lx_handle_close(block->handle, LX_HANDLE_OPEN32_MEMORY);
        free(block);
    }
    lx_host_monitor_unlock(monitor);
}

ULONG GlobalAlloc(ULONG flags, ULONG size)
{
    if (flags & ~(unsigned)WIN32_GMEM_VALID_FLAGS)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* Win32 share/bank/notification hints do not change process-heap ownership. */
    return LocalAlloc(flags & GLOBAL_ALLOC_FLAGS, size);
}

ULONG GlobalReAlloc(ULONG handle, ULONG size, ULONG flags)
{
    if (flags & ~(unsigned)GLOBAL_REALLOC_VALID_FLAGS)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return LocalReAlloc(handle, size, flags & GLOBAL_REALLOC_FLAGS);
}

ULONG GlobalFree(ULONG handle)
{
    return LocalFree(handle);
}

ULONG GlobalLock(ULONG handle)
{
    return LocalLock(handle);
}

BOOL GlobalUnlock(ULONG handle)
{
    return LocalUnlock(handle);
}

ULONG GlobalSize(ULONG handle)
{
    return LocalSize(handle);
}

ULONG GlobalHandle(ULONG pointer)
{
    return LocalHandle(pointer);
}

ULONG GlobalFlags(ULONG handle)
{
    return LocalFlags(handle) & GLOBAL_FLAGS_MASK;
}

VOID ZeroMemory(PVOID output, ULONG count)
{
    if (count)
        memset(output, 0, count);
}

VOID GlobalMemoryStatus(PVOID output)
{
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return;
    }
    typedef APIRET(OS2API * Fn)(ULONG, ULONG, PVOID, ULONG);
    Fn query = (Fn)open32DosService("DosQuerySysInfo");
    ULONG info[QSV_MAXSHMEM - QSV_TOTPHYSMEM + 1] = {0};
    ULONG error =
        query ? query(QSV_TOTPHYSMEM, QSV_MAXSHMEM, info, sizeof info) : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
    {
        open32SetError(error);
        return;
    }
    /* TOTPHYSMEM, TOTRESMEM, TOTAVAILMEM: the paging file is what exceeds physical memory. */
    ULONG available = info[0] - info[1], paging = info[2] - info[0];
    /* MEMORYSTATUS, from dwLength to dwAvailVirtual. */
    ULONG fields[] = {WIN32_MEMORYSTATUS_SIZE,
                      info[0] ? (ULONG)((uint64_t)info[1] * 100 / info[0]) : 0,
                      info[0],
                      available,
                      paging,
                      paging,
                      info[0],
                      available};
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u32((uint8 *)output + i * sizeof *fields, fields[i]);
}
