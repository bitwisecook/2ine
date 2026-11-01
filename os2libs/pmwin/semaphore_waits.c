/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM semaphore handles share DOSCALLS resources and timeout values. */
#include "os2libs/pmwin/internal.h"
#include "hal/host_library.h"
static void *semaphore_service(const char *name)
{
    for (LxModule *module = GLoaderState.loaded_modules; module; module = module->next)
        if (module->nativelib && !strcasecmp(module->name, "DOSCALLS"))
            return lx_host_library_symbol(module->nativelib, name);
    return NULL;
}
APIRET WinWaitEventSem(HEV handle, ULONG timeout)
{
    TRACE_NATIVE("WinWaitEventSem(%u, %u)", handle, timeout);
    typedef APIRET(OS2API * WaitFn)(HEV, ULONG);
    WaitFn wait = (WaitFn)semaphore_service("DosWaitEventSem");
    return wait ? wait(handle, timeout) : 50;
}
APIRET WinRequestMutexSem(HMTX handle, ULONG timeout)
{
    TRACE_NATIVE("WinRequestMutexSem(%u, %u)", handle, timeout);
    typedef APIRET(OS2API * WaitFn)(HMTX, ULONG);
    WaitFn wait = (WaitFn)semaphore_service("DosRequestMutexSem");
    return wait ? wait(handle, timeout) : 50;
}
APIRET WinWaitMuxWaitSem(HMUX handle, ULONG timeout, PULONG user)
{
    TRACE_NATIVE("WinWaitMuxWaitSem(%u, %u, %p)", handle, timeout, user);
    typedef APIRET(OS2API * WaitFn)(HMUX, ULONG, PULONG);
    WaitFn wait = (WaitFn)semaphore_service("DosWaitMuxWaitSem");
    return wait ? wait(handle, timeout, user) : 50;
}
