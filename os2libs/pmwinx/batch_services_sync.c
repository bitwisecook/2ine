/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native DOS mutex/event ownership stays in DOSCALLS. A gate makes an Open32
   auto-reset event consume one post even when several waiters are awakened. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "guest/handles.h"
#include "host/clock.h"
#include "host/sync.h"
#include "host/shared_memory.h"
#include <stdio.h>

enum
{
    EVENT_MODIFY_STATE_RIGHT = 2
};

typedef struct SyncMetadata
{
    ULONG ready, event, manual;
    char name[241];
} SyncMetadata;

typedef struct Open32Sync
{
    ULONG native, gate, handle;
    BOOL event, manual;
    LxHostShared *shared;
    SyncMetadata *metadata;
    struct Open32Sync *next;
} Open32Sync;

static LxHostOnce sync_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *sync_lock;
static Open32Sync *sync_objects;

static void initialize_sync_objects(void)
{
    sync_lock = lx_host_monitor_create();
    if (!sync_lock)
        abort();
}

static ULONG close_native(ULONG handle, BOOL event)
{
    typedef APIRET(OS2API * Fn)(ULONG);
    Fn call = (Fn)open32DosService(event ? "DosCloseEventSem" : "DosCloseMutexSem");
    return call ? call(handle) : 120;
}

static void destroy_sync(void *resource)
{
    Open32Sync *sync = resource;
    if (sync_lock && sync->handle)
    {
        lx_host_monitor_lock(sync_lock);
        Open32Sync **link = &sync_objects;
        while (*link && *link != sync)
            link = &(*link)->next;
        if (*link)
            *link = sync->next;
        lx_host_monitor_unlock(sync_lock);
    }
    if (sync->native)
        close_native(sync->native, sync->event);
    if (sync->gate)
        close_native(sync->gate, FALSE);
    if (sync->metadata)
        lx_host_shared_unmap(sync->metadata, sizeof *sync->metadata);
    if (sync->shared)
        lx_host_shared_close(sync->shared);
    free(sync);
}

static ULONG native_create(PSZ name, BOOL event, BOOL initial, ULONG *handle, BOOL *existing)
{
    typedef APIRET(OS2API * Create)(PSZ, PULONG, ULONG, BOOL);
    typedef APIRET(OS2API * Open)(PSZ, PULONG);
    Create create = (Create)open32DosService(event ? "DosCreateEventSem" : "DosCreateMutexSem");
    Open open = (Open)open32DosService(event ? "DosOpenEventSem" : "DosOpenMutexSem");
    ULONG error = create ? create(name, handle, name ? 1 : 0, initial) : 120;
    if (name && error == 285 && open)
    {
        *handle = 0;
        error = open(name, handle);
        if (!error)
            *existing = TRUE;
    }
    return error;
}

