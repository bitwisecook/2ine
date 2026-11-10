/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Event semaphores, private and named; named events live in host shared memory. */
#include "os2/baseos/doscalls/internal.h"

typedef struct EventReference
{
    struct EventReference *next;
    uint32 public_handle, internal_handle;
    size_t opens;
} EventReference;

static EventReference *event_references;

static LxHostOnce event_pool_once = LX_HOST_ONCE_INIT;

static LxHostShared *event_pool;

static uint32 *event_sequence;

/* A named event's shared state: the post count, a ready flag the creator sets last,
   and the public handle every opener reports. */
enum
{
    SHARED_EVENT_POSTED,
    SHARED_EVENT_READY,
    SHARED_EVENT_HANDLE,
    SHARED_EVENT_SIZE = 12
};

/* The ball-wide pool: the last allocated public handle number and a ready flag. */
enum
{
    EVENT_POOL_SEQUENCE,
    EVENT_POOL_READY,
    EVENT_POOL_SIZE = 8
};

/* Public handles of named events carry this bit, apart from private handles. */
#define SHARED_EVENT_HANDLE_BIT 0x80000000u

enum
{
    /* Openers give up on a creator that has not finished initialising by then. */
    SHARED_READY_TIMEOUT_US = 1000000,
    EVENT_POST_LIMIT = 0xFFFF
};

uint64 eventScope(void)
{
    uint64 hash = FNV1A_64_OFFSET_BASIS;
    const char *scope = getenv("TWOINE_BALL");
    if (!scope)
        scope = getenv("TWOINE_IPC_DIR");
    for (const char *p = scope; p && *p; p++)
        hash = (hash ^ (unsigned char)*p) * FNV1A_64_PRIME;
    return (hash ^ FNV_SCOPE_SEPARATOR) * FNV1A_64_PRIME;
}

static void initializeEventPool(void)
{
    char name[64];
    /* Host shared-memory names also fit Darwin's 31-character limit. */
    snprintf(name, sizeof name, "/2ine-epool4-%016llx", (unsigned long long)eventScope());
    LxHostSharedResult result = lx_host_shared_open(name, EVENT_POOL_SIZE, &event_pool);
    int created = result == LX_HOST_SHARED_OK;
    if (result == LX_HOST_SHARED_EXISTS)
        result = lx_host_shared_open(name, 0, &event_pool);
    if (result != LX_HOST_SHARED_OK || lx_host_shared_size(event_pool) != EVENT_POOL_SIZE ||
        !(event_sequence = lx_host_shared_map(event_pool, NULL, 0)))
        return;
    if (created)
        __atomic_store_n(&event_sequence[EVENT_POOL_READY], 1, __ATOMIC_RELEASE);
    else
    {
        uint64 deadline = lx_host_monotonic_us() + SHARED_READY_TIMEOUT_US;
        while (!__atomic_load_n(&event_sequence[EVENT_POOL_READY], __ATOMIC_ACQUIRE))
        {
            if (lx_host_monotonic_us() >= deadline)
            {
                event_sequence = NULL;
                return;
            }
            lx_host_sleep_ms(1);
        }
    }
}

int borrowEventResource(HEV handle, LxHandleBorrow *borrow)
{
    if (!(handle & SHARED_EVENT_HANDLE_BIT))
        return lx_handle_borrow(handle, LX_HANDLE_EVENT, borrow);
    grabLock(&GMutexDosCalls);
    uint32 internal = 0;
    for (EventReference *r = event_references; r; r = r->next)
        if (r->public_handle == handle)
        {
            internal = r->internal_handle;
            break;
        }
    int result = internal && lx_handle_borrow(internal, LX_HANDLE_EVENT, borrow);
    ungrabLock(&GMutexDosCalls);
    return result;
}

void destroyEventResource(void *value)
{
    EventSem *sem = value;
    remove_sharing(value);
    if (sem->monitor)
        lx_host_monitor_destroy(sem->monitor);
    if (sem->shared_state)
        lx_host_shared_unmap(sem->shared_state, SHARED_EVENT_SIZE);
    if (sem->shared)
        lx_host_shared_close(sem->shared);
    free(sem);
}

