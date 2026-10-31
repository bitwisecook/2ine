/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host child processes. Guest command syntax and result codes belong above. */
#ifndef TWOINE_HOST_PROCESS_H
#define TWOINE_HOST_PROCESS_H
#include <stdint.h>
typedef struct LxHostChild LxHostChild;
typedef enum LxHostTermination
{
    LX_HOST_CHILD_EXITED,
    LX_HOST_CHILD_KILLED,
    LX_HOST_CHILD_HARD_ERROR,
    LX_HOST_CHILD_EXCEPTION
} LxHostTermination;
typedef struct LxHostChildStatus
{
    LxHostTermination reason;
    uint32_t result;
} LxHostChildStatus;
LxHostChild *lx_host_child_spawn(const char *path, char *const *arguments,
                                 char *const *environment);
uint32_t lx_host_child_id(const LxHostChild *child);
/* 1 observed completion, 0 still running, -1 failure. */
int lx_host_child_wait(LxHostChild *child, int nohang, LxHostChildStatus *status);
/* A released running child is detached and reaped by the host adapter. */
void lx_host_child_release(LxHostChild *child);
#endif
