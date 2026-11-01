/* Public domain. No rights reserved. Wholly AI-authored. */
/* Local memory handles retain their mappings and movable-block lock counts. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_sync.h"
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
    if (size > UINT32_MAX - 4095)
        return NULL;
    size_t capacity = ((size_t)(size ? size : 1) + 4095) & ~(size_t)4095;
    return lx_execution_map(GLoaderState.execution, 0, capacity, LX_GUEST_READ | LX_GUEST_WRITE, 0);
}
ULONG LocalAlloc(ULONG flags, ULONG size)
{
    TRACE_NATIVE("LocalAlloc(%X, %u)", flags, size);
    if (flags & ~0xF72u)
    {
        open32SetError(87);
        return 0;
    }
    LocalMemory *block = calloc(1, sizeof *block);
    if (!block)
    {
        open32SetError(8);
        return 0;
    }
    block->size = size;
    block->flags = flags & 0xF02;
    if (size || !(flags & 2))
    {
        block->mapping = allocate(size);
        if (!block->mapping)
        {
            free(block);
            open32SetError(8);
            return 0;
        }
    }
    block->handle = flags & 2 ? lx_handle_create(LX_HANDLE_OPEN32_MEMORY, block, NULL)
                              : lx_execution_mapping_address(block->mapping);
    if (!block->handle)
    {
        if (block->mapping)
            lx_execution_unmap(GLoaderState.execution, block->mapping);
        free(block);
        open32SetError(8);
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
        open32SetError(6);
    else if (block->locks)
        open32SetError(212); /* ERROR_LOCKED */
    else if (block->mapping && !lx_execution_unmap(GLoaderState.execution, block->mapping))
        open32SetError(212);
    else
    {
        *link = block->next;
        if (block->flags & 2)
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
        open32SetError(6);
    else if (!block->mapping)
        open32SetError(157); /* ERROR_DISCARDED */
    else if ((block->flags & 2) && block->locks == 255)
        open32SetError(8);
    else
    {
        result = lx_execution_mapping_address(block->mapping);
        if (block->flags & 2)
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
        open32SetError(6);
    else if (!(block->flags & 2))
        open32SetError(0);
    else if (!block->locks)
        open32SetError(158); /* ERROR_NOT_LOCKED */
    else
    {
        result = --block->locks != 0;
        if (!result)
            open32SetError(0);
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
        open32SetError(6);
    lx_host_monitor_unlock(monitor);
    return result;
}
ULONG LocalFlags(ULONG handle)
{
    lock_heap();
    LocalMemory *block = find_block(handle);
    ULONG result =
        block ? (block->flags & 0xF00) | block->locks | (block->mapping ? 0 : 0x4000) : 0x8000;
    if (!block)
        open32SetError(6);
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
        open32SetError(6);
    lx_host_monitor_unlock(monitor);
    return result;
}
ULONG LocalReAlloc(ULONG handle, ULONG size, ULONG flags)
{
    if (flags & ~0xFF2u)
    {
        open32SetError(87);
        return 0;
    }
    lock_heap();
    LocalMemory *block = find_block(handle);
    ULONG result = 0;
    if (!block)
        open32SetError(6);
    else if (flags & 0x80)
    {
        block->flags = (block->flags & 2) | (flags & 0xF00);
        result = handle;
    }
    else if (!size && (block->flags & 2))
    {
        if (block->locks ||
            (block->mapping && !lx_execution_unmap(GLoaderState.execution, block->mapping)))
            open32SetError(212);
        else
        {
            block->mapping = NULL;
            block->size = 0;
            result = handle;
        }
    }
    else if (block->mapping && size <= lx_execution_mapping_size(block->mapping))
    {
        if ((flags & 0x40) && size > block->size)
            memset((uint8 *)lx_execution_mapping_host(block->mapping) + block->size, 0,
                   size - block->size);
        block->size = size;
        result = handle;
    }
    else if (block->locks || (!(block->flags & 2) && !(flags & 2)))
        open32SetError(8);
    else
    {
        LxGuestMapping *replacement = allocate(size);
        if (!replacement)
            open32SetError(8);
        else
        {
            if (block->mapping)
                memcpy(lx_execution_mapping_host(replacement),
                       lx_execution_mapping_host(block->mapping),
                       block->size < size ? block->size : size);
            if (block->mapping && !lx_execution_unmap(GLoaderState.execution, block->mapping))
            {
                lx_execution_unmap(GLoaderState.execution, replacement);
                open32SetError(212);
            }
            else
            {
                block->mapping = replacement;
                block->size = size;
                if (!(block->flags & 2))
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
            lx_execution_unmap(GLoaderState.execution, block->mapping);
        if (block->flags & 2)
            lx_handle_close(block->handle, LX_HANDLE_OPEN32_MEMORY);
        free(block);
    }
    lx_host_monitor_unlock(monitor);
}

ULONG GlobalAlloc(ULONG flags, ULONG size)
{
    if (flags & ~0x7F72u)
    {
        open32SetError(87);
        return 0;
    }
    /* Win32 share/bank/notification hints do not change process-heap ownership. */
    return LocalAlloc(flags & 0x172, size);
}
ULONG GlobalReAlloc(ULONG handle, ULONG size, ULONG flags)
{
    if (flags & ~0x7FF2u)
    {
        open32SetError(87);
        return 0;
    }
    return LocalReAlloc(handle, size, flags & 0x1F2);
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
    return LocalFlags(handle) & 0xC1FF;
}