/* Named events use a ball-scoped shared state and live-handle references.
   Waits poll at one millisecond; private events retain condition-variable waits. */
static APIRET namedEvent(const char *name, PHEV output, int create, BOOL32 state)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    if (!name || strncasecmp(name, SEM32_PREFIX, SEM32_PREFIX_LENGTH) ||
        strlen(name) > SEMAPHORE_NAME_MAXIMUM || !name[SEM32_PREFIX_LENGTH])
        return ERROR_INVALID_NAME;
    lx_host_once(&event_pool_once, initializeEventPool);
    if (!event_sequence)
        return ERROR_NOT_ENOUGH_MEMORY;
    uint64 hash = eventScope();
    for (const char *p = name; *p; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        hash = (hash ^ c) * FNV1A_64_PRIME;
    }
    char identifier[64];
    snprintf(identifier, sizeof identifier, "/2ine-event4-%016llx", (unsigned long long)hash);
    EventSem *sem = calloc(1, sizeof *sem);
    if (!sem)
        return ERROR_NOT_ENOUGH_MEMORY;
    LxHostSharedResult opened =
        lx_host_shared_open(identifier, create ? SHARED_EVENT_SIZE : 0, &sem->shared);
    APIRET result = opened == LX_HOST_SHARED_EXISTS    ? ERROR_DUPLICATE_NAME
                    : opened == LX_HOST_SHARED_MISSING ? ERROR_SEM_NOT_FOUND
                    : opened == LX_HOST_SHARED_NOMEM   ? ERROR_NOT_ENOUGH_MEMORY
                                                       : ERROR_ACCESS_DENIED;
    if (opened != LX_HOST_SHARED_OK)
        goto fail;
    if (lx_host_shared_size(sem->shared) != SHARED_EVENT_SIZE ||
        !(sem->shared_state = lx_host_shared_map(sem->shared, NULL, 0)))
        goto fail;
    if (create)
    {
        uint32 id = __atomic_add_fetch(&event_sequence[EVENT_POOL_SEQUENCE], 1, __ATOMIC_ACQ_REL);
        if (!id || id >= SHARED_EVENT_HANDLE_BIT)
            goto fail;
        sem->shared_state[SHARED_EVENT_HANDLE] = id | SHARED_EVENT_HANDLE_BIT;
        __atomic_store_n(&sem->shared_state[SHARED_EVENT_POSTED], state != 0, __ATOMIC_RELAXED);
        __atomic_store_n(&sem->shared_state[SHARED_EVENT_READY], 1, __ATOMIC_RELEASE);
    }
    else
    {
        uint64 deadline = lx_host_monotonic_us() + SHARED_READY_TIMEOUT_US;
        while (!__atomic_load_n(&sem->shared_state[SHARED_EVENT_READY], __ATOMIC_ACQUIRE))
        {
            if (lx_host_monotonic_us() >= deadline)
                goto fail;
            lx_host_sleep_ms(1);
        }
    }
    uint32 handle = sem->shared_state[SHARED_EVENT_HANDLE];
    grabLock(&GMutexDosCalls);
    for (EventReference *r = event_references; r; r = r->next)
        if (r->public_handle == handle)
        {
            r->opens++;
            ungrabLock(&GMutexDosCalls);
            destroyEventResource(sem);
            *output = handle;
            return NO_ERROR;
        }
    EventReference *reference = calloc(1, sizeof *reference);
    uint32 internal = reference ? lx_handle_create(LX_HANDLE_EVENT, sem, destroyEventResource) : 0;
    if (!internal)
    {
        free(reference);
        ungrabLock(&GMutexDosCalls);
        result = ERROR_NOT_ENOUGH_MEMORY;
        goto fail;
    }
    *reference = (EventReference){event_references, handle, internal, 1};
    event_references = reference;
    ungrabLock(&GMutexDosCalls);
    *output = handle;
    return NO_ERROR;
