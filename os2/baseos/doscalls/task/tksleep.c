/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosSleep(ULONG msec)
{
    TRACE_NATIVE("DosSleep(%u)", (uint)msec);

    do
    {
        lx_execution_poll_termination(GProcessState.execution);
        ULONG slice = msec > 10 ? 10 : msec;
        lx_host_sleep_ms(slice);
        msec -= slice;
    } while (msec);
    lx_execution_poll_termination(GProcessState.execution);
    return NO_ERROR;
}
