/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
void destroyEventResource(void *value)
{
    EventSem *sem = value;
    lx_host_monitor_destroy(sem->monitor);
    free(sem);
}

void destroyMutexResource(void *value)
{
    LxHostMutex *mutex = value;
    /* An unpublished, initially-owned mutex can arrive here on allocation failure. */
    if (lx_host_mutex_busy(mutex))
        while (lx_host_mutex_unlock(mutex) == LX_HOST_LOCK_OK)
        {
        }
    lx_host_mutex_destroy(mutex);
}

NamedSem *findNamedSemLocked(const char *name, int is_mutex)
{
    for (NamedSem *sem = GNamedSems; sem; sem = sem->next)
        if (sem->is_mutex == is_mutex && !strcasecmp(sem->name, name))
            return sem;
    return NULL;
}

APIRET publishSemaphore(void *value, LxHandleKind kind, const char *name, uint32 *output)
{
    void (*destroy)(void *) = kind == LX_HANDLE_MUTEX ? destroyMutexResource : destroyEventResource;
    uint32 public_handle = lx_handle_create(kind, value, destroy);
    if (!public_handle)
    {
        destroy(value);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    APIRET result = NO_ERROR;
    grabLock(&GMutexDosCalls);
    if (name && *name)
    {
        if (findNamedSemLocked(name, kind == LX_HANDLE_MUTEX))
            result = ERROR_DUPLICATE_NAME;
        else
        {
            NamedSem *entry = calloc(1, sizeof *entry);
            if (entry)
                entry->name = strdup(name);
            uint32 internal = entry && entry->name ? lx_handle_create(kind, value, destroy) : 0;
            if (!internal)
            {
                if (entry)
                {
                    free(entry->name);
                    free(entry);
                }
                result = ERROR_NOT_ENOUGH_MEMORY;
            }
            else
            {
                entry->is_mutex = kind == LX_HANDLE_MUTEX;
                entry->handle = value;
                entry->internal_handle = internal;
                entry->next = GNamedSems;
                GNamedSems = entry;
            }
        }
    }
    else if (!name && kind == LX_HANDLE_EVENT)
    {
        uint32 internal = lx_handle_create(kind, value, destroy);
        if (!internal)
            result = ERROR_NOT_ENOUGH_MEMORY;
        else
        {
            uint32 old = GLastUnnamedEventHandle;
            GLastUnnamedEventSem = value;
            GLastUnnamedEventHandle = internal;
            if (old)
                lx_handle_close(old, LX_HANDLE_EVENT);
        }
    }
    ungrabLock(&GMutexDosCalls);
    if (result)
        lx_handle_close(public_handle, kind);
    else
        *output = public_handle;
    return result;
}

APIRET openSemaphore(const char *name, LxHandleKind kind, uint32 *output)
{
    if (!output || (kind == LX_HANDLE_MUTEX && !name))
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    void *value = NULL;
    if ((!name || !*name) && kind == LX_HANDLE_EVENT)
        value = GLastUnnamedEventSem;
    else if (name)
    {
        NamedSem *entry = findNamedSemLocked(name, kind == LX_HANDLE_MUTEX);
        if (entry)
            value = entry->handle;
    }
    uint32 handle = value ? lx_handle_create(kind, value,
                                             kind == LX_HANDLE_MUTEX ? destroyMutexResource
                                                                     : destroyEventResource)
                          : 0;
    ungrabLock(&GMutexDosCalls);
    if (!value)
        return ERROR_SEM_NOT_FOUND;
    if (!handle)
        return ERROR_NOT_ENOUGH_MEMORY;
    *output = handle;
    return NO_ERROR;
}

void retireSemaphoreName(void *value, LxHandleKind kind)
{
    if (lx_handle_count(kind, value) != 1)
        return;
    for (NamedSem **link = &GNamedSems; *link; link = &(*link)->next)
    {
        NamedSem *entry = *link;
        if (entry->handle == value && entry->is_mutex == (kind == LX_HANDLE_MUTEX))
        {
            *link = entry->next;
            uint32 internal = entry->internal_handle;
            free(entry->name);
            free(entry);
            lx_handle_close(internal, kind);
            return;
        }
    }
    if (kind == LX_HANDLE_EVENT && GLastUnnamedEventSem == value)
    {
        uint32 internal = GLastUnnamedEventHandle;
        GLastUnnamedEventHandle = 0;
        GLastUnnamedEventSem = NULL;
        lx_handle_close(internal, kind);
    }
}

APIRET DosCreateEventSem(PSZ name, PHEV output, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateEventSem('%s', %p, %u, %u)", name, output, (uint)attr, (uint)state);
    if (!output)
        return ERROR_INVALID_PARAMETER;
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
    return publishSemaphore(sem, LX_HANDLE_EVENT, name, output);
}

APIRET DosOpenEventSem(PSZ name, PHEV output)
{
    TRACE_NATIVE("DosOpenEventSem('%s', %p)", name, output);
    return openSemaphore(name, LX_HANDLE_EVENT, output);
}

APIRET DosCreateMutexSem(PSZ name, PHMTX output, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateMutexSem('%s', %p, %u, %u)", name, output, (uint)attr, (uint)state);
    if (!output)
        return ERROR_INVALID_PARAMETER;
    LxHostMutex *mutex = lx_host_mutex_create(1, state);
    if (!mutex)
        return ERROR_NOT_ENOUGH_MEMORY;
    return publishSemaphore(mutex, LX_HANDLE_MUTEX, name, output);
}

APIRET DosOpenMutexSem(PSZ name, PHMTX output)
{
    TRACE_NATIVE("DosOpenMutexSem('%s', %p)", name, output);
    return openSemaphore(name, LX_HANDLE_MUTEX, output);
}

APIRET DosRequestMutexSemResource(HMTX hmtx, ULONG ulTimeout, LxHostMutex *mutex)
{
    TRACE_NATIVE("DosRequestMutexSem(%u, %u)", (uint)hmtx, (uint)ulTimeout);

    if (!hmtx)
        return ERROR_INVALID_HANDLE;

    switch (lx_host_mutex_lock(mutex, ulTimeout))
    {
        case LX_HOST_LOCK_OK:
            return NO_ERROR;
        case LX_HOST_LOCK_BUSY:
        case LX_HOST_LOCK_TIMEOUT:
            return ERROR_TIMEOUT;
        case LX_HOST_LOCK_OWNER_DIED:
            return ERROR_SEM_OWNER_DIED;
        default:
            return ERROR_INVALID_HANDLE;
    }
}

APIRET DosRequestMutexSem(HMTX hmtx, ULONG ulTimeout)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hmtx, LX_HANDLE_MUTEX, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_REQUEST_MUTEX, hmtx, ulTimeout, &object, NULL, NULL);
    return result;
}

