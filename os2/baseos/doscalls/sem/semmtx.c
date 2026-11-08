/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

typedef struct SemaphoreSharing
{
    struct SemaphoreSharing *next;
    void *resource;
} SemaphoreSharing;

static LxHostOnce sharing_once = LX_HOST_ONCE_INIT;

static LxHostMonitor *sharing_lock;

static SemaphoreSharing *sharing;

static void initialize_sharing(void)
{
    sharing_lock = lx_host_monitor_create();
    if (!sharing_lock)
        abort();
}

BOOL set_sharing(void *resource, BOOL shared)
{
    if (!shared)
        return TRUE;
    SemaphoreSharing *entry = malloc(sizeof *entry);
    if (!entry)
        return FALSE;
    entry->resource = resource;
    lx_host_once(&sharing_once, initialize_sharing);
    lx_host_monitor_lock(sharing_lock);
    entry->next = sharing;
    sharing = entry;
    lx_host_monitor_unlock(sharing_lock);
    return TRUE;
}

void remove_sharing(void *resource)
{
    lx_host_once(&sharing_once, initialize_sharing);
    lx_host_monitor_lock(sharing_lock);
    for (SemaphoreSharing **link = &sharing; *link; link = &(*link)->next)
        if ((*link)->resource == resource)
        {
            SemaphoreSharing *entry = *link;
            *link = entry->next;
            free(entry);
            break;
        }
    lx_host_monitor_unlock(sharing_lock);
}

BOOL semaphoreResourceShared(void *resource)
{
    lx_host_once(&sharing_once, initialize_sharing);
    lx_host_monitor_lock(sharing_lock);
    BOOL shared = FALSE;
    for (SemaphoreSharing *entry = sharing; entry; entry = entry->next)
        if (entry->resource == resource)
        {
            shared = TRUE;
            break;
        }
    lx_host_monitor_unlock(sharing_lock);
    return shared;
}

/* Owner words: a sequence that is odd during an update, then the OS/2 owner.
   Only the owner writes them. Named mutexes keep them in a block every process
   maps, so a query reports an owner in another process. */
enum
{
    MUTEX_SEQUENCE,
    MUTEX_PID,
    MUTEX_TID,
    MUTEX_COUNT,
    MUTEX_DIED,
    MUTEX_WORDS,
    MUTEX_BUCKETS = 256
};

typedef struct MutexState
{
    struct MutexState *next;
    void *resource;
    uint32 *owner;
    LxHostShared *shared;
} MutexState;

static MutexState *mutex_states[MUTEX_BUCKETS];

static MutexState **mutex_bucket(void *resource)
{
    return &mutex_states[((uintptr_t)resource >> 4) % MUTEX_BUCKETS];
}

static MutexState *find_mutex_state(void *resource)
{
    lx_host_once(&sharing_once, initialize_sharing);
    lx_host_monitor_lock(sharing_lock);
    MutexState *state = *mutex_bucket(resource);
    while (state && state->resource != resource)
        state = state->next;
    lx_host_monitor_unlock(sharing_lock);
    return state;
}

uint32 current_tid(void)
{
    return LX_GETTIB() ? ((PTIB2)LX_GETTIB2())->tib2_ultid : 0;
}

static void begin_owner_update(uint32 *owner)
{
    __atomic_add_fetch(&owner[MUTEX_SEQUENCE], 1, __ATOMIC_ACQ_REL);
}

static void end_owner_update(uint32 *owner)
{
    __atomic_add_fetch(&owner[MUTEX_SEQUENCE], 1, __ATOMIC_RELEASE);
}

static BOOL owned_by_caller(const uint32 *owner)
{
    return owner[MUTEX_COUNT] && owner[MUTEX_PID] == GProcessState.pib.process_id &&
           owner[MUTEX_TID] == current_tid();
}

