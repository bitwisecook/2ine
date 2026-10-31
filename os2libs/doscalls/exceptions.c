/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

APIRET DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosSetExceptionHandler(%p)", rec);
    return NO_ERROR;
}

APIRET DosUnsetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosUnsetExceptionHandler(%p)", rec);
    return NO_ERROR;
}

APIRET DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes)
{
    TRACE_NATIVE("DosSetSignalExceptionFocus(%u, %p)", (uint)flag, pulTimes);

    if (flag == 0)
    {
        if (GLoaderState.main_module->signal_exception_focus_count == 0)
            return ERROR_ALREADY_RESET;
        GLoaderState.main_module->signal_exception_focus_count--;
    }
    else if (flag == 1)
    {
        if (GLoaderState.main_module->signal_exception_focus_count == 0xFFFFFFFF)
            return ERROR_NESTING_TOO_DEEP;
        GLoaderState.main_module->signal_exception_focus_count++;
    }
    else
    {
    }

    if (pulTimes)
        *pulTimes = GLoaderState.main_module->signal_exception_focus_count;

    return NO_ERROR;
}

APIRET16 Dos16Error(USHORT error)
{
    TRACE_NATIVE("Dos16Error(%u)", (uint)error);
    return (APIRET16)DosError((ULONG)error);
}

APIRET16 Dos16SetVec(USHORT vecnum, PFN routine, PVOID prevaddress)
{
    TRACE_NATIVE("Dos16SetVec(%u, %p, %p)", (uint)vecnum, routine, prevaddress);
    FIXME("do something");
    if (prevaddress)
        lx_guest_store_u32(prevaddress, 0);
    return NO_ERROR;
}

APIRET16 Dos16SetSigHandler(PFN routine, PVOID prevaddress, PUSHORT prevaction, USHORT action,
                            USHORT signumber)
{
    TRACE_NATIVE("Dos16SetSigHandler(%p, %p, %p, %u, %u)", routine, prevaddress, prevaction, action,
                 signumber);
    FIXME("do something");
    if (prevaddress)
        lx_guest_store_u32(prevaddress, 0);
    if (prevaction)
        *prevaction = 0;
    return NO_ERROR;
}

APIRET16 Dos16HoldSignal(USHORT action)
{
    TRACE_NATIVE("Dos16HoldSignal(%d)", action);
    FIXME("stub");
    return NO_ERROR;
}

APIRET DosError(ULONG error)
{
    TRACE_NATIVE("DosError(%u)", (uint)error);
    /* The shim reports ordinary errors rather than host-specific recovery classes. */
    return ERROR_GEN_FAILURE;
}

APIRET DosAcknowledgeSignalException(ULONG ulSignalNum)
{
    TRACE_NATIVE("DosAcknowledgeSignalException(%u)", (uint)ulSignalNum);
    return NO_ERROR;
}

APIRET DosRaiseException(PEXCEPTIONREPORTRECORD pexcept)
{
    TRACE_NATIVE("DosRaiseException(%p)", pexcept);
    FIXME("OS/2 exception dispatch is not implemented");
    return ERROR_NOT_SUPPORTED;
}

APIRET DosUnwindException(PEXCEPTIONREGISTRATIONRECORD phandler, PVOID pTargetIP,
                          PEXCEPTIONREPORTRECORD pERepRec)
{
    TRACE_NATIVE("DosUnwindException(%p, %p, %p)", phandler, pTargetIP, pERepRec);
    FIXME("OS/2 exception unwinding is not implemented");
    return ERROR_NOT_SUPPORTED;
}