static ULONG make_sync(PVOID security, BOOL event, BOOL manual, BOOL initial, PSZ name)
{
    /* The inherit word only marks the handle, and no child process ever receives one. */
    (void)security;
    if (name && (!*name || strlen(name) > 240))
    {
        open32SetError(123);
        return 0;
    }
    char identifier[80], gate[84], shared_name[40];
    PSZ native_name = NULL;
    if (name)
    {
        uint64 hash = 1469598103934665603ULL;
        const char *scope = getenv("TWOINE_BALL");
        for (const char *p = scope; p && *p; p++)
            hash = (hash ^ (uint8)*p) * 1099511628211ULL;
        hash = (hash ^ 255) * 1099511628211ULL;
        for (const char *p = name; *p; p++)
            hash = (hash ^ (uint8)*p) * 1099511628211ULL;
        snprintf(identifier, sizeof identifier, "\\SEM32\\2ine-open32-%016llx",
                 (unsigned long long)hash);
        snprintf(gate, sizeof gate, "%s-g", identifier);
        native_name = identifier;
        snprintf(shared_name, sizeof shared_name, "/2ine-wsync1-%016llx", (unsigned long long)hash);
    }
    Open32Sync *sync = calloc(1, sizeof *sync);
    if (!sync)
    {
        open32SetError(8);
        return 0;
    }
    sync->event = event;
    sync->manual = manual;
    if (name)
    {
        BOOL found = FALSE;
        for (unsigned attempt = 0; attempt < 8; attempt++)
        {
            LxHostSharedResult opened = lx_host_shared_open(shared_name, 0, &sync->shared);
            if (opened == LX_HOST_SHARED_OK)
            {
                found = TRUE;
                break;
            }
            if (opened == LX_HOST_SHARED_MISSING)
            {
                opened = lx_host_shared_open(shared_name, sizeof(SyncMetadata), &sync->shared);
                if (opened == LX_HOST_SHARED_OK)
                    break;
                if (opened == LX_HOST_SHARED_EXISTS)
                    continue;
            }
            destroy_sync(sync);
            open32SetError(8);
            return 0;
        }
        if (!sync->shared || lx_host_shared_size(sync->shared) != sizeof(SyncMetadata) ||
            !(sync->metadata = lx_host_shared_map(sync->shared, NULL, 0)))
        {
            destroy_sync(sync);
            open32SetError(8);
            return 0;
        }
        if (!found)
        {
            sync->metadata->event = event;
            sync->metadata->manual = manual;
            strcpy(sync->metadata->name, name);
        }
        else
        {
            uint64 deadline = lx_host_monotonic_us() + 1000000;
            while (!__atomic_load_n(&sync->metadata->ready, __ATOMIC_ACQUIRE) &&
                   lx_host_monotonic_us() < deadline)
                lx_host_sleep_ms(1);
            if (!__atomic_load_n(&sync->metadata->ready, __ATOMIC_ACQUIRE) ||
                sync->metadata->event != (ULONG)event || strcmp(sync->metadata->name, name))
            {
                destroy_sync(sync);
                open32SetError(6);
                return 0;
            }
            sync->manual = sync->metadata->manual;
        }
    }
    BOOL existing = FALSE, ignored = FALSE;
    ULONG error = native_create(native_name, event, initial, &sync->native, &existing);
    if (!error && event && !sync->manual)
        error = native_create(name ? gate : NULL, FALSE, FALSE, &sync->gate, &ignored);
    if (!error && sync->metadata)
        __atomic_store_n(&sync->metadata->ready, 1, __ATOMIC_RELEASE);
    ULONG handle = !error
                       ? lx_handle_create(event ? LX_HANDLE_OPEN32_EVENT : LX_HANDLE_OPEN32_MUTEX,
                                          sync, destroy_sync)
                       : 0;
    if (!handle)
    {
        destroy_sync(sync);
        open32SetError(error ? error : 8);
        return 0;
    }
    lx_host_once(&sync_once, initialize_sync_objects);
    sync->handle = handle;
    lx_host_monitor_lock(sync_lock);
    sync->next = sync_objects;
    sync_objects = sync;
    lx_host_monitor_unlock(sync_lock);
    if (existing)
        open32SetError(183);
    return handle;
}

ULONG CreateEvent(PVOID security, BOOL manual, BOOL initial, PSZ name)
{
    return make_sync(security, TRUE, !!manual, !!initial, name);
}

ULONG CreateMutex(PVOID security, BOOL owner, PSZ name)
{
    return make_sync(security, FALSE, FALSE, !!owner, name);
}

BOOL open32CloseSync(ULONG handle)
{
    return lx_handle_close(handle, LX_HANDLE_OPEN32_EVENT) ||
           lx_handle_close(handle, LX_HANDLE_OPEN32_MUTEX);
}

