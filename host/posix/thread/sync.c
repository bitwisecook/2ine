/* Public domain. No rights reserved. Wholly AI-authored. */
/* Synchronization adapters for host mutexes and monotonic wait deadlines. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "host/sync.h"
#include "host/shared_memory.h"
#include "host/posix/thread/sync_hooks.h"
#include "host/clock.h"
#include "host/process.h"
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

struct LxHostMutex
{
    pthread_mutex_t object;
    uint32_t held, waiting;
    LxHostShared *shared;
    uint32_t *state;
    int file;
};

typedef struct NamedOwner
{
    LxHostMutex *mutex;
    struct NamedOwner *next;
} NamedOwner;

enum
{
    NAMED_MUTEX_BYTES = 8,
    NAMED_MUTEX_OWNER = 0,
    NAMED_MUTEX_READY = 1
};

enum
{
    MICROSECONDS_PER_MILLISECOND = 1000,
    /* Polling interval while waiting for a contended named or timed lock. */
    LOCK_POLL_NANOSECONDS = 1000000,
    /* Time allowed for a named mutex's creator to finish initializing it. */
    NAMED_MUTEX_READY_TIMEOUT_US = 1000000
};

static pthread_key_t named_owner_key;
static pthread_once_t named_owner_once = PTHREAD_ONCE_INIT;
static int named_owner_ready;

static void abandon_named(void *value)
{
    NamedOwner *owner = value;
    while (owner)
    {
        NamedOwner *next = owner->next;
        LxHostMutex *mutex = owner->mutex;
        uint32_t count = __atomic_exchange_n(&mutex->held, 0, __ATOMIC_SEQ_CST);
        /* A nonzero owner marker plus an obtainable file lock signals abandonment. */
        flock(mutex->file, LOCK_UN);
        while (count--)
            pthread_mutex_unlock(&mutex->object);
        free(owner);
        owner = next;
    }
}

static void initialize_named_owner(void)
{
    named_owner_ready = !pthread_key_create(&named_owner_key, abandon_named);
}

static int retain_named_owner(LxHostMutex *mutex)
{
    pthread_once(&named_owner_once, initialize_named_owner);
    if (!named_owner_ready)
        return 0;
    NamedOwner *owner = malloc(sizeof *owner);
    if (!owner)
        return 0;
    *owner = (NamedOwner){mutex, pthread_getspecific(named_owner_key)};
    if (pthread_setspecific(named_owner_key, owner))
    {
        free(owner);
        return 0;
    }
    return 1;
}

static void release_named_owner(LxHostMutex *mutex)
{
    NamedOwner *first = pthread_getspecific(named_owner_key);
    for (NamedOwner **link = &first; *link; link = &(*link)->next)
        if ((*link)->mutex == mutex)
        {
            NamedOwner *owner = *link;
            *link = owner->next;
            free(owner);
            break;
        }
    pthread_setspecific(named_owner_key, first);
}

LxHostMutex *lx_host_mutex_create(int recursive, int owned)
{
    LxHostMutex *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,
                              recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_ERRORCHECK);
    lx_posix_mutex_attributes(&attr);
    int rc = pthread_mutex_init(&m->object, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc)
    {
        free(m);
        return NULL;
    }
    if (owned && pthread_mutex_lock(&m->object))
    {
        pthread_mutex_destroy(&m->object);
        free(m);
        return NULL;
    }
    m->held = owned ? 1 : 0;
    return m;
}

static LxHostLockResult result(int rc)
{
    switch (rc)
    {
        case 0:
            return LX_HOST_LOCK_OK;
        case EBUSY:
            return LX_HOST_LOCK_BUSY;
        case ETIMEDOUT:
            return LX_HOST_LOCK_TIMEOUT;
        case EPERM:
            return LX_HOST_LOCK_NOT_OWNER;
#if defined(EOWNERDEAD)
        case EOWNERDEAD:
            return LX_HOST_LOCK_OWNER_DIED;
#endif
        default:
            return LX_HOST_LOCK_FAILED;
    }
}

