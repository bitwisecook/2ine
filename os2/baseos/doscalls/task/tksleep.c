/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

/* A sleep wakes this often to notice a termination request. */
enum
{
    SLEEP_SLICE_MS = 10
};

APIRET DosSleep(ULONG msec)
{
    TRACE_NATIVE("DosSleep(%u)", (uint)msec);

    do
    {
        lx_execution_poll_termination(GProcessState.execution);
        ULONG slice = msec > SLEEP_SLICE_MS ? SLEEP_SLICE_MS : msec;
        lx_host_sleep_ms(slice);
        msec -= slice;
    } while (msec);
    lx_execution_poll_termination(GProcessState.execution);
    return NO_ERROR;
}
