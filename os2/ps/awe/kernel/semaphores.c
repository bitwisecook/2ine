/* Public domain. No rights reserved. Wholly AI-authored. */
/* Counting semaphores retain typed handles; named counts are shared within a ball. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "host/clock.h"
#include "host/shared_memory.h"
#include "host/sync.h"
#include "host/thread.h"

enum
{
    SEMAPHORE_NAME_CAPACITY = 260,
    /* Separates the scope from the name in the digest; never a byte of a name. */
    HASH_SEPARATOR = 0xFF,
    /* Retries when a last close races another process's create or open. */
    SHARED_OPEN_ATTEMPTS = 8,
    /* How long an opener waits for the creator to publish a named semaphore. */
    CREATOR_PUBLISH_WAIT_US = 1000000,
    US_PER_MS = 1000
};

static const uint64 FNV_OFFSET_BASIS = 1469598103934665603ULL, FNV_PRIME = 1099511628211ULL;

typedef struct SemaphoreState
{
    uint32 count, maximum, ready;
    char name[SEMAPHORE_NAME_CAPACITY];
} SemaphoreState;

typedef struct Open32Semaphore
{
    LxHostMonitor *monitor;
    LxHostShared *shared;
    SemaphoreState *state;
    SemaphoreState local;
    ULONG access;
} Open32Semaphore;

static void destroy_semaphore(void *resource)
{
    Open32Semaphore *sem = resource;
    if (sem->shared)
    {
        if (sem->state)
            lx_host_shared_unmap(sem->state, sizeof *sem->state);
        lx_host_shared_close(sem->shared);
    }
    if (sem->monitor)
        lx_host_monitor_destroy(sem->monitor);
    free(sem);
}

static BOOL valid_counts(LONG initial, LONG maximum)
{
    return initial >= 0 && maximum > 0 && initial <= maximum;
}

static ULONG semaphore_handle(PSZ name, BOOL create, LONG initial, LONG maximum, ULONG access)
{
    if ((!create && !name) || (name && (!*name || strlen(name) >= SEMAPHORE_NAME_CAPACITY)))
    {
        open32SetError(ERROR_INVALID_NAME);
        return 0;
    }
    Open32Semaphore *sem = calloc(1, sizeof *sem);
    if (!sem)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    sem->access = access;
    BOOL existing = FALSE;
    ULONG error = ERROR_NOT_ENOUGH_MEMORY;
    if (!name)
    {
        if (!valid_counts(initial, maximum))
        {
            error = ERROR_INVALID_PARAMETER;
            goto failed;
        }
        sem->monitor = lx_host_monitor_create();
        if (!sem->monitor)
            goto failed;
        sem->state = &sem->local;
        sem->state->count = (uint32)initial;
        sem->state->maximum = (uint32)maximum;
    }
    else
    {
        /* FNV-1a combines the ball scope and case-sensitive Windows object name.
           The original name is also retained to reject a digest collision. */
        uint64 hash = FNV_OFFSET_BASIS;
        const char *scope = getenv("TWOINE_BALL");
        if (!scope)
            scope = getenv("TWOINE_IPC_DIR");
        for (const char *p = scope; p && *p; p++)
            hash = (hash ^ (unsigned char)*p) * FNV_PRIME;
        hash = (hash ^ HASH_SEPARATOR) * FNV_PRIME;
        for (const char *p = name; *p; p++)
            hash = (hash ^ (unsigned char)*p) * FNV_PRIME;
        char identifier[32];
        snprintf(identifier, sizeof identifier, "/2ine-wsem1-%016llx", (unsigned long long)hash);
        /* A last close may race create/open. Retry acquisition without resetting a live count. */
        for (unsigned attempt = 0; attempt < SHARED_OPEN_ATTEMPTS; attempt++)
        {
            LxHostSharedResult opened = lx_host_shared_open(identifier, 0, &sem->shared);
            if (opened == LX_HOST_SHARED_OK)
            {
                existing = TRUE;
                break;
            }
            if (opened == LX_HOST_SHARED_MISSING && create)
            {
                if (!valid_counts(initial, maximum))
                {
                    error = ERROR_INVALID_PARAMETER;
                    goto failed;
                }
                opened = lx_host_shared_open(identifier, sizeof(SemaphoreState), &sem->shared);
                if (opened == LX_HOST_SHARED_OK)
                    break;
                if (opened == LX_HOST_SHARED_EXISTS)
                    continue;
            }
            error = opened == LX_HOST_SHARED_MISSING ? ERROR_FILE_NOT_FOUND
                    : opened == LX_HOST_SHARED_NOMEM ? ERROR_NOT_ENOUGH_MEMORY
                                                     : ERROR_ACCESS_DENIED;
            goto failed;
        }
        if (!sem->shared || lx_host_shared_size(sem->shared) != sizeof(SemaphoreState) ||
            !(sem->state = lx_host_shared_map(sem->shared, NULL, 0)))
            goto failed;
        if (!existing)
        {
            sem->state->count = (uint32)initial;
            sem->state->maximum = (uint32)maximum;
            memcpy(sem->state->name, name, strlen(name) + 1);
            __atomic_store_n(&sem->state->ready, 1, __ATOMIC_RELEASE);
        }
        else
        {
            uint64 deadline = lx_host_monotonic_us() + CREATOR_PUBLISH_WAIT_US;
            while (!__atomic_load_n(&sem->state->ready, __ATOMIC_ACQUIRE))
            {
                if (lx_host_monotonic_us() >= deadline)
                    goto failed;
                lx_host_sleep_ms(1);
            }
            if (strncmp(sem->state->name, name, sizeof sem->state->name))
            {
                error = ERROR_INVALID_NAME;
                goto failed;
            }
        }
    }
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_SEMAPHORE, sem, destroy_semaphore);
    if (!handle)
        goto failed;
    if (create)
        open32SetError(existing ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return handle;

failed:
    destroy_semaphore(sem);
    open32SetError(error);
    return 0;
}