/* A hash names the block a named mutex shares; a fresh mutex starts it unowned. */
static BOOL track_mutex(void *resource, const uint64 *hash, BOOL fresh)
{
    MutexState *state = calloc(1, sizeof *state);
    if (!state)
        return FALSE;
    state->resource = resource;
    if (hash)
    {
        char name[64];
        snprintf(name, sizeof name, "/2ine-mtxown-%016llx", (unsigned long long)*hash);
        LxHostSharedResult opened =
            lx_host_shared_open(name, MUTEX_WORDS * sizeof(uint32), &state->shared);
        if (opened == LX_HOST_SHARED_EXISTS)
            opened = lx_host_shared_open(name, 0, &state->shared);
        if (opened == LX_HOST_SHARED_OK &&
            lx_host_shared_size(state->shared) == MUTEX_WORDS * sizeof(uint32))
            state->owner = lx_host_shared_map(state->shared, NULL, 0);
        if (state->owner && fresh)
            memset(state->owner, 0, MUTEX_WORDS * sizeof(uint32));
    }
    else
        state->owner = calloc(MUTEX_WORDS, sizeof(uint32));
    if (!state->owner)
    {
        if (state->shared)
            lx_host_shared_close(state->shared);
        free(state);
        return FALSE;
    }
    lx_host_once(&sharing_once, initialize_sharing);
    lx_host_monitor_lock(sharing_lock);
    MutexState **bucket = mutex_bucket(resource);
    state->next = *bucket;
    *bucket = state;
    lx_host_monitor_unlock(sharing_lock);
    return TRUE;
}

static void forget_mutex(void *resource)
{
    lx_host_once(&sharing_once, initialize_sharing);
    lx_host_monitor_lock(sharing_lock);
    MutexState *state = NULL;
    for (MutexState **link = mutex_bucket(resource); *link; link = &(*link)->next)
        if ((*link)->resource == resource)
        {
            state = *link;
            *link = state->next;
            break;
        }
    lx_host_monitor_unlock(sharing_lock);
    if (!state)
        return;
    if (state->shared)
    {
        lx_host_shared_unmap(state->owner, MUTEX_WORDS * sizeof(uint32));
        lx_host_shared_close(state->shared);
    }
    else
        free(state->owner);
    free(state);
}

/* The caller holds the host mutex. True when the previous owner died holding it. */
static BOOL mutex_acquired(void *resource, BOOL owner_died)
{
    MutexState *state = find_mutex_state(resource);
    if (!state)
        return owner_died;
    uint32 *owner = state->owner;
    const BOOL died = owner_died || owner[MUTEX_DIED];
    begin_owner_update(owner);
    if (!died && owned_by_caller(owner))
        owner[MUTEX_COUNT]++;
    else
    {
        owner[MUTEX_PID] = GProcessState.pib.process_id;
        owner[MUTEX_TID] = current_tid();
        owner[MUTEX_COUNT] = 1;
        owner[MUTEX_DIED] = 0;
    }
    end_owner_update(owner);
    return died;
}

/* Runs before the host unlock, while this thread is still the only writer. */
void mutexReleasing(void *resource)
{
    MutexState *state = find_mutex_state(resource);
    if (!state || !owned_by_caller(state->owner))
        return;
    uint32 *owner = state->owner;
    begin_owner_update(owner);
    if (!--owner[MUTEX_COUNT])
        owner[MUTEX_PID] = owner[MUTEX_TID] = 0;
    end_owner_update(owner);
}

/* The ending thread releases what it still owns. The owner words stay in place
   and are flagged so a query and the next requester see the dead owner. */
