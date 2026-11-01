/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

enum
{
    EXCEPTION_CHAIN_END = UINT32_MAX,
    EXCEPTION_CHAIN_LIMIT = 4096
};
static LxGuestTib *exception_tib(void)
{
    return lx_execution_current_tib(GLoaderState.execution);
}
static BOOL exception_record(uint32 address, uint32 *previous, uint32 *procedure)
{
    uint8 bytes[8];
    if ((address & 3) || !lx_execution_read(GLoaderState.execution, address, bytes, sizeof bytes))
        return FALSE;
    *previous = lx_guest_load_u32(bytes);
    *procedure = lx_guest_load_u32(bytes + 4);
    return TRUE;
}
APIRET DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosSetExceptionHandler(%p)", rec);
    LxGuestTib *tib = exception_tib();
    uint32 address, previous, procedure;
    if (!tib ||
        !lx_execution_address(GLoaderState.execution, rec, 8, LX_GUEST_READ | LX_GUEST_WRITE,
                              &address) ||
        !exception_record(address, &previous, &procedure) ||
        !lx_execution_memory(GLoaderState.execution, procedure, 1, LX_GUEST_EXECUTE))
        return ERROR_INVALID_PARAMETER;
    uint32 head = lx_guest_load_u32(&tib->exception_chain);
    unsigned count = 0;
    for (uint32 entry = head; entry && entry != EXCEPTION_CHAIN_END;)
    {
        uint32 ignored;
        if (entry == address || ++count > EXCEPTION_CHAIN_LIMIT ||
            !exception_record(entry, &entry, &ignored))
            return ERROR_INVALID_PARAMETER;
    }
    lx_guest_store_u32(rec, head);
    lx_guest_store_u32(&tib->exception_chain, address);
    return NO_ERROR;
}
APIRET DosUnsetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosUnsetExceptionHandler(%p)", rec);
    LxGuestTib *tib = exception_tib();
    uint32 address, previous, procedure;
    if (!tib ||
        !lx_execution_address(GLoaderState.execution, rec, 8, LX_GUEST_READ | LX_GUEST_WRITE,
                              &address) ||
        lx_guest_load_u32(&tib->exception_chain) != address ||
        !exception_record(address, &previous, &procedure))
        return ERROR_INVALID_PARAMETER;
    lx_guest_store_u32(&tib->exception_chain, previous);
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
    LxExecution *execution = GLoaderState.execution;
    LxGuestTib *tib = exception_tib();
    uint32 target = (uint32)(uintptr_t)phandler, ip = (uint32)(uintptr_t)pTargetIP;
    uint32 input = (uint32)(uintptr_t)pERepRec;
    uint32 arguments = lx_execution_api_arguments(execution);
    LxExecutionRegisters32 registers;
    if (!tib || !arguments || !lx_execution_api_registers32(execution, &registers))
        return ERROR_NOT_SUPPORTED;
    if (!ip || !lx_execution_memory(execution, ip, 1, LX_GUEST_EXECUTE) || arguments < 4 ||
        !lx_execution_memory(execution, arguments - 4, 4, LX_GUEST_WRITE))
        return ERROR_INVALID_ADDRESS;
    typedef struct
    {
        uint32 address, previous, procedure;
    } UnwindEntry;
    UnwindEntry *entries = calloc(EXCEPTION_CHAIN_LIMIT, sizeof *entries);
    if (!entries)
        return ERROR_NOT_ENOUGH_MEMORY;
    size_t count = 0;
    uint32 head = lx_guest_load_u32(&tib->exception_chain), cursor = head;
    APIRET result = ERROR_INVALID_PARAMETER;
    while (cursor != target && cursor && cursor != EXCEPTION_CHAIN_END)
    {
        if (count == EXCEPTION_CHAIN_LIMIT)
            goto finished;
        for (size_t i = 0; i < count; i++)
            if (entries[i].address == cursor)
                goto finished;
        UnwindEntry *entry = &entries[count++];
        entry->address = cursor;
        if (!exception_record(cursor, &entry->previous, &entry->procedure) ||
            !lx_execution_memory(execution, entry->procedure, 1, LX_GUEST_EXECUTE))
            goto finished;
        cursor = entry->previous;
    }
    if (target && cursor != target)
        goto finished;
    uint8 report[36] = {0};
    if (input)
    {
        if (!lx_execution_read(execution, input, report, sizeof report) ||
            lx_guest_load_u32(report + 16) > 4)
            goto finished;
    }
    else
    {
        lx_guest_store_u32(report, 0xC0000026u); /* XCPT_UNWIND */
        lx_guest_store_u32(report + 12, registers.eip);
    }
    lx_guest_store_u32(report + 4, lx_guest_load_u32(report + 4) | 2 | (target ? 0 : 4));
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
    {
        result = ERROR_NOT_ENOUGH_MEMORY;
        goto finished;
    }
    uint32 base = lx_execution_mapping_address(mapping);
    uint8 *context = (uint8 *)lx_execution_mapping_host(mapping) + 64;
    memset(context, 0, 176);
    lx_guest_store_u32(context, 7); /* CONTROL | INTEGER | SEGMENTS; x87 state is not supplied. */
    const uint32 fields[] = {registers.gs,  registers.fs,     registers.es,  registers.ds,
                             registers.edi, registers.esi,    registers.eax, registers.ebx,
                             registers.ecx, registers.edx,    registers.ebp, registers.eip,
                             registers.cs,  registers.eflags, registers.esp, registers.ss};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
        lx_guest_store_u32(context + 112 + i * 4, fields[i]);
    memcpy(lx_execution_mapping_host(mapping), report, sizeof report);
    result = NO_ERROR;
    for (size_t i = 0; i < count; i++)
    {
        UnwindEntry *entry = &entries[i];
        uint32 disposition = lx_execution_callback32_4(execution, entry->procedure, base,
                                                       entry->address, base + 64, 0);
        if (disposition != 0)
        {
            result = ERROR_INVALID_PARAMETER;
            break;
        }
        lx_guest_store_u32(&tib->exception_chain, entry->previous);
    }
    lx_execution_unmap(execution, mapping);
    if (!result)
    {
        lx_guest_store_u32(&tib->exception_chain, target ? target : EXCEPTION_CHAIN_END);
        uint8 address[4];
        lx_guest_store_u32(address, ip);
        if (!lx_execution_write(execution, arguments - 4, address, sizeof address))
            result = ERROR_INVALID_ADDRESS;
    }
finished:
    free(entries);
    return result;
}