static BOOL event_operation(ULONG handle, BOOL set)
{
    LxHandleBorrow borrow = {0};
    uint32_t access = 0;
    if (!lx_handle_access(handle, LX_HANDLE_OPEN32_EVENT, &access) ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_EVENT, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    if (!(access & EVENT_MODIFY_STATE_RIGHT))
    {
        lx_handle_release(&borrow);
        /* The original reports a refused SetEvent as a bad handle, ResetEvent as denied. */
        open32SetError(set ? 6 : 5);
        return FALSE;
    }
    Open32Sync *sync = borrow.resource;
    ULONG error;
    if (set)
    {
        typedef APIRET(OS2API * Fn)(ULONG);
        Fn call = (Fn)open32DosService("DosPostEventSem");
        error = call ? call(sync->native) : 120;
        if (error == 299)
            error = 0; /* Already posted is still a signalled event. */
    }
    else
    {
        typedef APIRET(OS2API * Fn)(ULONG, PULONG);
        Fn call = (Fn)open32DosService("DosResetEventSem");
        ULONG count;
        error = call ? call(sync->native, &count) : 120;
        if (error == 300)
            error = 0;
    }
    lx_handle_release(&borrow);
    if (error)
        open32SetError(error);
    return !error;
}

BOOL SetEvent(ULONG handle)
{
    return event_operation(handle, TRUE);
}

BOOL ResetEvent(ULONG handle)
{
    return event_operation(handle, FALSE);
}

BOOL ReleaseMutex(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_MUTEX, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    typedef APIRET(OS2API * Fn)(ULONG);
    Fn call = (Fn)open32DosService("DosReleaseMutexSem");
    ULONG error = call ? call(((Open32Sync *)borrow.resource)->native) : 120;
    lx_handle_release(&borrow);
    if (error)
        open32SetError(error);
    return !error;
}

static ULONG wait_native(ULONG handle, BOOL event, ULONG timeout)
{
    typedef APIRET(OS2API * Fn)(ULONG, ULONG);
    Fn call = (Fn)open32DosService(event ? "DosWaitEventSem" : "DosRequestMutexSem");
    return call ? call(handle, timeout) : 120;
}

typedef struct SyncWait
{
    LxHandleBorrow borrow;
    ULONG timeout;
} SyncWait;

static void release_sync_wait(void *argument)
{
    lx_handle_release(&((SyncWait *)argument)->borrow);
}

static ULONG wait_sync(void *argument)
{
    SyncWait *wait = argument;
    Open32Sync *sync = wait->borrow.resource;
    ULONG error = 0;
    BOOL gate = FALSE;
    uint64 deadline = lx_host_monotonic_us() + (uint64)wait->timeout * 1000;
    if (sync->gate)
    {
        error = wait_native(sync->gate, FALSE, wait->timeout);
        gate = !error || error == 105;
    }
    if (!error || error == 105)
    {
        ULONG timeout = wait->timeout;
        if (timeout != UINT32_MAX)
        {
            uint64 now = lx_host_monotonic_us();
            timeout = now >= deadline ? 0 : (ULONG)((deadline - now + 999) / 1000);
        }
        error = wait_native(sync->native, sync->event, timeout);
        if (!error && sync->event && !sync->manual)
        {
            typedef APIRET(OS2API * Fn)(ULONG, PULONG);
            ULONG count;
            Fn reset = (Fn)open32DosService("DosResetEventSem");
            error = reset ? reset(sync->native, &count) : 120;
        }
    }
    if (gate)
    {
        typedef APIRET(OS2API * Fn)(ULONG);
        Fn release = (Fn)open32DosService("DosReleaseMutexSem");
        if (release)
            release(sync->gate);
    }
    if (error == 640 || error == 121)
        return 258;
    if (error == 105)
        return 0x80; /* ERROR_SEM_OWNER_DIED / WAIT_ABANDONED */
    if (error)
    {
        open32SetError(error);
        return UINT32_MAX;
    }
    return 0;
}

int open32WaitSync(ULONG handle, ULONG timeout, ULONG *result)
{
    SyncWait wait = {.timeout = timeout};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_EVENT, &wait.borrow) &&
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_MUTEX, &wait.borrow))
        return 0;
    *result = lx_host_guarded_call(wait_sync, &wait, release_sync_wait, &wait);
    return 1;
}

void open32SyncCleanup(void)
{
    if (!sync_lock)
        return;
    for (;;)
    {
        lx_host_monitor_lock(sync_lock);
        Open32Sync *sync = sync_objects;
        if (sync)
            sync_objects = sync->next;
        lx_host_monitor_unlock(sync_lock);
        if (!sync)
            break;
        lx_handle_close(sync->handle,
                        sync->event ? LX_HANDLE_OPEN32_EVENT : LX_HANDLE_OPEN32_MUTEX);
    }
}