void abandonThreadMutexes(uint32 tid)
{
    BOOL released = FALSE;
    lx_host_once(&sharing_once, initialize_sharing);
    lx_host_monitor_lock(sharing_lock);
    for (unsigned bucket = 0; bucket < MUTEX_BUCKETS; bucket++)
        for (MutexState *state = mutex_states[bucket]; state; state = state->next)
        {
            uint32 *owner = state->owner;
            const uint32 count = owner[MUTEX_COUNT];
            if (!count || owner[MUTEX_TID] != tid ||
                owner[MUTEX_PID] != GProcessState.pib.process_id)
                continue;
            begin_owner_update(owner);
            owner[MUTEX_DIED] = 1;
            end_owner_update(owner);
            for (uint32 i = 0; i < count; i++)
                lx_host_mutex_unlock(state->resource);
            released = TRUE;
        }
    lx_host_monitor_unlock(sharing_lock);
    if (released)
        notifyMuxWaiters();
}

void destroyMutexResource(void *value)
{
    LxHostMutex *mutex = value;
    remove_sharing(value);
    forget_mutex(value);
    /* An unpublished, initially-owned mutex can arrive here on allocation failure. */
    if (lx_host_mutex_busy(mutex))
        while (lx_host_mutex_unlock(mutex) == LX_HOST_LOCK_OK)
        {
        }
    lx_host_mutex_destroy(mutex);
}

