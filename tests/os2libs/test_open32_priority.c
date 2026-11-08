/* Public domain. No rights reserved. Wholly AI-authored. */
/* Process priority mapping and host failures must not publish a false class. */
#include "os2libs/pmwinx.h"
#include "host/process.h"
#include "host/system.h"
#include "host/thread.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#ifdef TWOINE_PRIORITY_REAL_HOST
#include <sys/resource.h>
#include <sys/wait.h>
#endif

/* Registration is shared with the complete module; this fixture can link the new unit alone. */
BOOL SetPriorityClass(ULONG process, ULONG priority);
ULONG GetPriorityClass(ULONG process);
/* This fixture has no child processes: only the current-process handle is valid. */
int open32ChildSetPriority(ULONG handle, LxHostProcessPriority priority, ULONG priority_class,
                           int *error);
int open32ChildPriorityClass(ULONG handle, PULONG priority_class);

int open32ChildSetPriority(ULONG handle, LxHostProcessPriority priority, ULONG priority_class,
                           int *error)
{
    (void)priority;
    (void)priority_class;
    (void)error;
    return handle == UINT32_MAX ? 0 : -1;
}

int open32ChildPriorityClass(ULONG handle, PULONG priority_class)
{
    (void)priority_class;
    return handle == UINT32_MAX ? 0 : -1;
}

static _Thread_local ULONG last_error;

void open32SetError(ULONG error)
{
    last_error = error;
}

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (last error %u, errno %d)\n", __FILE__, __LINE__, #x,       \
                    last_error, errno);                                                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#ifndef TWOINE_PRIORITY_REAL_HOST
static int forced_error;
static unsigned calls, active;
static LxHostProcessPriority selected;

int lx_host_process_set_priority(LxHostProcessPriority priority)
{
    CHECK(__atomic_add_fetch(&active, 1, __ATOMIC_SEQ_CST) == 1);
    selected = priority;
    calls++;
    int error = forced_error;
    lx_host_sleep_ms(1);
    CHECK(__atomic_sub_fetch(&active, 1, __ATOMIC_SEQ_CST) == 0);
    errno = error;
    return error ? -1 : 0;
}

static void *worker(void *argument)
{
    ULONG saved = (ULONG)(uintptr_t)argument;
    for (unsigned i = 0; i < 25; i++)
    {
        last_error = saved;
        CHECK(SetPriorityClass(UINT32_MAX, (i & 1) ? 0x20 : 0x80));
        CHECK(last_error == saved);
        ULONG current = GetPriorityClass(UINT32_MAX);
        CHECK(current == 0x20 || current == 0x80);
        CHECK(last_error == saved);
    }
    return NULL;
}

int main(void)
{
    last_error = 0x555;
    CHECK(GetPriorityClass(UINT32_MAX) == 0x20 && last_error == 0x555);
    ULONG invalid[] = {0, 1, 123, UINT32_MAX - 1, (ULONG)getpid()};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
    {
        CHECK(!SetPriorityClass(invalid[i], 0x20) && last_error == 6);
        CHECK(!GetPriorityClass(invalid[i]) && last_error == 6);
    }
    CHECK(!SetPriorityClass(0, 0) && last_error == 307); /* the class is validated first */
    CHECK(calls == 0);
    ULONG bad_classes[] = {0, 1, 0x60, 0x8000, UINT32_MAX};
    for (unsigned i = 0; i < sizeof bad_classes / sizeof bad_classes[0]; i++)
        CHECK(!SetPriorityClass(UINT32_MAX, bad_classes[i]) && last_error == 307);
    CHECK(calls == 0);
    ULONG classes[] = {0x20, 0x40, 0x80, 0x100};
    LxHostProcessPriority levels[] = {
        LX_HOST_PROCESS_PRIORITY_NORMAL, LX_HOST_PROCESS_PRIORITY_IDLE,
        LX_HOST_PROCESS_PRIORITY_NORMAL, LX_HOST_PROCESS_PRIORITY_REALTIME};
    for (unsigned i = 0; i < sizeof classes / sizeof classes[0]; i++)
    {
        last_error = 0x1234;
        CHECK(SetPriorityClass(UINT32_MAX, classes[i]) && last_error == 0x1234);
        CHECK(selected == levels[i]);
        CHECK(GetPriorityClass(UINT32_MAX) == classes[i] && last_error == 0x1234);
    }

    struct
    {
        int native;
        ULONG open32;
    } errors[] = {{EACCES, 5}, {EPERM, 5},   {ENOTSUP, 50}, {ENOSYS, 50},
                  {ENOMEM, 8}, {ESRCH, 303}, {EINVAL, 87},  {EIO, 31}};

    for (unsigned i = 0; i < sizeof errors / sizeof errors[0]; i++)
    {
        forced_error = errors[i].native;
        CHECK(!SetPriorityClass(UINT32_MAX, 0x20) && last_error == errors[i].open32);
        CHECK(GetPriorityClass(UINT32_MAX) == 0x100 && last_error == errors[i].open32);
    }
    forced_error = 0;
    LxHostThread *threads[4];
    for (unsigned i = 0; i < 4; i++)
    {
        threads[i] = lx_host_thread_create(worker, (void *)(uintptr_t)(0x100 + i), NULL);
        CHECK(threads[i]);
    }
    for (unsigned i = 0; i < 4; i++)
        CHECK(!lx_host_thread_join(threads[i], NULL));
    CHECK(calls == 112 && !active);
    puts(
        "Open32 priority validation, original mapping, error/state preservation and serialization passed");
    return 0;
}
#else
static void child(void)
{
    errno = 0;
    int initial = getpriority(PRIO_PROCESS, 0);
    CHECK(!errno);
    last_error = 0x1234;
    CHECK(SetPriorityClass(UINT32_MAX, 0x20) && last_error == 0x1234);
    CHECK(getpriority(PRIO_PROCESS, 0) == 0);
    CHECK(SetPriorityClass(UINT32_MAX, 0x80) && last_error == 0x1234);
    CHECK(GetPriorityClass(UINT32_MAX) == 0x80 && getpriority(PRIO_PROCESS, 0) == 0);
    CHECK(SetPriorityClass(UINT32_MAX, 0x40) && last_error == 0x1234);
    CHECK(GetPriorityClass(UINT32_MAX) == 0x40 && getpriority(PRIO_PROCESS, 0) == 19);
    CHECK(!SetPriorityClass(UINT32_MAX, 0x100) && last_error == 50);
    CHECK(GetPriorityClass(UINT32_MAX) == 0x40 && getpriority(PRIO_PROCESS, 0) == 19);
    if (geteuid() != 0)
    {
        CHECK(!SetPriorityClass(UINT32_MAX, 0x20) && last_error == 5);
        CHECK(GetPriorityClass(UINT32_MAX) == 0x40 && getpriority(PRIO_PROCESS, 0) == 19);
    }
    printf(
        "Owned child native nice %d -> 0 -> 19, denied promotion and unsupported realtime preserved class\n",
        initial);
    fflush(stdout);
    _exit(0);
}

int main(void)
{
    errno = 0;
    int initial = getpriority(PRIO_PROCESS, 0);
    CHECK(!errno);
    pid_t process = fork();
    CHECK(process >= 0);
    if (!process)
        child();
    int status;
    CHECK(waitpid(process, &status, 0) == process && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(getpriority(PRIO_PROCESS, 0) == initial);
    puts("Parent native scheduling priority remained unchanged");
    return 0;
}
#endif