static void stopWaiting(void *p)
{
    LxHostMutex *m = p;
    __atomic_sub_fetch(&m->waiting, 1, __ATOMIC_SEQ_CST);
}

static LxHostLockResult lock_named(LxHostMutex *mutex, uint32_t timeout)
{
    uint64_t deadline =
        timeout == UINT32_MAX
            ? UINT64_MAX
            : lx_host_monotonic_us() + (uint64_t)timeout * MICROSECONDS_PER_MILLISECOND;
    LxHostLockResult result = LX_HOST_LOCK_FAILED;
    __atomic_add_fetch(&mutex->waiting, 1, __ATOMIC_SEQ_CST);
    pthread_cleanup_push(stopWaiting, mutex);
    for (;;)
    {
        int cancellation;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancellation);
        int locked = pthread_mutex_trylock(&mutex->object);
        if (!locked)
        {
            if (__atomic_load_n(&mutex->held, __ATOMIC_SEQ_CST))
            {
                __atomic_add_fetch(&mutex->held, 1, __ATOMIC_SEQ_CST);
                result = LX_HOST_LOCK_OK;
                pthread_setcancelstate(cancellation, NULL);
                break;
            }
            if (!flock(mutex->file, LOCK_EX | LOCK_NB))
            {
                if (!retain_named_owner(mutex))
                {
                    flock(mutex->file, LOCK_UN);
                    pthread_mutex_unlock(&mutex->object);
                    pthread_setcancelstate(cancellation, NULL);
                    break;
                }
                uint32_t process, parent;
                lx_host_process_ids(&process, &parent);
                result =
                    __atomic_exchange_n(&mutex->state[NAMED_MUTEX_OWNER], process, __ATOMIC_ACQ_REL)
                        ? LX_HOST_LOCK_OWNER_DIED
                        : LX_HOST_LOCK_OK;
                __atomic_store_n(&mutex->held, 1, __ATOMIC_SEQ_CST);
                pthread_setcancelstate(cancellation, NULL);
                break;
            }
            int error = errno;
            pthread_mutex_unlock(&mutex->object);
            if (error != EWOULDBLOCK && error != EAGAIN)
            {
                pthread_setcancelstate(cancellation, NULL);
                break;
            }
        }
        else if (locked != EBUSY)
        {
            pthread_setcancelstate(cancellation, NULL);
            break;
        }
        pthread_setcancelstate(cancellation, NULL);
        if (!timeout || lx_host_monotonic_us() >= deadline)
        {
            result = timeout ? LX_HOST_LOCK_TIMEOUT : LX_HOST_LOCK_BUSY;
            break;
        }
        const struct timespec pause = {0, LOCK_POLL_NANOSECONDS};
        nanosleep(&pause, NULL);
    }
    pthread_cleanup_pop(1);
    return result;
}

LxHostLockResult lx_host_mutex_lock(LxHostMutex *m, uint32_t timeout)
{
    if (m->shared)
        return lock_named(m, timeout);
    int rc;
    __atomic_add_fetch(&m->waiting, 1, __ATOMIC_SEQ_CST);
    pthread_cleanup_push(stopWaiting, m);
    if (timeout == UINT32_MAX)
        rc = pthread_mutex_lock(&m->object);
    else
    {
        const uint64_t deadline =
            lx_host_monotonic_us() + (uint64_t)timeout * MICROSECONDS_PER_MILLISECOND;
        while ((rc = pthread_mutex_trylock(&m->object)) == EBUSY && timeout)
        {
            if (lx_host_monotonic_us() >= deadline)
            {
                rc = ETIMEDOUT;
                break;
            }
            const struct timespec pause = {0, LOCK_POLL_NANOSECONDS};
            nanosleep(&pause, NULL);
        }
    }
    lx_posix_mutex_recover(&m->object, rc);
    if (rc == 0)
        __atomic_add_fetch(&m->held, 1, __ATOMIC_SEQ_CST);
#if defined(EOWNERDEAD)
    if (rc == EOWNERDEAD)
        __atomic_store_n(&m->held, 1, __ATOMIC_SEQ_CST);
#endif
    pthread_cleanup_pop(1);
    return result(rc);
}

