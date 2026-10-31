/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native thread creation and cancellation cleanup are confined to the host. */
#include "host_thread.h"
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct LxHostThread
{
    HANDLE object;
};
typedef struct Start
{
    void *(*entry)(void *);
    void *argument;
    void (*cleanup)(void *);
} Start;
static DWORD WINAPI enter(void *argument)
{
    Start start = *(Start *)argument;
    free(argument);
    void *result = start.entry(start.argument);
    if (start.cleanup)
        start.cleanup(start.argument);
    return (DWORD)(uintptr_t)result;
}
LxHostThread *lx_host_thread_create(void *(*entry)(void *), void *argument, void (*cleanup)(void *))
{
    LxHostThread *thread = calloc(1, sizeof *thread);
    Start *start = malloc(sizeof *start);
    if (!thread || !start)
    {
        free(thread);
        free(start);
        return NULL;
    }
    start->entry = entry;
    start->argument = argument;
    start->cleanup = cleanup;
    thread->object = CreateThread(NULL, 0, enter, start, 0, NULL);
    if (!thread->object)
    {
        free(start);
        free(thread);
        return NULL;
    }
    return thread;
}
int lx_host_thread_join(LxHostThread *thread, void **result)
{
    DWORD value;
    if (WaitForSingleObject(thread->object, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(thread->object, &value))
        return -1;
    if (result)
        *result = (void *)(uintptr_t)value;
    CloseHandle(thread->object);
    free(thread);
    return 0;
}
void lx_host_thread_detach(LxHostThread *thread)
{
    CloseHandle(thread->object);
    free(thread);
}
void lx_host_thread_exit(void *result)
{
    ExitThread((DWORD)(uintptr_t)result);
}
uint64_t lx_host_thread_id(void)
{
    return GetCurrentThreadId();
}
uint32_t lx_host_guarded_call(uint32_t (*operation)(void *), void *argument,
                              void (*cleanup)(void *), void *cleanup_argument)
{
    uint32_t result = operation(argument);
    cleanup(cleanup_argument);
    return result;
}
