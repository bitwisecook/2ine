/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host child processes. Guest command syntax and result codes belong above. */
#ifndef TWOINE_HOST_PROCESS_H
#define TWOINE_HOST_PROCESS_H
#include <stdint.h>
typedef struct LxHostChild LxHostChild;
typedef enum LxHostProcessPriority
{
    LX_HOST_PROCESS_PRIORITY_NORMAL,
    LX_HOST_PROCESS_PRIORITY_IDLE,
    LX_HOST_PROCESS_PRIORITY_REALTIME
} LxHostProcessPriority;
/* Current process only. Zero means the requested host scheduling level applies
   to the process; -1 sets errno. Unsupported process-wide changes fail before
   mutation. This contract does not grant scheduling privileges. */
int lx_host_process_set_priority(LxHostProcessPriority priority);
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
    uint32_t signal; /* POSIX termination signal, or zero for ordinary exit. */
} LxHostChildStatus;
LxHostChild *lx_host_child_spawn(const char *path, char *const *arguments,
                                 char *const *environment);
/* A new host group retains ordinary guest operation ownership. */
LxHostChild *lx_host_child_spawn_group(const char *path, char *const *arguments,
                                       char *const *environment);
/* Trusted runtime services have independent group/lifetime and no operation lease. */
LxHostChild *lx_host_child_spawn_service(const char *path, char *const *arguments,
                                         char *const *environment);
/* The child stops before its first instruction, so lx_host_child_resume starts it.
   POSIX reports a failed image as exit status 127 after resume instead of here. */
LxHostChild *lx_host_child_spawn_suspended(const char *path, char *const *arguments,
                                           char *const *environment);
/* Hold or continue this retained child: 0 applied, 1 already finished, -1 failure.
   POSIX holds the whole process; Windows holds its primary thread. */
int lx_host_child_suspend(LxHostChild *child);
int lx_host_child_resume(LxHostChild *child);
uint32_t lx_host_child_id(const LxHostChild *child);
/* 1 observed completion, 0 still running, -1 failure. */
int lx_host_child_wait(LxHostChild *child, int nohang, LxHostChildStatus *status);
/* Terminate this retained child only: 0 signalled, 1 already finished, -1 failure. */
int lx_host_child_terminate(LxHostChild *child);
/* A released running child is detached and reaped by the host adapter. */
void lx_host_child_release(LxHostChild *child);
#endif