LxHostLockResult lx_host_mutex_unlock(LxHostMutex *m)
{
    if (m->shared)
    {
        int cancellation;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancellation);
        int rc = pthread_mutex_trylock(&m->object);
        if (rc)
        {
            pthread_setcancelstate(cancellation, NULL);
            return rc == EBUSY ? LX_HOST_LOCK_NOT_OWNER : LX_HOST_LOCK_FAILED;
        }
        uint32_t count = __atomic_load_n(&m->held, __ATOMIC_SEQ_CST);
        if (!count)
        {
            pthread_mutex_unlock(&m->object);
            pthread_setcancelstate(cancellation, NULL);
            return LX_HOST_LOCK_NOT_OWNER;
        }
        if (count == 1)
        {
            __atomic_store_n(&m->state[NAMED_MUTEX_OWNER], 0, __ATOMIC_RELEASE);
            flock(m->file, LOCK_UN);
            release_named_owner(m);
        }
        __atomic_sub_fetch(&m->held, 1, __ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&m->object);
        pthread_mutex_unlock(&m->object);
        pthread_setcancelstate(cancellation, NULL);
        return LX_HOST_LOCK_OK;
    }
    int rc = pthread_mutex_unlock(&m->object);
    if (!rc)
        __atomic_sub_fetch(&m->held, 1, __ATOMIC_SEQ_CST);
    return result(rc);
}

int lx_host_mutex_busy(LxHostMutex *m)
{
    return __atomic_load_n(&m->held, __ATOMIC_SEQ_CST) ||
           __atomic_load_n(&m->waiting, __ATOMIC_SEQ_CST);
}

LxHostLockResult lx_host_mutex_destroy(LxHostMutex *m)
{
    if (lx_host_mutex_busy(m))
        return LX_HOST_LOCK_BUSY;
    int rc = pthread_mutex_destroy(&m->object);
    if (!rc)
    {
        if (m->shared)
        {
            lx_host_shared_unmap(m->state, NAMED_MUTEX_BYTES);
            lx_host_shared_close(m->shared);
            close(m->file);
        }
        free(m);
    }
    return result(rc);
}

