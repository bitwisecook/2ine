/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pipe leases survive guest wrappers without changing OS/2 wait semantics. */
#define _POSIX_C_SOURCE 200809L
#include "hal/host_operation.h"
#include "hal/host_process.h"
#include "hal/host.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *environment_value(char *const *environment, const char *name)
{
    size_t length = strlen(name);
    for (size_t i = 0; environment && environment[i]; i++)
        if (!strncmp(environment[i], name, length) && environment[i][length] == '=')
            return environment[i] + length + 1;
    return NULL;
}
static int channel(const char *text, int mode)
{
    if (!text || *text < '0' || *text > '9')
        return -1;
    char *end;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || *end || value < 3 || value > INT_MAX)
        return -1;
    int fd = (int)value, flags = fcntl(fd, F_GETFL);
    struct stat info;
    return flags >= 0 && (flags & O_ACCMODE) == mode && !fstat(fd, &info) && S_ISFIFO(info.st_mode)
               ? fd
               : -1;
}
int lx_host_operation_channels(char *const *environment, int *lease, int *owner)
{
    const char *write_channel = environment_value(environment, LX_HOST_OPERATION_LEASE);
    const char *read_channel = environment_value(environment, LX_HOST_OPERATION_OWNER);
    if (!write_channel && !read_channel)
        return 0;
    *lease = channel(write_channel, O_WRONLY);
    *owner = channel(read_channel, O_RDONLY);
    if (*lease < 0 || *owner < 0 || *lease == *owner)
    {
        errno = EINVAL;
        return -1;
    }
    return 1;
}
static void *watch_owner(void *argument)
{
    int fd = (int)(intptr_t)argument;
    char byte;
    ssize_t result;
    do
    {
        result = read(fd, &byte, 1);
    } while (result < 0 && errno == EINTR);
    /* The supervisor never sends data. Loss of its sole writer means the
       operation owner died; no guest session may survive as an orphan. */
    kill(getpid(), SIGKILL);
    _exit(128 + SIGKILL);
    return NULL;
}
int lx_host_operation_join(void)
{
    static int joined;
    if (joined)
        return 1;
    int lease, owner;
    int scoped = lx_host_operation_channels((char *const *)lx_host_environment(), &lease, &owner);
    if (scoped <= 0)
        return scoped == 0;
    /* Only the trusted child-spawn path explicitly inherits these channels.
       Unrelated host tools and service execs must close them automatically. */
    if (fcntl(lease, F_SETFD, FD_CLOEXEC) || fcntl(owner, F_SETFD, FD_CLOEXEC))
        return 0;
    pthread_t watcher;
    if (pthread_create(&watcher, NULL, watch_owner, (void *)(intptr_t)owner))
        return 0;
    if (pthread_detach(watcher))
        return 0;
    joined = 1;
    fprintf(stderr, "2ine operation: worker %u joined\n", (unsigned)getpid());
    return 1;
}

