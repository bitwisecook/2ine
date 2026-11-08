/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hooks joining Open32 process handles to the generic handle entries. Each int
   hook returns 1 when the handle was a process object and 0 to try the next owner. */
#ifndef TWOINE_PMWINX_PROCESS_H
#define TWOINE_PMWINX_PROCESS_H
#include "os2libs/pmwinx.h"
#include "host/process.h"
BOOL open32CloseProcess(ULONG handle);
int open32WaitProcess(ULONG handle, ULONG timeout, ULONG *result);
/* A child's primary thread handle: the operations act on the child itself. */
int open32ChildThreadExitCode(ULONG handle, PULONG code);
int open32ChildThreadResume(ULONG handle, PULONG previous);
int open32ChildThreadSuspend(ULONG handle, PULONG previous);
int open32ChildThreadTerminate(ULONG handle, ULONG code, BOOL *result);
/* These return 1 for a child, 0 when the handle names this process, -1 for a bad handle. */
int open32ChildSetPriority(ULONG handle, LxHostProcessPriority priority, ULONG priority_class,
                           int *error);
int open32ChildPriorityClass(ULONG handle, PULONG priority_class);
/* A real handle naming the calling thread; zero with the last error set. */
ULONG open32DuplicateCurrentThread(void);
#endif
