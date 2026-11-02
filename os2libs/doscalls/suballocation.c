/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pool-relative free lists remain valid when another process attaches the same memory. */
#include "os2libs/doscalls/internal.h"
enum
{
    POOL_MAGIC = 0x32425553,
    POOL_HEADER = 64,
    POOL_SIZE = 4,
    POOL_FREE = 8,
    POOL_OPTIONS = 12,
    POOL_LOCK = 16
};
static uint32 word(const uint8 *pool, uint32 offset)
{
    return lx_guest_load_u32(pool + offset);
}
static void store(uint8 *pool, uint32 offset, uint32 value)
{
    lx_guest_store_u32(pool + offset, value);
}
static APIRET open_pool(void *base, LxGuestBorrow *borrow)
{
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, base, POOL_HEADER,
                              LX_GUEST_READ | LX_GUEST_WRITE, &address) ||
        (address & 7))
        return ERROR_INVALID_PARAMETER;
    uint8 header[POOL_HEADER];
    if (!lx_execution_read(GLoaderState.execution, address, header, sizeof header) ||
        word(header, 0) != POOL_MAGIC)
        return ERROR_DOSSUB_CORRUPTED;
    uint32 size = word(header, POOL_SIZE);
    if (size < POOL_HEADER + 8 || (size & 7) ||
        !lx_execution_borrow(GLoaderState.execution, address, size, LX_GUEST_READ | LX_GUEST_WRITE,
                             borrow))
        return ERROR_DOSSUB_CORRUPTED;
    uint32 *lock = (uint32 *)((uint8 *)borrow->host + POOL_LOCK);
    for (unsigned tries = 0; tries < 1000000; tries++)
    {
        uint32 expected = 0;
        if (__atomic_compare_exchange_n(lock, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            if (word(borrow->host, 0) == POOL_MAGIC && word(borrow->host, POOL_SIZE) == size)
                return NO_ERROR;
            __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
            lx_execution_release(borrow);
            return ERROR_DOSSUB_CORRUPTED;
        }
    }
    lx_execution_release(borrow);
    return ERROR_SEM_TIMEOUT;
}
static void close_pool(LxGuestBorrow *borrow)
{
    __atomic_store_n((uint32 *)((uint8 *)borrow->host + POOL_LOCK), 0, __ATOMIC_RELEASE);
    lx_execution_release(borrow);
}
static BOOL free_list_valid(const uint8 *pool)
{
    uint32 size = word(pool, POOL_SIZE), end = POOL_HEADER;
    for (uint32 at = word(pool, POOL_FREE); at; at = word(pool, at))
    {
        if (at < end || (at & 7) || at > size - 8)
            return FALSE;
        uint32 length = word(pool, at + 4);
        if (length < 8 || (length & 7) || length > size - at)
            return FALSE;
        end = at + length;
    }
    return TRUE;
}
static APIRET release_block(uint8 *pool, uint32 offset, uint32 length)
{
    uint32 size = word(pool, POOL_SIZE);
    if (!free_list_valid(pool))
        return ERROR_DOSSUB_CORRUPTED;
    if (offset < POOL_HEADER || (offset & 7) || offset > size || length > size - offset)
        return ERROR_INVALID_PARAMETER;
    uint32 previous = 0, next = word(pool, POOL_FREE);
    while (next && next < offset)
    {
        previous = next;
        next = word(pool, next);
    }
    if ((previous && previous + word(pool, previous + 4) > offset) ||
        (next && offset + length > next))
        return ERROR_DOSSUB_OVERLAP;
    store(pool, offset, next);
    store(pool, offset + 4, length);
    store(pool, previous ? previous : POOL_FREE, offset);
    if (next && offset + length == next)
    {
        store(pool, offset + 4, length + word(pool, next + 4));
        store(pool, offset, word(pool, next));
    }
    if (previous && previous + word(pool, previous + 4) == offset)
    {
        store(pool, previous + 4, word(pool, previous + 4) + word(pool, offset + 4));
        store(pool, previous, word(pool, offset));
    }
    return NO_ERROR;
}
APIRET DosSubSetMem(PVOID base, ULONG flags, ULONG size)
{
    TRACE_NATIVE("DosSubSetMem(%p, %u, %u)", base, (uint)flags, (uint)size);
    uint32 address;
    if ((flags & ~15u) || (flags & 3) == 3 ||
        !lx_execution_address(GLoaderState.execution, base, POOL_HEADER,
                              LX_GUEST_READ | LX_GUEST_WRITE, &address) ||
        (address & 7))
        return ERROR_INVALID_PARAMETER;
    if (!size)
        return ERROR_DOSSUB_NOMEM;
    if (size < POOL_HEADER + 8 || size > UINT32_MAX - 7)
        return ERROR_INVALID_PARAMETER;
    size &= ~7u;
    if (flags & 1)
    {
        LxGuestBorrow pool;
        if (!lx_execution_borrow(GLoaderState.execution, address, size,
                                 LX_GUEST_READ | LX_GUEST_WRITE, &pool))
            return ERROR_INVALID_PARAMETER;
        memset(pool.host, 0, POOL_HEADER);
        store(pool.host, POOL_SIZE, size);
        store(pool.host, POOL_FREE, POOL_HEADER);
        store(pool.host, POOL_OPTIONS, flags & 12);
        store(pool.host, POOL_HEADER + 4, size - POOL_HEADER);
        store(pool.host, POOL_HEADER, 0);
        store(pool.host, 0, POOL_MAGIC);
        lx_execution_release(&pool);
        return NO_ERROR;
    }
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool);
    if (result)
        return result;
    uint32 old_size = word(pool.host, POOL_SIZE);
    if ((flags & 12) != word(pool.host, POOL_OPTIONS))
        result = ERROR_INVALID_PARAMETER;
    else if (flags & 2)
    {
        if (size < old_size)
            result = ERROR_DOSSUB_SHRINK;
        else if (size > old_size)
        {
            LxGuestBorrow expanded;
            if (!lx_execution_borrow(GLoaderState.execution, address, size,
                                     LX_GUEST_READ | LX_GUEST_WRITE, &expanded))
                result = ERROR_INVALID_PARAMETER;
            else
            {
                store(expanded.host, POOL_SIZE, size);
                result = release_block(expanded.host, old_size, size - old_size);
                if (result)
                    store(expanded.host, POOL_SIZE, old_size);
                lx_execution_release(&expanded);
            }
        }
    }
    else if (size != old_size)
        result = ERROR_INVALID_PARAMETER;
    close_pool(&pool);
    return result;
}
APIRET DosSubAllocMem(PVOID base, PPVOID output, ULONG size)
{
    TRACE_NATIVE("DosSubAllocMem(%p, %p, %u)", base, output, (uint)size);
    if (!output || !size || size > UINT32_MAX - 7)
        return ERROR_INVALID_PARAMETER;
    size = (size + 7) & ~7u;
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool);
    if (result)
        return result;
    uint8 *bytes = pool.host;
    result = ERROR_DOSSUB_CORRUPTED;
    if (free_list_valid(bytes))
    {
        result = ERROR_DOSSUB_NOMEM;
        uint32 previous = POOL_FREE;
        for (uint32 at = word(bytes, previous); at; previous = at, at = word(bytes, at))
        {
            uint32 length = word(bytes, at + 4), next = word(bytes, at);
            if (length < size)
                continue;
            if (length > size)
            {
                store(bytes, at + size, next);
                store(bytes, at + size + 4, length - size);
                next = at + size;
            }
            store(bytes, previous, next);
            *output = bytes + at;
            result = NO_ERROR;
            break;
        }
    }
    close_pool(&pool);
    return result;
}
APIRET DosSubFreeMem(PVOID base, PVOID memory, ULONG size)
{
    TRACE_NATIVE("DosSubFreeMem(%p, %p, %u)", base, memory, (uint)size);
    if (!size || size > UINT32_MAX - 7)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool);
    if (result)
        return result;
    uintptr_t first = (uintptr_t)pool.host, pointer = (uintptr_t)memory;
    result = pointer < first || pointer - first > UINT32_MAX
                 ? ERROR_INVALID_PARAMETER
                 : release_block(pool.host, (uint32)(pointer - first), (size + 7) & ~7u);
    close_pool(&pool);
    return result;
}
APIRET DosSubUnsetMem(PVOID base)
{
    TRACE_NATIVE("DosSubUnsetMem(%p)", base);
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool);
    if (!result)
    {
        store(pool.host, 0, 0);
        close_pool(&pool);
    }
    return result;
}
