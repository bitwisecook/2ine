/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pool-relative free lists remain valid when another process attaches the same memory. */
#include "os2/baseos/doscalls/internal.h"

/* The pool header: a signature, the pool size, the first free block, the
   DOSSUB_SPARSE_OBJ/DOSSUB_SERIALIZE options and a lock word. */
enum
{
    POOL_MAGIC = 0x32425553,
    POOL_SIGNATURE = 0,
    POOL_HEADER = 64,
    POOL_SIZE = 4,
    POOL_FREE = 8,
    POOL_OPTIONS = 12,
    POOL_LOCK = 16
};

/* Blocks are multiples of eight bytes; a free block holds the offset of the next free
   block and its own length. */
enum
{
    BLOCK_GRANULE = 8,
    BLOCK_NEXT = 0,
    BLOCK_LENGTH = 4
};

enum
{
    DOSSUB_VALID_FLAGS = DOSSUB_INIT | DOSSUB_GROW | DOSSUB_SPARSE_OBJ | DOSSUB_SERIALIZE,
    DOSSUB_OPTIONS = DOSSUB_SPARSE_OBJ | DOSSUB_SERIALIZE,
    /* Attempts to take a pool's lock before reporting a timeout. */
    POOL_LOCK_ATTEMPTS = 1000000
};

typedef struct PoolAttachment
{
    uint32 address;
    void *base;
    BOOL active;
    struct PoolAttachment *next;
} PoolAttachment;

static PoolAttachment *PoolAttachments;

static PoolAttachment *pool_attachment(uint32 address, void *base, BOOL create)
{
    for (PoolAttachment *p = PoolAttachments; p; p = p->next)
        if (p->address == address && p->base == base)
            return p;
    if (!create)
        return NULL;
    PoolAttachment *p = calloc(1, sizeof *p);
    if (p)
    {
        p->address = address;
        p->base = base;
        p->next = PoolAttachments;
        PoolAttachments = p;
    }
    return p;
}

void cleanupSuballocation(void)
{
    while (PoolAttachments)
    {
        PoolAttachment *next = PoolAttachments->next;
        free(PoolAttachments);
        PoolAttachments = next;
    }
}

static uint32 word(const uint8 *pool, uint32 offset)
{
    return lx_guest_load_u32(pool + offset);
}

static void store(uint8 *pool, uint32 offset, uint32 value)
{
    lx_guest_store_u32(pool + offset, value);
}