APIRET DosReleaseMutexSemResource(HMTX hmtx, LxHostMutex *mutex)
{
    TRACE_NATIVE("DosReleaseMutexSem(%u)", (uint)hmtx);

    if (!hmtx)
        return ERROR_INVALID_HANDLE;

    const LxHostLockResult rc = lx_host_mutex_unlock(mutex);
    if (rc == LX_HOST_LOCK_OK)
        return NO_ERROR;
    if (rc == LX_HOST_LOCK_NOT_OWNER)
        return ERROR_NOT_OWNER;
    return ERROR_INVALID_HANDLE;
}

APIRET DosReleaseMutexSem(HMTX hmtx)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hmtx, LX_HANDLE_MUTEX, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_RELEASE_MUTEX, hmtx, 0, &object, NULL, NULL);
    return result;
}

APIRET DosResetEventSemResource(HEV hev, PULONG pulPostCt, EventSem *sem)
{
    TRACE_NATIVE("DosResetEventSem(%u, %p)", (uint)hev, pulPostCt);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    APIRET retval = NO_ERROR;
    ULONG count = 0;

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
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object))
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

    lx_host_monitor_lock(sem->monitor);
    if (sem->posted)
        retval = ERROR_ALREADY_POSTED;
    else
    {
        sem->posted = 1;
        lx_host_monitor_wake_all(sem->monitor);
    }
    lx_host_monitor_unlock(sem->monitor);

    return retval;
}