static volatile sig_atomic_t operation_group, cancellation;
static void cancel_operation(int signal)
{
    int saved = errno;
    if (signal == SIGUSR2 || cancellation != SIGKILL)
        cancellation = signal == SIGUSR2 ? SIGKILL : signal;
    if (operation_group > 0)
        kill(-(pid_t)operation_group, cancellation);
    errno = saved;
}
static int make_channel(int descriptors[2])
{
    if (pipe(descriptors))
        return 0;
    if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) || fcntl(descriptors[1], F_SETFD, FD_CLOEXEC))
    {
        close(descriptors[0]);
        close(descriptors[1]);
        return 0;
    }
    return 1;
}
int lx_host_operation_supervise(const char *executable, char *const *arguments,
                                char *const *environment)
{
    int lease[2], owner[2];
    if (!make_channel(lease))
        return 1;
    if (!make_channel(owner))
    {
        close(lease[0]);
        close(lease[1]);
        return 1;
    }
    size_t argument_count = 0, environment_count = 0;
    while (arguments && arguments[argument_count])
        argument_count++;
    while (environment && environment[environment_count])
        environment_count++;
    char **argv = calloc(argument_count + 2, sizeof *argv);
    char **env = calloc(environment_count + 3, sizeof *env);
    char lease_value[80], owner_value[80];
    snprintf(lease_value, sizeof lease_value, LX_HOST_OPERATION_LEASE "=%d", lease[1]);
    snprintf(owner_value, sizeof owner_value, LX_HOST_OPERATION_OWNER "=%d", owner[0]);
    int result = 1;
    LxHostChild *child = NULL;
    struct sigaction saved[3], action = {0};
    const int signals[3] = {SIGTERM, SIGINT, SIGUSR2};
    unsigned installed = 0;
    if (!argv || !env || !argument_count)
        goto finished;
    argv[0] = (char *)executable;
    for (size_t i = 0; i < argument_count; i++)
        argv[i + 1] = arguments[i];
    size_t count = 0;
    for (size_t i = 0; i < environment_count; i++)
        if (strncmp(environment[i], LX_HOST_OPERATION_LEASE "=", sizeof LX_HOST_OPERATION_LEASE) &&
            strncmp(environment[i], LX_HOST_OPERATION_OWNER "=", sizeof LX_HOST_OPERATION_OWNER))
            env[count++] = environment[i];
    env[count++] = lease_value;
    env[count++] = owner_value;
    action.sa_handler = cancel_operation;
    sigemptyset(&action.sa_mask);
    for (unsigned i = 0; i < 3; i++)
        sigaddset(&action.sa_mask, signals[i]);
    operation_group = cancellation = 0;
    for (; installed < 3; installed++)
        if (sigaction(signals[installed], &action, &saved[installed]))
            goto finished;
    child = lx_host_child_spawn_group(executable, argv, env);
    if (!child)
        goto finished;
    operation_group = (sig_atomic_t)lx_host_child_id(child);
    if (cancellation)
        kill(-(pid_t)operation_group, cancellation);
    close(lease[1]);
    lease[1] = -1;
    close(owner[0]);
    owner[0] = -1;
    fprintf(stderr, "2ine operation: supervising worker %u\n", (unsigned)operation_group);
    char bytes[256];
    ssize_t received;
    do
    {
        received = read(lease[0], bytes, sizeof bytes);
    } while (received > 0 || (received < 0 && errno == EINTR));
    if (received < 0)
    {
        kill(-(pid_t)operation_group, SIGKILL);
        cancellation = SIGKILL;
        close(owner[1]);
        owner[1] = -1;
    }
    /* Keep the root unreaped until lease EOF. Its reserved PID also reserves
       the process-group number while cancellation can still target it. */
    operation_group = 0;
    LxHostChildStatus status;
    if (lx_host_child_wait(child, 0, &status) == 1)
    {
        fprintf(stderr, "2ine operation: all workers exited; root result %u, signal %u\n",
                status.result, status.signal);
        result = status.result;
        if (cancellation || status.signal)
        {
            int signal = cancellation ? cancellation : (int)status.signal;
            /* No operation workers remain. Preserve a signalled result for
               frontend cancellation/receipt policy instead of reporting0. */
            struct sigaction normal = {0};
            normal.sa_handler = SIG_DFL;
            sigemptyset(&normal.sa_mask);
            sigaction(signal, &normal, NULL);
            kill(getpid(), signal);
            result = 128 + signal;
        }
    }
finished:
    operation_group = 0;
    while (installed)
    {
        installed--;
        sigaction(signals[installed], &saved[installed], NULL);
    }
    if (child)
        lx_host_child_release(child);
    close(lease[0]);
    if (lease[1] >= 0)
        close(lease[1]);
    if (owner[0] >= 0)
        close(owner[0]);
    if (owner[1] >= 0)
        close(owner[1]);
    free(argv);
    free(env);
    return result;
}
