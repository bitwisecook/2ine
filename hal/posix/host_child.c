/* Public domain. No rights reserved. Wholly AI-authored. */
/* Child lifecycle and status normalization; no OS/2 data layouts cross here. */
#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "hal/host_process.h"
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
struct LxHostChild
{
    pid_t id;
    int waited;
    LxHostChildStatus status;
};
LxHostChild *lx_host_child_spawn(const char *path, char *const *argv, char *const *env)
{
    LxHostChild *child = calloc(1, sizeof *child);
    if (!child)
        return NULL;
    int rc = posix_spawn(&child->id, path, NULL, NULL, argv, env);
    if (rc)
    {
        free(child);
        return NULL;
    }
    return child;
}
int lx_host_child_wait(LxHostChild *child, int nohang, LxHostChildStatus *status)
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
        rc = waitpid(child->id, &result, nohang ? WNOHANG : 0);
    } while (rc < 0 && errno == EINTR);
    if (!rc)
        return 0;
    if (rc < 0)
        return -1;
    child->waited = 1;
    child->status.result = WIFEXITED(result) ? (uint32_t)WEXITSTATUS(result) : 0;
    child->status.reason = LX_HOST_CHILD_EXITED;
    if (WIFSIGNALED(result))
    {
        int sig = WTERMSIG(result);
        child->status.reason = sig == SIGTERM || sig == SIGKILL || sig == SIGINT || sig == SIGQUIT
                                   ? LX_HOST_CHILD_KILLED
                               : sig == SIGPIPE || sig == SIGABRT ? LX_HOST_CHILD_HARD_ERROR
                                                                  : LX_HOST_CHILD_EXCEPTION;
    }
    *status = child->status;
    return 1;
}
static void *reap(void *arg)
{
    LxHostChild *child = arg;
    LxHostChildStatus status;
    lx_host_child_wait(child, 0, &status);
    free(child);
    return NULL;
}
void lx_host_child_release(LxHostChild *child)
{
    if (!child)
        return;
    if (child->waited)
    {
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
