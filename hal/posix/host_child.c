/* Public domain. No rights reserved. Wholly AI-authored. */
/* Child lifecycle and status normalization; no OS/2 data layouts cross here. */
#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "hal/host_process.h"
#include "hal/host_operation.h"
#include "hal/host.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
struct LxHostChild
{
    pid_t id;
    int waited;
    LxHostChildStatus status;
    pthread_mutex_t mutex;
};
static LxHostChild *spawn_child(const char *path, char *const *argv, char *const *env,
                                int new_group)
{
    LxHostChild *child = calloc(1, sizeof *child);
    if (!child)
        return NULL;
    if (pthread_mutex_init(&child->mutex, NULL))
    {
        free(child);
        return NULL;
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int initialized_actions = 0, initialized_attributes = 0;
    int inherited[2], copies[2] = {-1, -1};
    int scoped = lx_host_operation_channels(env, &inherited[0], &inherited[1]);
    int rc = scoped < 0 ? EINVAL : posix_spawn_file_actions_init(&actions);
    if (!rc)
        initialized_actions = 1;
    if (!rc && scoped)
        for (unsigned i = 0; i < 2 && !rc; i++)
        {
            copies[i] = fcntl(inherited[i], F_DUPFD_CLOEXEC, 3);
            if (copies[i] < 0)
                rc = errno;
            else if (!(rc = posix_spawn_file_actions_adddup2(&actions, copies[i], inherited[i])))
                rc = posix_spawn_file_actions_addclose(&actions, copies[i]);
        }
    if (!rc)
    {
        rc = posix_spawnattr_init(&attributes);
        initialized_attributes = !rc;
    }
    if (!rc && new_group)
    {
        rc = posix_spawnattr_setpgroup(&attributes, 0);
        if (!rc)
            rc = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    }
    if (!rc)
        rc = posix_spawn(&child->id, path, &actions, &attributes, argv, env);
    for (unsigned i = 0; i < 2; i++)
        if (copies[i] >= 0)
            close(copies[i]);
    if (initialized_actions)
        posix_spawn_file_actions_destroy(&actions);
    if (initialized_attributes)
        posix_spawnattr_destroy(&attributes);
    if (rc)
    {
        pthread_mutex_destroy(&child->mutex);
        free(child);
        return NULL;
    }
    return child;
}
/* posix_spawn has no portable stopped start. Only async-signal-safe calls run
   between fork and exec, which keeps this valid in a threaded process. */
LxHostChild *lx_host_child_spawn_suspended(const char *path, char *const *argv, char *const *env)
{
    /* exec runs only after resume, too late to report a failure to this caller. */
    if (access(path, X_OK))
        return NULL;
    LxHostChild *child = calloc(1, sizeof *child);
    if (!child)
        return NULL;
    if (pthread_mutex_init(&child->mutex, NULL))
    {
        free(child);
        return NULL;
    }
    int inherited[2], copies[2] = {-1, -1};
    int scoped = lx_host_operation_channels(env, &inherited[0], &inherited[1]);
    int failed = scoped < 0;
    for (unsigned i = 0; scoped > 0 && i < 2 && !failed; i++)
        failed = (copies[i] = fcntl(inherited[i], F_DUPFD_CLOEXEC, 3)) < 0;
    pid_t id = failed ? -1 : fork();
    if (!id)
    {
        for (unsigned i = 0; scoped > 0 && i < 2; i++)
            if (dup2(copies[i], inherited[i]) < 0)
                _exit(127);
        raise(SIGSTOP);
        execve(path, argv, env);
        _exit(127);
    }
    for (unsigned i = 0; i < 2; i++)
        if (copies[i] >= 0)
            close(copies[i]);
    int status = 0;
    if (id > 0)
    {
        /* A continue sent before the stop would be lost and strand the child. */
        pid_t waited;
        do
        {
            waited = waitpid(id, &status, WUNTRACED);
        } while (waited < 0 && errno == EINTR);
        if (waited != id || !WIFSTOPPED(status))
        {
            if (waited < 0)
                kill(id, SIGKILL);
            id = -1;
        }
    }
    if (id < 0)
    {
        pthread_mutex_destroy(&child->mutex);
        free(child);
        return NULL;
    }
    child->id = id;
    return child;
}
LxHostChild *lx_host_child_spawn(const char *path, char *const *argv, char *const *env)
{
    return spawn_child(path, argv, env, 0);
}
LxHostChild *lx_host_child_spawn_group(const char *path, char *const *argv, char *const *env)
{
    return spawn_child(path, argv, env, 1);
}
LxHostChild *lx_host_child_spawn_service(const char *path, char *const *argv, char *const *env)
{
    size_t count = 0;
    while (env && env[count])
        count++;
    char **clean = calloc(count + 1, sizeof *clean);
    if (!clean)
        return NULL;
    size_t output = 0;
    for (size_t i = 0; i < count; i++)
        if (strncmp(env[i], LX_HOST_OPERATION_LEASE "=", sizeof LX_HOST_OPERATION_LEASE) &&
            strncmp(env[i], LX_HOST_OPERATION_OWNER "=", sizeof LX_HOST_OPERATION_OWNER))
            clean[output++] = env[i];
    LxHostChild *child = spawn_child(path, argv, clean, 1);
    free(clean);
    return child;
}
static int poll_child(LxHostChild *child, LxHostChildStatus *status)
{
    if (child->waited)
    {
        *status = child->status;
        return 1;
    }
    int result;
    pid_t rc;
    do
    {
        rc = waitpid(child->id, &result, WNOHANG);
    } while (rc < 0 && errno == EINTR);
    if (!rc)
        return 0;
    if (rc < 0)
        return -1;
    child->waited = 1;
    child->status.result = WIFEXITED(result) ? (uint32_t)WEXITSTATUS(result) : 0;
    child->status.reason = LX_HOST_CHILD_EXITED;
    child->status.signal = 0;
    if (WIFSIGNALED(result))
    {
        int sig = WTERMSIG(result);
        child->status.signal = (uint32_t)sig;
        child->status.reason = sig == SIGTERM || sig == SIGKILL || sig == SIGINT || sig == SIGQUIT
                                   ? LX_HOST_CHILD_KILLED
                               : sig == SIGPIPE || sig == SIGABRT ? LX_HOST_CHILD_HARD_ERROR
                                                                  : LX_HOST_CHILD_EXCEPTION;
    }
    *status = child->status;
    return 1;
}
int lx_host_child_wait(LxHostChild *child, int nohang, LxHostChildStatus *status)
{
    for (;;)
    {
        int cancellation;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancellation);
        pthread_mutex_lock(&child->mutex);
        int result = poll_child(child, status);
        pthread_mutex_unlock(&child->mutex);
        pthread_setcancelstate(cancellation, NULL);
        if (result || nohang)
            return result;
        lx_host_sleep_ms(1);
    }
}
static int signal_child(LxHostChild *child, int signal_number)
{
    int cancellation;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancellation);
    pthread_mutex_lock(&child->mutex);
    LxHostChildStatus status;
    /* Reaping and signalling share a lock, so an exited child's PID cannot be reused here. */
    int polled = poll_child(child, &status);
    int result = polled == 1 ? 1 : polled < 0 ? -1 : kill(child->id, signal_number) ? -1 : 0;
    pthread_mutex_unlock(&child->mutex);
    pthread_setcancelstate(cancellation, NULL);
    return result;
}
int lx_host_child_terminate(LxHostChild *child)
{
    return signal_child(child, SIGKILL);
}
int lx_host_child_suspend(LxHostChild *child)
{
    return signal_child(child, SIGSTOP);
}
int lx_host_child_resume(LxHostChild *child)
{
    return signal_child(child, SIGCONT);
}
int lx_host_child_set_priority(LxHostChild *child, LxHostProcessPriority priority)
{
    int value;
    switch (priority)
    {
        case LX_HOST_PROCESS_PRIORITY_NORMAL:
            value = 0;
            break;
        case LX_HOST_PROCESS_PRIORITY_IDLE:
            value = 19;
            break;
        case LX_HOST_PROCESS_PRIORITY_REALTIME:
            errno = ENOTSUP;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }
    int cancellation;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancellation);
    pthread_mutex_lock(&child->mutex);
    LxHostChildStatus status;
    int polled = poll_child(child, &status);
    int result = polled ? -1 : lx_host_process_set_level((int)child->id, value);
    if (polled == 1)
        errno = ESRCH;
    pthread_mutex_unlock(&child->mutex);
    pthread_setcancelstate(cancellation, NULL);
    return result;
}
static void *reap(void *arg)
{
    LxHostChild *child = arg;
    LxHostChildStatus status;
    lx_host_child_wait(child, 0, &status);
    pthread_mutex_destroy(&child->mutex);
    free(child);
    return NULL;
}
void lx_host_child_release(LxHostChild *child)
{
    if (!child)
        return;
    if (child->waited)
    {
        pthread_mutex_destroy(&child->mutex);
        free(child);
        return;
    }
    pthread_t worker;
    if (!pthread_create(&worker, NULL, reap, child))
        pthread_detach(worker);
}
uint32_t lx_host_child_id(const LxHostChild *child)
{
    return (uint32_t)child->id;
}