static APIRET open_pool(void *base, LxGuestBorrow *borrow, BOOL attached)
{
    uint32 address;
    if (!lx_execution_address(GProcessState.execution, base, POOL_HEADER,
                              LX_GUEST_READ | LX_GUEST_WRITE, &address) ||
        (address & (BLOCK_GRANULE - 1)))
        return ERROR_INVALID_PARAMETER;
    uint8 header[POOL_HEADER];
    if (!lx_execution_read(GProcessState.execution, address, header, sizeof header) ||
        word(header, POOL_SIGNATURE) != POOL_MAGIC)
        return ERROR_DOSSUB_CORRUPTED;
    uint32 size = word(header, POOL_SIZE);
    if (size < POOL_HEADER + BLOCK_GRANULE || (size & (BLOCK_GRANULE - 1)) ||
        !lx_execution_borrow(GProcessState.execution, address, size, LX_GUEST_READ | LX_GUEST_WRITE,
                             borrow))
        return ERROR_DOSSUB_CORRUPTED;
    uint32 *lock = (uint32 *)((uint8 *)borrow->host + POOL_LOCK);
    for (unsigned tries = 0; tries < POOL_LOCK_ATTEMPTS; tries++)
    {
        uint32 expected = 0;
        if (__atomic_compare_exchange_n(lock, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            grabLock(&GMutexDosCalls);
            PoolAttachment *attachment = pool_attachment(address, base, FALSE);
            BOOL active = attachment && attachment->active;
            ungrabLock(&GMutexDosCalls);
            if (word(borrow->host, POOL_SIGNATURE) == POOL_MAGIC &&
                word(borrow->host, POOL_SIZE) == size && (!attached || active))
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
    for (uint32 at = word(pool, POOL_FREE); at; at = word(pool, at + BLOCK_NEXT))
    {
        if (at < end || (at & (BLOCK_GRANULE - 1)) || at > size - BLOCK_GRANULE)
            return FALSE;
        uint32 length = word(pool, at + BLOCK_LENGTH);
        if (length < BLOCK_GRANULE || (length & (BLOCK_GRANULE - 1)) || length > size - at)
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
    if (offset < POOL_HEADER || (offset & (BLOCK_GRANULE - 1)) || offset > size ||
        length > size - offset)
        return ERROR_INVALID_PARAMETER;
    uint32 previous = 0, next = word(pool, POOL_FREE);
    while (next && next < offset)
    {
        previous = next;
        next = word(pool, next + BLOCK_NEXT);
    }
    if ((previous && previous + word(pool, previous + BLOCK_LENGTH) > offset) ||
        (next && offset + length > next))
        return ERROR_DOSSUB_OVERLAP;
    store(pool, offset + BLOCK_NEXT, next);
    store(pool, offset + BLOCK_LENGTH, length);
    store(pool, previous ? previous + BLOCK_NEXT : POOL_FREE, offset);
    /* Merge with adjacent free blocks so the list never holds touching blocks. */
    if (next && offset + length == next)
    {
        store(pool, offset + BLOCK_LENGTH, length + word(pool, next + BLOCK_LENGTH));
        store(pool, offset + BLOCK_NEXT, word(pool, next + BLOCK_NEXT));
    }
    if (previous && previous + word(pool, previous + BLOCK_LENGTH) == offset)
    {
        store(pool, previous + BLOCK_LENGTH,
              word(pool, previous + BLOCK_LENGTH) + word(pool, offset + BLOCK_LENGTH));
        store(pool, previous + BLOCK_NEXT, word(pool, offset + BLOCK_NEXT));
    }
    return NO_ERROR;
}

static APIRET set_pool(PVOID base, ULONG flags, ULONG size)
{
    TRACE_NATIVE("DosSubSetMem(%p, %u, %u)", base, (uint)flags, (uint)size);
    uint32 address;
    if ((flags & ~(ULONG)DOSSUB_VALID_FLAGS) ||
        (flags & (DOSSUB_INIT | DOSSUB_GROW)) == (DOSSUB_INIT | DOSSUB_GROW) ||
        !lx_execution_address(GProcessState.execution, base, POOL_HEADER,
                              LX_GUEST_READ | LX_GUEST_WRITE, &address) ||
        (address & (BLOCK_GRANULE - 1)))
        return ERROR_INVALID_PARAMETER;
    if (!size)
        return ERROR_DOSSUB_NOMEM;
    if (size < POOL_HEADER + BLOCK_GRANULE || size > UINT32_MAX - (BLOCK_GRANULE - 1))
        return ERROR_INVALID_PARAMETER;
    size &= ~(BLOCK_GRANULE - 1u);
    grabLock(&GMutexDosCalls);
    PoolAttachment *attachment = pool_attachment(address, base, TRUE);
    ungrabLock(&GMutexDosCalls);
    if (!attachment)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (flags & DOSSUB_INIT)
    {
        LxGuestBorrow pool;
        if (!lx_execution_borrow(GProcessState.execution, address, size,
                                 LX_GUEST_READ | LX_GUEST_WRITE, &pool))
            return ERROR_INVALID_PARAMETER;
        memset(pool.host, 0, POOL_HEADER);
        store(pool.host, POOL_SIZE, size);
        store(pool.host, POOL_FREE, POOL_HEADER);
        store(pool.host, POOL_OPTIONS, flags & DOSSUB_OPTIONS);
        store(pool.host, POOL_HEADER + BLOCK_LENGTH, size - POOL_HEADER);
        store(pool.host, POOL_HEADER + BLOCK_NEXT, 0);
        store(pool.host, POOL_SIGNATURE, POOL_MAGIC);
        grabLock(&GMutexDosCalls);
        attachment->active = TRUE;
        ungrabLock(&GMutexDosCalls);
        lx_execution_release(&pool);
        return NO_ERROR;
    }
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool, FALSE);
    if (result)
        return result;
    uint32 old_size = word(pool.host, POOL_SIZE);
    if ((flags & DOSSUB_OPTIONS) != word(pool.host, POOL_OPTIONS))
        result = ERROR_INVALID_PARAMETER;
    else if (flags & DOSSUB_GROW)
    {
        if (size < old_size)
            result = ERROR_DOSSUB_SHRINK;
        else if (size > old_size)
        {
            LxGuestBorrow expanded;
            if (!lx_execution_borrow(GProcessState.execution, address, size,
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
    if (!result)
    {
        grabLock(&GMutexDosCalls);
        attachment->active = TRUE;
        ungrabLock(&GMutexDosCalls);
    }
    close_pool(&pool);
    return result;
}

APIRET DosSubSetMem(PVOID base, ULONG flags, ULONG size)
{
    APIRET result = set_pool(base, flags, size);
    if (getenv("TWOINE_TRACE_RET"))
        fprintf(stderr, "2INE RET: DosSubSetMem(%p, %u, %u) -> %u\n", base, (uint)flags, (uint)size,
                (uint)result);
    return result;
}

APIRET DosSubAllocMem(PVOID base, PPVOID output, ULONG size)
{
    TRACE_NATIVE("DosSubAllocMem(%p, %p, %u)", base, output, (uint)size);
    if (!output || !size || size > UINT32_MAX - (BLOCK_GRANULE - 1))
        return ERROR_INVALID_PARAMETER;
    size = (size + BLOCK_GRANULE - 1) & ~(BLOCK_GRANULE - 1u);
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool, TRUE);
    if (result)
        return result;
    uint8 *bytes = pool.host;
    result = ERROR_DOSSUB_CORRUPTED;
    if (free_list_valid(bytes))
    {
        result = ERROR_DOSSUB_NOMEM;
        uint32 previous = POOL_FREE;
        for (uint32 at = word(bytes, previous); at;
             previous = at + BLOCK_NEXT, at = word(bytes, at + BLOCK_NEXT))
        {
            uint32 length = word(bytes, at + BLOCK_LENGTH), next = word(bytes, at + BLOCK_NEXT);
            if (length < size)
                continue;
            if (length > size)
            {
                store(bytes, at + size + BLOCK_NEXT, next);
                store(bytes, at + size + BLOCK_LENGTH, length - size);
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
    if (!size || size > UINT32_MAX - (BLOCK_GRANULE - 1))
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool, TRUE);
    if (result)
        return result;
    uintptr_t first = (uintptr_t)pool.host, pointer = (uintptr_t)memory;
    result = pointer < first || pointer - first > UINT32_MAX
                 ? ERROR_INVALID_PARAMETER
                 : release_block(pool.host, (uint32)(pointer - first),
                                 (size + BLOCK_GRANULE - 1) & ~(BLOCK_GRANULE - 1u));
    close_pool(&pool);
    return result;
}

APIRET DosSubUnsetMem(PVOID base)
{
    TRACE_NATIVE("DosSubUnsetMem(%p)", base);
    LxGuestBorrow pool;
    APIRET result = open_pool(base, &pool, TRUE);
    if (!result)
    {
        uint32 address;
        if (!lx_execution_address(GProcessState.execution, base, POOL_HEADER, 0, &address))
            result = ERROR_INVALID_PARAMETER;
        else
        {
            grabLock(&GMutexDosCalls);
            PoolAttachment *attachment = pool_attachment(address, base, FALSE);
            if (attachment)
                attachment->active = FALSE;
            ungrabLock(&GMutexDosCalls);
        }
        close_pool(&pool);
    }
    return result;
}