APIRET DosCreateMutexSem(PSZ name, PHMTX output, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateMutexSem('%s', %p, %u, %u)", name, output, (uint)attr, (uint)state);
    if (!output || (attr & ~1u) || state > 1)
        return ERROR_INVALID_PARAMETER;
    if (name && *name)
    {
        if (strncasecmp(name, "\\SEM32\\", 7) || strlen(name) > 255 || !name[7])
            return ERROR_INVALID_NAME;
        uint64 hash = eventScope();
        for (const char *p = name; *p; p++)
        {
            unsigned char c = (unsigned char)*p;
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            hash = (hash ^ c) * 1099511628211ULL;
        }
        char identifier[64];
        snprintf(identifier, sizeof identifier, "/2ine-mutex1-%016llx", (unsigned long long)hash);
        LxHostMutex *mutex = NULL;
        LxHostNamedMutexResult opened = lx_host_mutex_open_named(identifier, 1, state, &mutex);
        if (opened != LX_HOST_NAMED_MUTEX_OK)
            return opened == LX_HOST_NAMED_MUTEX_EXISTS  ? ERROR_DUPLICATE_NAME
                   : opened == LX_HOST_NAMED_MUTEX_NOMEM ? ERROR_NOT_ENOUGH_MEMORY
                                                         : ERROR_ACCESS_DENIED;
        if (!set_sharing(mutex, TRUE) || !track_mutex(mutex, &hash, TRUE))
        {
            destroyMutexResource(mutex);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        if (state)
            mutex_acquired(mutex, FALSE);
        return publishSemaphore(mutex, LX_HANDLE_MUTEX, name, output);
    }
    LxHostMutex *mutex = lx_host_mutex_create(1, state);
    if (!mutex)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!set_sharing(mutex, (attr & 1) || name) || !track_mutex(mutex, NULL, TRUE))
    {
        destroyMutexResource(mutex);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (state)
        mutex_acquired(mutex, FALSE);
    return publishSemaphore(mutex, LX_HANDLE_MUTEX, name, output);
}

APIRET DosOpenMutexSem(PSZ name, PHMTX output)
{
    TRACE_NATIVE("DosOpenMutexSem('%s', %p)", name, output);
    APIRET local = openSemaphore(name, LX_HANDLE_MUTEX, output);
    if (local != ERROR_SEM_NOT_FOUND || !name || !*name)
        return local;
    if (strncasecmp(name, "\\SEM32\\", 7) || strlen(name) > 255 || !name[7])
        return ERROR_INVALID_NAME;
    uint64 hash = eventScope();
    for (const char *p = name; *p; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        hash = (hash ^ c) * 1099511628211ULL;
    }
    char identifier[64];
    snprintf(identifier, sizeof identifier, "/2ine-mutex1-%016llx", (unsigned long long)hash);
    LxHostMutex *mutex = NULL;
    LxHostNamedMutexResult opened = lx_host_mutex_open_named(identifier, 0, 0, &mutex);
    if (opened != LX_HOST_NAMED_MUTEX_OK)
        return opened == LX_HOST_NAMED_MUTEX_MISSING ? ERROR_SEM_NOT_FOUND
               : opened == LX_HOST_NAMED_MUTEX_NOMEM ? ERROR_NOT_ENOUGH_MEMORY
                                                     : ERROR_ACCESS_DENIED;
    if (!set_sharing(mutex, TRUE) || !track_mutex(mutex, &hash, FALSE))
    {
        destroyMutexResource(mutex);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    APIRET result = publishSemaphore(mutex, LX_HANDLE_MUTEX, name, output);
    return result == ERROR_DUPLICATE_NAME ? openSemaphore(name, LX_HANDLE_MUTEX, output) : result;
}

APIRET DosRequestMutexSemResource(HMTX hmtx, ULONG ulTimeout, LxHostMutex *mutex)
{
    TRACE_NATIVE("DosRequestMutexSem(%u, %u)", (uint)hmtx, (uint)ulTimeout);

    if (!hmtx)
        return ERROR_INVALID_HANDLE;

    switch (lx_host_mutex_lock(mutex, ulTimeout))
    {
        case LX_HOST_LOCK_OK:
            return mutex_acquired(mutex, FALSE) ? ERROR_SEM_OWNER_DIED : NO_ERROR;
        case LX_HOST_LOCK_BUSY:
        case LX_HOST_LOCK_TIMEOUT:
            return ERROR_TIMEOUT;
        case LX_HOST_LOCK_OWNER_DIED:
            mutex_acquired(mutex, TRUE);
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

    mutexReleasing(mutex);
    const LxHostLockResult rc = lx_host_mutex_unlock(mutex);
    if (rc == LX_HOST_LOCK_OK)
    {
        notifyMuxWaiters();
        return NO_ERROR;
    }
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

/* Pid and tid are written only for an owned semaphore, as the kernel does. */
APIRET DosQueryMutexSem(HMTX hmtx, PPID ppid, PTID ptid, PULONG pulCount)
{
    TRACE_NATIVE("DosQueryMutexSem(%u, %p, %p, %p)", (uint)hmtx, ppid, ptid, pulCount);
    LxHandleBorrow object;
    if (!lx_handle_borrow(hmtx, LX_HANDLE_MUTEX, &object))
        return ERROR_INVALID_HANDLE;
    uint32 snapshot[MUTEX_WORDS] = {0};
    MutexState *state = find_mutex_state(object.resource);
    if (state)
        for (;;)
        {
            const uint32 before = __atomic_load_n(&state->owner[MUTEX_SEQUENCE], __ATOMIC_ACQUIRE);
            for (unsigned i = MUTEX_PID; i < MUTEX_WORDS; i++)
                snapshot[i] = __atomic_load_n(&state->owner[i], __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (!(before & 1) &&
                __atomic_load_n(&state->owner[MUTEX_SEQUENCE], __ATOMIC_RELAXED) == before)
                break;
        }
    lx_handle_release(&object);
    if (!pulCount)
        return ERROR_INVALID_PARAMETER;
    *pulCount = snapshot[MUTEX_COUNT];
    if (!snapshot[MUTEX_COUNT])
        return NO_ERROR;
    if (!ppid || !ptid)
        return ERROR_INVALID_PARAMETER;
    *ppid = snapshot[MUTEX_PID];
    *ptid = snapshot[MUTEX_TID];
    const BOOL foreign = snapshot[MUTEX_PID] != GProcessState.pib.process_id;
    return snapshot[MUTEX_DIED] || (foreign && !lxIsProcessPeer(snapshot[MUTEX_PID]))
               ? ERROR_SEM_OWNER_DIED
               : NO_ERROR;
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