APIRET DosPostEventSem(HEV hev)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_POST_EVENT, hev, 0, &object, NULL, NULL);
    return result;
}

APIRET DosWaitEventSemResource(HEV hev, ULONG timeout, EventSem *sem)
{
    TRACE_NATIVE("DosWaitEventSem(%u, %u)", (uint)hev, (uint)timeout);
    const uint64 deadline =
        timeout == UINT32_MAX ? UINT64_MAX : lx_host_monotonic_us() + (uint64)timeout * 1000;
    APIRET result = NO_ERROR;
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
            wait = (uint32)((deadline - now + 999) / 1000);
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
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object))
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

    lx_host_monitor_lock(sem->monitor);
    const ULONG count = (ULONG)sem->posted;
    lx_host_monitor_unlock(sem->monitor);

    if (pulPostCt)
        *pulPostCt = count;

    return NO_ERROR;
}

APIRET DosQueryEventSem(HEV hev, PULONG pulPostCt)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object))
        return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_QUERY_EVENT, hev, 0, &object, NULL, pulPostCt);
    return result;
}

APIRET DosCloseEventSem(HEV handle)
{
    TRACE_NATIVE("DosCloseEventSem(%u)", (uint)handle);
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

int semaphoreWord(PHSEM16 sem, int exchange, uint32 value, uint32 *previous)
{
    if (!sem)
        return 0;
    if (GLoaderState.execution)
    {
        uint32 address;
        return lx_execution_address(GLoaderState.execution, sem, 4, 3, &address) &&
               lx_execution_atomic_u32(GLoaderState.execution, address, exchange, value, previous);
    }
    grabLock(&GMutexDosCalls);
    *previous = lx_guest_load_u32(sem);
    if (exchange)
        lx_guest_store_u32(sem, value);
    ungrabLock(&GMutexDosCalls);
    return 1;
}

APIRET16 waitSemaphoreWord(PHSEM16 sem, LONG milliseconds, int request)
{
    const uint64 deadline =
        milliseconds < 0 ? UINT64_MAX : lx_host_monotonic_us() + (uint64)milliseconds * 1000;
    for (;;)
    {
        uint32 previous;
        if (!semaphoreWord(sem, request, 1, &previous))
            return ERROR_INVALID_PARAMETER;
        if (!previous)
            return NO_ERROR;
        if (milliseconds == 0 || lx_host_monotonic_us() >= deadline)
            return ERROR_TIMEOUT;
        lx_host_sleep_ms(1);
    }
}

APIRET16 DosSemRequest(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemRequest(%p, %d)", sem, (int)ms);
    return waitSemaphoreWord(sem, ms, 1);
}

APIRET16 DosSemClear(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemClear(%p)", sem);
    uint32 previous;
    return semaphoreWord(sem, 1, 0, &previous) ? NO_ERROR : ERROR_INVALID_PARAMETER;
}

APIRET16 DosSemWait(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemWait(%p, %d)", sem, (int)ms);
    return waitSemaphoreWord(sem, ms, 0);
}

APIRET16 DosSemSet(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemSet(%p)", sem);
    uint32 previous;
    return semaphoreWord(sem, 1, 1, &previous) ? NO_ERROR : ERROR_INVALID_PARAMETER;
}

int mutexCanClose(void *value)
{
    return !lx_host_mutex_busy(value);
}

APIRET DosCloseMutexSem(HMTX handle)
{
    TRACE_NATIVE("DosCloseMutexSem(%u)", (uint)handle);
    LxHandleBorrow object;
    if (!lx_handle_borrow(handle, LX_HANDLE_MUTEX, &object))
        return ERROR_INVALID_HANDLE;
    grabLock(&GMutexDosCalls);
    int closed = lx_handle_close_guarded(handle, LX_HANDLE_MUTEX, 1, mutexCanClose);
    if (closed == 1)
        retireSemaphoreName(object.resource, LX_HANDLE_MUTEX);
    ungrabLock(&GMutexDosCalls);
    lx_handle_release(&object);
    return closed == 1 ? NO_ERROR : closed == -1 ? ERROR_SEM_BUSY : ERROR_INVALID_HANDLE;
}
