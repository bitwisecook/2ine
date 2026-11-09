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
    SEMAPHORE_MODIFY_STATE = 2,
    SEMAPHORE_SYNCHRONIZE = 0x100000,
    SEMAPHORE_ALL_ACCESS = 0x1F0003,
    SEMAPHORE_NAME_CAPACITY = 260
};

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
        open32SetError(123);
        return 0;
    }
    Open32Semaphore *sem = calloc(1, sizeof *sem);
    if (!sem)
    {
        open32SetError(8);
        return 0;
    }
    sem->access = access;
    BOOL existing = FALSE;
    ULONG error = 8;
    if (!name)
    {
        if (!valid_counts(initial, maximum))
        {
            error = 87;
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
        uint64 hash = 1469598103934665603ULL;
        const char *scope = getenv("TWOINE_BALL");
        if (!scope)
            scope = getenv("TWOINE_IPC_DIR");
        for (const char *p = scope; p && *p; p++)
            hash = (hash ^ (unsigned char)*p) * 1099511628211ULL;
        hash = (hash ^ 0xFF) * 1099511628211ULL;
        for (const char *p = name; *p; p++)
            hash = (hash ^ (unsigned char)*p) * 1099511628211ULL;
        char identifier[32];
        snprintf(identifier, sizeof identifier, "/2ine-wsem1-%016llx", (unsigned long long)hash);
        /* A last close may race create/open. Retry acquisition without resetting a live count. */
        for (unsigned attempt = 0; attempt < 8; attempt++)
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
                    error = 87;
                    goto failed;
                }
                opened = lx_host_shared_open(identifier, sizeof(SemaphoreState), &sem->shared);
                if (opened == LX_HOST_SHARED_OK)
                    break;
                if (opened == LX_HOST_SHARED_EXISTS)
                    continue;
            }
            error = opened == LX_HOST_SHARED_MISSING ? 2 : opened == LX_HOST_SHARED_NOMEM ? 8 : 5;
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
            uint64 deadline = lx_host_monotonic_us() + 1000000;
            while (!__atomic_load_n(&sem->state->ready, __ATOMIC_ACQUIRE))
            {
                if (lx_host_monotonic_us() >= deadline)
                    goto failed;
                lx_host_sleep_ms(1);
            }
            if (strncmp(sem->state->name, name, sizeof sem->state->name))
            {
                error = 123;
                goto failed;
            }
        }
    }
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_SEMAPHORE, sem, destroy_semaphore);
    if (!handle)
        goto failed;
    if (create)
        open32SetError(existing ? 183 : 0);
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
        if (lx_guest_load_u32(record) != 12)
        {
            open32SetError(87);
            return 0;
        }
        /* The host provider does not expose Windows ACLs or inherited kernel handles. */
        if (lx_guest_load_u32(record + 4) || lx_guest_load_u32(record + 8))
        {
            open32SetError(50);
            return 0;
        }
    }
    return semaphore_handle(name, TRUE, initial, maximum, SEMAPHORE_ALL_ACCESS);
}

ULONG OpenSemaphore(ULONG access, BOOL inherit, PSZ name)
{
    TRACE_NATIVE("OpenSemaphore(%X, %u, '%s')", access, inherit, name);
    if (inherit || (access & ~SEMAPHORE_ALL_ACCESS))
    {
        open32SetError(inherit ? 50 : 87);
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
        open32SetError(6);
        return FALSE;
    }
    Open32Semaphore *sem = borrow.resource;
    ULONG error = 0;
    if (!(sem->access & rights & SEMAPHORE_MODIFY_STATE))
        error = 5;
    else if (count <= 0)
        error = 87;
    else
    {
        if (sem->monitor)
            lx_host_monitor_lock(sem->monitor);
        uint32 old = __atomic_load_n(&sem->state->count, __ATOMIC_ACQUIRE);
        do
        {
            if ((uint32)count > sem->state->maximum - old)
            {
                error = 298; /* ERROR_TOO_MANY_POSTS leaves the count unchanged. */
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
    ULONG result = UINT32_MAX;
    ULONG timeout = call->timeout;
    if (!(sem->access & SEMAPHORE_SYNCHRONIZE))
        open32SetError(5);
    else
    {
        uint64 deadline = lx_host_monotonic_us() + (uint64)timeout * 1000;
        if (sem->monitor)
            lx_host_monitor_lock(sem->monitor);
        for (;;)
        {
            uint32 count = __atomic_load_n(&sem->state->count, __ATOMIC_ACQUIRE);
            if (count && __atomic_compare_exchange_n(&sem->state->count, &count, count - 1, 0,
                                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            {
                result = 0;
                break;
            }
            if (count)
                continue;
            uint64 now = lx_host_monotonic_us();
            if (timeout != UINT32_MAX && now >= deadline)
            {
                result = 258;
                break;
            }
            ULONG remaining =
                timeout == UINT32_MAX ? UINT32_MAX : (ULONG)((deadline - now + 999) / 1000);
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