fail:
    destroyEventResource(sem);
    return result;
}

APIRET DosCreateEventSem(PSZ name, PHEV output, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateEventSem('%s', %p, %u, %u)", name, output, (uint)attr, (uint)state);
    if (!output || (attr & ~(ULONG)DC_SEM_SHARED))
        return ERROR_INVALID_PARAMETER;
    if (name && *name)
        return namedEvent(name, output, 1, state);
    EventSem *sem = calloc(1, sizeof *sem);
    if (!sem)
        return ERROR_NOT_ENOUGH_MEMORY;
    sem->monitor = lx_host_monitor_create();
    if (!sem->monitor)
    {
        free(sem);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    sem->posted = state != 0;
    if (!set_sharing(sem, (attr & DC_SEM_SHARED) || name))
    {
        destroyEventResource(sem);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return publishSemaphore(sem, LX_HANDLE_EVENT, name, output);
}

APIRET DosOpenEventSem(PSZ name, PHEV output)
{
    TRACE_NATIVE("DosOpenEventSem('%s', %p)", name, output);
    if (name && *name)
        return namedEvent(name, output, 0, 0);
    return openSemaphore(name, LX_HANDLE_EVENT, output);
}

APIRET DosResetEventSemResource(HEV hev, PULONG pulPostCt, EventSem *sem)
{
    TRACE_NATIVE("DosResetEventSem(%u, %p)", (uint)hev, pulPostCt);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    APIRET retval = NO_ERROR;
    ULONG count = 0;

    if (sem->shared_state)
    {
        count = __atomic_exchange_n(&sem->shared_state[SHARED_EVENT_POSTED], 0, __ATOMIC_ACQ_REL);
        if (pulPostCt)
            *pulPostCt = count;
        return count ? NO_ERROR : ERROR_ALREADY_RESET;
    }

    lx_host_monitor_lock(sem->monitor);
    count = sem->posted;
    if (!sem->posted)
        retval = ERROR_ALREADY_RESET;
    else
        sem->posted = 0;
    lx_host_monitor_unlock(sem->monitor);

    if (pulPostCt)
        *pulPostCt = count;

    return retval;
}

APIRET DosResetEventSem(HEV hev, PULONG pulPostCt)
{
    LxHandleBorrow object;
    if (!borrowEventResource(hev, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_RESET_EVENT, hev, 0, &object, NULL, pulPostCt);
    return result;
}

APIRET DosPostEventSemResource(HEV hev, EventSem *sem)
{
    TRACE_NATIVE("DosPostEventSem(%u)", (uint)hev);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    APIRET retval = NO_ERROR;

    if (sem->shared_state)
    {
        uint32 count = __atomic_load_n(&sem->shared_state[SHARED_EVENT_POSTED], __ATOMIC_ACQUIRE);
        for (;;)
        {
            if (count == EVENT_POST_LIMIT)
                return ERROR_TOO_MANY_POSTS;
            if (__atomic_compare_exchange_n(&sem->shared_state[SHARED_EVENT_POSTED], &count,
                                            count + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return count ? ERROR_ALREADY_POSTED : NO_ERROR;
        }
    }

    lx_host_monitor_lock(sem->monitor);
    if (sem->posted)
        retval = ERROR_ALREADY_POSTED;
    if (sem->posted == EVENT_POST_LIMIT)
        retval = ERROR_TOO_MANY_POSTS;
    else
    {
        sem->posted++;
        lx_host_monitor_wake_all(sem->monitor);
    }
    lx_host_monitor_unlock(sem->monitor);
    if (!retval)
        notifyMuxWaiters();

    return retval;
}

APIRET DosPostEventSem(HEV hev)
{
    LxHandleBorrow object;
    if (!borrowEventResource(hev, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_POST_EVENT, hev, 0, &object, NULL, NULL);
    return result;
}

APIRET DosWaitEventSemResource(HEV hev, ULONG timeout, EventSem *sem)
{
    TRACE_NATIVE("DosWaitEventSem(%u, %u)", (uint)hev, (uint)timeout);
    const uint64 deadline =
        timeout == UINT32_MAX ? UINT64_MAX : lx_host_monotonic_us() + (uint64)timeout * US_PER_MS;
    APIRET result = NO_ERROR;
    if (sem->shared_state)
    {
        while (!__atomic_load_n(&sem->shared_state[SHARED_EVENT_POSTED], __ATOMIC_ACQUIRE))
        {
            if (!timeout || lx_host_monotonic_us() >= deadline)
                return ERROR_TIMEOUT;
            lx_host_sleep_ms(1);
        }
        return NO_ERROR;
    }
    lx_host_monitor_lock(sem->monitor);
    while (!sem->posted)
    {
        const uint64 now = lx_host_monotonic_us();
        if (!timeout || now >= deadline)
        {
            result = ERROR_TIMEOUT;
            break;
        }
        uint32 wait = UINT32_MAX;
        if (deadline != UINT64_MAX)
            wait = (uint32)((deadline - now + US_PER_MS - 1) / US_PER_MS);
        LxHostLockResult rc = lx_host_monitor_wait(sem->monitor, wait);
        if (rc == LX_HOST_LOCK_FAILED)
        {
            result = ERROR_INVALID_HANDLE;
            break;
        }
    }
    lx_host_monitor_unlock(sem->monitor);
    return result;
}

APIRET DosWaitEventSem(HEV hev, ULONG timeout)
{
    LxHandleBorrow object;
    if (!borrowEventResource(hev, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_WAIT_EVENT, hev, timeout, &object, NULL, NULL);
    return result;
}

APIRET DosQueryEventSemResource(HEV hev, PULONG pulPostCt, EventSem *sem)
{
    TRACE_NATIVE("DosQueryEventSem(%u, %p)", (uint)hev, pulPostCt);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    ULONG count;
    if (sem->shared_state)
        count = __atomic_load_n(&sem->shared_state[SHARED_EVENT_POSTED], __ATOMIC_ACQUIRE);
    else
    {
        lx_host_monitor_lock(sem->monitor);
        count = (ULONG)sem->posted;
        lx_host_monitor_unlock(sem->monitor);
    }

    if (pulPostCt)
        *pulPostCt = count;

    return NO_ERROR;
}

APIRET DosQueryEventSem(HEV hev, PULONG pulPostCt)
{
    LxHandleBorrow object;
    if (!borrowEventResource(hev, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_QUERY_EVENT, hev, 0, &object, NULL, pulPostCt);
    return result;
}

APIRET DosCloseEventSem(HEV handle)
{
    TRACE_NATIVE("DosCloseEventSem(%u)", (uint)handle);
    if (handle & SHARED_EVENT_HANDLE_BIT)
    {
        grabLock(&GMutexDosCalls);
        for (EventReference **link = &event_references; *link; link = &(*link)->next)
            if ((*link)->public_handle == handle)
            {
                EventReference *r = *link;
                if (!--r->opens)
                {
                    *link = r->next;
                    lx_handle_close(r->internal_handle, LX_HANDLE_EVENT);
                    free(r);
                }
                ungrabLock(&GMutexDosCalls);
                return NO_ERROR;
            }
        ungrabLock(&GMutexDosCalls);
        return ERROR_INVALID_HANDLE;
    }
    LxHandleBorrow object;
    if (!lx_handle_borrow(handle, LX_HANDLE_EVENT, &object))
        return ERROR_INVALID_HANDLE;
    grabLock(&GMutexDosCalls);
    int closed = lx_handle_close(handle, LX_HANDLE_EVENT);
    if (closed)
        retireSemaphoreName(object.resource, LX_HANDLE_EVENT);
    ungrabLock(&GMutexDosCalls);
    lx_handle_release(&object);
    return closed ? NO_ERROR : ERROR_INVALID_HANDLE;
}
