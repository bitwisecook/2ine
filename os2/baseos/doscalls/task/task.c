/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Child process termination and waits. */
#include "os2/baseos/doscalls/internal.h"

void setProcessResultCode(PRESULTCODES output, const LxHostChildStatus *status)
{
    if (!output)
        return;
    const ULONG reasons[] = {TC_EXIT, TC_KILLPROCESS, TC_HARDERROR, TC_EXCEPTION};
    output->codeTerminate = reasons[status->reason];
    output->codeResult = status->result;
}

static PID legacyChildPid(USHORT pid)
{
    PID result = pid;
    grabLock(&GMutexDosCalls);
    for (ChildProcess *child = GChildren; child; child = child->next)
        if ((USHORT)lx_host_child_id(child->host) == pid)
        {
            result = lx_host_child_id(child->host);
            break;
        }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET16 Dos16KillProcess(USHORT scope, USHORT pid)
{
    return DosKillProcess(scope, legacyChildPid(pid));
}

APIRET DosKillProcess(ULONG action, PID pid)
{
    TRACE_NATIVE("DosKillProcess(%u, %u)", (unsigned)action, (unsigned)pid);
    if (action > 1)
        return ERROR_INVALID_PARAMETER;
    /* Process-tree termination and unrelated host processes are outside this worker's scope. */
    if (action == 1)
        return ERROR_ACCESS_DENIED;
    APIRET result = ERROR_INVALID_PROCID;
    grabLock(&GMutexDosCalls);
    for (ChildProcess *child = GChildren; child; child = child->next)
        if (lx_host_child_id(child->host) == pid)
        {
            int stopped = lx_host_child_terminate(child->host);
            result = !stopped ? NO_ERROR : stopped > 0 ? ERROR_INVALID_PROCID : ERROR_ACCESS_DENIED;
            break;
        }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET DosWaitChild(ULONG action, ULONG option, PRESULTCODES result, PPID output_pid, PID pid)
{
    TRACE_NATIVE("DosWaitChild(%u, %u, %p, %p, %u)", (uint)action, (uint)option, result, output_pid,
                 (uint)pid);
    if (action != DCWA_PROCESS || (option != DCWW_WAIT && option != DCWW_NOWAIT))
        return ERROR_INVALID_PARAMETER;
    for (;;)
    {
        grabLock(&GMutexDosCalls);
        ChildProcess *child = NULL;
        LxHostChildStatus status;
        int ready = 0;
        for (ChildProcess *item = GChildren; item; item = item->next)
        {
            if (item->waiting || (pid && lx_host_child_id(item->host) != pid))
                continue;
            if (pid)
            {
                child = item;
                break;
            }
            ready = lx_host_child_wait(item->host, 1, &status);
            if (ready)
            {
                child = item;
                break;
            }
        }
        if (!child)
        {
            int any = GChildren != NULL;
            ungrabLock(&GMutexDosCalls);
            if (pid || !any)
                return pid ? ERROR_INVALID_PROCID : ERROR_WAIT_NO_CHILDREN;
            if (option == DCWW_NOWAIT)
                return ERROR_CHILD_NOT_COMPLETE;
            lx_host_sleep_ms(1);
            continue;
        }
        child->waiting = 1;
        ungrabLock(&GMutexDosCalls);
        if (!ready)
            ready = lx_host_child_wait(child->host, option == DCWW_NOWAIT, &status);
        grabLock(&GMutexDosCalls);
        if (ready == 1)
        {
            for (ChildProcess **link = &GChildren; *link; link = &(*link)->next)
                if (*link == child)
                {
                    *link = child->next;
                    break;
                }
        }
        else
            child->waiting = 0;
        ungrabLock(&GMutexDosCalls);
        if (ready != 1)
            return ready ? ERROR_INVALID_PROCID : ERROR_CHILD_NOT_COMPLETE;
        if (output_pid)
            *output_pid = lx_host_child_id(child->host);
        setProcessResultCode(result, &status);
        lx_host_child_release(child->host);
        free(child);
        return NO_ERROR;
    }
}

APIRET16 Dos16CWait(USHORT action, USHORT option, PRESULTCODES16 pres, PUSHORT ppid, USHORT pid)
{
    TRACE_NATIVE("Dos16CWait(%u, %u, %p, %p, %u)", (uint)action, (uint)option, pres, ppid,
                 (uint)pid);
    RESULTCODES res;
    PID cpid;
    APIRET ret = DosWaitChild(action, option, &res, &cpid, pid ? legacyChildPid(pid) : 0);
    if (ret)
        return ret;
    pres->codeTerminate = res.codeTerminate;
    pres->codeResult = res.codeResult;
    *ppid = cpid;
    return ret;
}