ULONG CreateSemaphore(PVOID security, LONG initial, LONG maximum, PSZ name)
{
    TRACE_NATIVE("CreateSemaphore(%p, %d, %d, '%s')", security, initial, maximum, name);
    if (security)
    {
        const uint8 *record = security;
        if (lx_guest_load_u32(record + WIN32_SECURITY_ATTRIBUTES_LENGTH) !=
            WIN32_SECURITY_ATTRIBUTES_SIZE)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        /* The host provider does not expose Windows ACLs or inherited kernel handles. */
        if (lx_guest_load_u32(record + WIN32_SECURITY_ATTRIBUTES_SECURITY_DESCRIPTOR) ||
            lx_guest_load_u32(record + WIN32_SECURITY_ATTRIBUTES_INHERIT_HANDLE))
        {
            open32SetError(ERROR_NOT_SUPPORTED);
            return 0;
        }
    }
    return semaphore_handle(name, TRUE, initial, maximum, WIN32_SEMAPHORE_ALL_ACCESS);
}

ULONG OpenSemaphore(ULONG access, BOOL inherit, PSZ name)
{
    TRACE_NATIVE("OpenSemaphore(%X, %u, '%s')", access, inherit, name);
    if (inherit || (access & ~WIN32_SEMAPHORE_ALL_ACCESS))
    {
        open32SetError(inherit ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return 0;
    }
    return semaphore_handle(name, FALSE, 0, 0, access);
}

BOOL ReleaseSemaphore(ULONG handle, LONG count, PLONG previous)
{
    TRACE_NATIVE("ReleaseSemaphore(%u, %d, %p)", handle, count, previous);
    LxHandleBorrow borrow = {0};
    uint32_t rights = 0;
    if (!lx_handle_access(handle, LX_HANDLE_OPEN32_SEMAPHORE, &rights) ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_SEMAPHORE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Open32Semaphore *sem = borrow.resource;
    ULONG error = 0;
    if (!(sem->access & rights & WIN32_SEMAPHORE_MODIFY_STATE))
        error = ERROR_ACCESS_DENIED;
    else if (count <= 0)
        error = ERROR_INVALID_PARAMETER;
    else
    {
        if (sem->monitor)
            lx_host_monitor_lock(sem->monitor);
        uint32 old = __atomic_load_n(&sem->state->count, __ATOMIC_ACQUIRE);
        do
        {
            if ((uint32)count > sem->state->maximum - old)
            {
                /* The count is left unchanged. */
                error = ERROR_TOO_MANY_POSTS;
                break;
            }
        } while (!__atomic_compare_exchange_n(&sem->state->count, &old, old + (uint32)count, 0,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
        if (!error && previous)
            *previous = (LONG)old;
        if (sem->monitor)
        {
            if (!error)
                lx_host_monitor_wake_all(sem->monitor);
            lx_host_monitor_unlock(sem->monitor);
        }
    }
    lx_handle_release(&borrow);
    if (error)
        open32SetError(error);
    return error == 0;
}

typedef struct SemaphoreWait
{
    LxHandleBorrow borrow;
    ULONG timeout;
} SemaphoreWait;

static uint32 wait_semaphore(void *argument)
{
    SemaphoreWait *call = argument;
    Open32Semaphore *sem = call->borrow.resource;
    ULONG result = WIN32_WAIT_FAILED;
    ULONG timeout = call->timeout;
    if (!(sem->access & WIN32_SYNCHRONIZE))
        open32SetError(ERROR_ACCESS_DENIED);
    else
    {
        uint64 deadline = lx_host_monotonic_us() + (uint64)timeout * US_PER_MS;
        if (sem->monitor)
            lx_host_monitor_lock(sem->monitor);
        for (;;)
        {
            uint32 count = __atomic_load_n(&sem->state->count, __ATOMIC_ACQUIRE);
            if (count && __atomic_compare_exchange_n(&sem->state->count, &count, count - 1, 0,
                                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            {
                result = WIN32_WAIT_OBJECT_0;
                break;
            }
            if (count)
                continue;
            uint64 now = lx_host_monotonic_us();
            if (timeout != WIN32_INFINITE && now >= deadline)
            {
                result = WIN32_WAIT_TIMEOUT;
                break;
            }
            ULONG remaining = timeout == WIN32_INFINITE
                                  ? UINT32_MAX
                                  : (ULONG)((deadline - now + US_PER_MS - 1) / US_PER_MS);
            if (sem->monitor)
                lx_host_monitor_wait(sem->monitor, remaining);
            else
                lx_host_sleep_ms(1);
        }
        if (sem->monitor)
            lx_host_monitor_unlock(sem->monitor);
    }
    return result;
}

static void release_wait(void *argument)
{
    SemaphoreWait *call = argument;
    lx_handle_release(&call->borrow);
}

int open32WaitSemaphore(ULONG handle, ULONG timeout, ULONG *result)
{
    SemaphoreWait call = {.timeout = timeout};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_SEMAPHORE, &call.borrow))
        return 0;
    *result = lx_host_guarded_call(wait_semaphore, &call, release_wait, &call);
    return 1;
}

BOOL open32CloseSemaphore(ULONG handle)
{
    return lx_handle_close(handle, LX_HANDLE_OPEN32_SEMAPHORE);
}