LxHostNamedMutexResult lx_host_mutex_open_named(const char *name, int create, int owned,
                                                LxHostMutex **output)
{
    *output = NULL;
    LxHostShared *shared = NULL;
    LxHostSharedResult opened = lx_host_shared_open(name, create ? NAMED_MUTEX_BYTES : 0, &shared);
    if (opened != LX_HOST_SHARED_OK)
        return opened == LX_HOST_SHARED_MISSING  ? LX_HOST_NAMED_MUTEX_MISSING
               : opened == LX_HOST_SHARED_EXISTS ? LX_HOST_NAMED_MUTEX_EXISTS
               : opened == LX_HOST_SHARED_NOMEM  ? LX_HOST_NAMED_MUTEX_NOMEM
                                                 : LX_HOST_NAMED_MUTEX_DENIED;
    LxHostMutex *mutex = lx_host_mutex_create(1, 0);
    uint32_t *state = lx_host_shared_size(shared) == NAMED_MUTEX_BYTES
                          ? lx_host_shared_map(shared, NULL, 0)
                          : NULL;
    const char *directory = getenv("TWOINE_IPC_DIR");
    char fallback[80];
    if (!directory || !*directory)
    {
        snprintf(fallback, sizeof fallback, "/tmp/2ine-%lu", (unsigned long)getuid());
        directory = fallback;
    }
    size_t length = strlen(directory) + strlen(name) + 16;
    char *path = malloc(length);
    int fd = -1;
    if (path)
    {
        snprintf(path, length, "%s/%s.mutex", directory, name + 1);
        fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        free(path);
    }
    if (!mutex || !state || fd < 0)
        goto failed;
    if (create)
    {
        __atomic_store_n(&state[NAMED_MUTEX_OWNER], 0, __ATOMIC_RELAXED);
    }
    else
    {
        uint64_t deadline = lx_host_monotonic_us() + NAMED_MUTEX_READY_TIMEOUT_US;
        while (!__atomic_load_n(&state[NAMED_MUTEX_READY], __ATOMIC_ACQUIRE))
        {
            if (lx_host_monotonic_us() >= deadline)
                goto failed;
            lx_host_sleep_ms(1);
        }
    }
    mutex->shared = shared;
    mutex->state = state;
    mutex->file = fd;
    if (owned && lx_host_mutex_lock(mutex, 0) != LX_HOST_LOCK_OK)
    {
        lx_host_mutex_destroy(mutex);
        return LX_HOST_NAMED_MUTEX_DENIED;
    }
    if (create)
        __atomic_store_n(&state[NAMED_MUTEX_READY], 1, __ATOMIC_RELEASE);
    *output = mutex;
    return LX_HOST_NAMED_MUTEX_OK;
failed:
    if (mutex)
        lx_host_mutex_destroy(mutex);
    if (state)
        lx_host_shared_unmap(state, NAMED_MUTEX_BYTES);
    lx_host_shared_close(shared);
    if (fd >= 0)
        close(fd);
    return LX_HOST_NAMED_MUTEX_NOMEM;
}

struct LxHostMonitor
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    clockid_t clock;
};

LxHostMonitor *lx_host_monitor_create(void)
{
    LxHostMonitor *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    if (pthread_mutex_init(&m->mutex, NULL))
    {
        free(m);
        return NULL;
    }
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    m->clock = lx_posix_condition_clock(&attr);
    int rc = pthread_cond_init(&m->condition, &attr);
    pthread_condattr_destroy(&attr);
    if (rc)
    {
        pthread_mutex_destroy(&m->mutex);
        free(m);
        return NULL;
    }
    return m;
}

void lx_host_monitor_destroy(LxHostMonitor *m)
{
    pthread_cond_destroy(&m->condition);
    pthread_mutex_destroy(&m->mutex);
    free(m);
}

void lx_host_monitor_lock(LxHostMonitor *m)
{
    pthread_mutex_lock(&m->mutex);
}

void lx_host_monitor_unlock(LxHostMonitor *m)
{
    pthread_mutex_unlock(&m->mutex);
}

static void unlockMonitor(void *p)
{
    lx_host_monitor_unlock(p);
}

LxHostLockResult lx_host_monitor_wait(LxHostMonitor *m, uint32_t ms)
{
    int rc;
    pthread_cleanup_push(unlockMonitor, m);
    if (ms == UINT32_MAX)
        rc = pthread_cond_wait(&m->condition, &m->mutex);
    else
        rc = lx_posix_condition_timedwait(&m->condition, &m->mutex, m->clock, ms);
    pthread_cleanup_pop(0);
    return result(rc);
}

void lx_host_monitor_wake_all(LxHostMonitor *m)
{
    pthread_cond_broadcast(&m->condition);
}

void lx_host_once(LxHostOnce *once, void (*initialize)(void))
{
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&once->state, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED))
    {
        initialize();
        __atomic_store_n(&once->state, 2, __ATOMIC_RELEASE);
    }
    else
        while (__atomic_load_n(&once->state, __ATOMIC_ACQUIRE) != 2)
        {
            const struct timespec pause = {0, LOCK_POLL_NANOSECONDS};
            nanosleep(&pause, NULL);
        }
}
