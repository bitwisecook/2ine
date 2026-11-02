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
    EXCEPTION_CHAIN_LIMIT = 4096,
    EXCEPTION_REPORT_HEADER = 20,
    EXCEPTION_CONTEXT_SIZE = 176,
    EXCEPTION_CONTEXT_REGISTERS = 112,
    EXCEPTION_CONTEXT_CONTROL = 1,
    EXCEPTION_CONTEXT_INTEGER = 2,
    EXCEPTION_CONTEXT_SEGMENTS = 4,
    EXCEPTION_NONCONTINUABLE = 1,
    EXCEPTION_NESTED_CALL = 16,
    EXCEPTION_NONCONTINUABLE_NUMBER = 0xC0000024u,
    EXCEPTION_INVALID_DISPOSITION_NUMBER = 0xC0000025u
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
    LxExecution *execution = GLoaderState.execution;
    LxGuestTib *tib = exception_tib();
    LxHostThreadState *thread = lxHostThreadState();
    LxExecutionRegisters32 registers;
    /* This entry receives an encoded guest address, as does DosUnwindException. */
    const uint32 input = (uint32)(uintptr_t)pexcept;
    uint8 header[EXCEPTION_REPORT_HEADER];
    if (!tib || !lx_execution_api_registers32(execution, &registers))
        return ERROR_NOT_SUPPORTED;
    if (!input || !lx_execution_read(execution, input, header, sizeof header))
        return ERROR_INVALID_ADDRESS;
    const uint32 parameters = lx_guest_load_u32(header + 16);
    const uint64 report_size = EXCEPTION_REPORT_HEADER + (uint64)parameters * 4;
    const uint64 context_offset = (report_size + 15) & ~UINT64_C(15);
    const uint64 total = context_offset + EXCEPTION_CONTEXT_SIZE + 36;
    if (total > UINT32_MAX || report_size > UINT32_MAX - input)
        return ERROR_INVALID_PARAMETER;
    if (thread->raise_depth >= 64)
        return ERROR_NESTING_TOO_DEEP;
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, (size_t)((total + 4095) & ~UINT64_C(4095)),
                         LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return ERROR_NOT_ENOUGH_MEMORY;
    uint8 *report = lx_execution_mapping_host(mapping);
    uint8 *context = report + context_offset;
    const uint32 base = lx_execution_mapping_address(mapping);
    if (!lx_execution_read(execution, input, report, (size_t)report_size))
    {
        lx_execution_unmap(execution, mapping);
        return ERROR_INVALID_ADDRESS;
    }
    lx_guest_store_u32(report + 12, registers.eip);
    uint32 flags = lx_guest_load_u32(report + 4) & EXCEPTION_NONCONTINUABLE;
    if (thread->raised_report)
    {
        flags |= EXCEPTION_NESTED_CALL;
        lx_guest_store_u32(report + 8, thread->raised_report);
    }
    lx_guest_store_u32(report + 4, flags);
    memset(context, 0, EXCEPTION_CONTEXT_SIZE);
    lx_guest_store_u32(context, EXCEPTION_CONTEXT_CONTROL | EXCEPTION_CONTEXT_INTEGER |
                                    EXCEPTION_CONTEXT_SEGMENTS);
    registers.eax = NO_ERROR;
    const uint32 fields[] = {registers.gs,  registers.fs,     registers.es,  registers.ds,
                             registers.edi, registers.esi,    registers.eax, registers.ebx,
                             registers.ecx, registers.edx,    registers.ebp, registers.eip,
                             registers.cs,  registers.eflags, registers.esp, registers.ss};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
        lx_guest_store_u32(context + EXCEPTION_CONTEXT_REGISTERS + i * 4, fields[i]);
    uint32 *visited = malloc(EXCEPTION_CHAIN_LIMIT * sizeof *visited);
    if (!visited)
    {
        lx_execution_unmap(execution, mapping);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    uint32 previous_report = thread->raised_report;
    thread->raised_report = base;
    thread->raise_depth++;
    APIRET result = NO_ERROR;
    BOOL handled = FALSE;
    uint32 fatal = lx_guest_load_u32(report);
    /* A rejected continuation or invalid disposition is itself a noncontinuable
       exception. Its nested record retains the original report and parameters. */
    for (unsigned pass = 0; pass < 2 && !handled; pass++)
    {
        uint8 *current = pass ? context + EXCEPTION_CONTEXT_SIZE : report;
        uint32 report_address = base + (uint32)(current - report);
        uint32 cursor = lx_guest_load_u32(&tib->exception_chain);
        unsigned count = 0;
        BOOL secondary = FALSE;
        while (cursor && cursor != EXCEPTION_CHAIN_END)
        {
            uint32 next, procedure;
            if (count == EXCEPTION_CHAIN_LIMIT)
            {
                result = ERROR_INVALID_PARAMETER;
                break;
            }
            for (unsigned i = 0; i < count; i++)
                if (visited[i] == cursor)
                {
                    result = ERROR_INVALID_PARAMETER;
                    break;
                }
            if (result || !exception_record(cursor, &next, &procedure) ||
                !lx_execution_memory(execution, procedure, 1, LX_GUEST_EXECUTE))
            {
                result = ERROR_INVALID_PARAMETER;
                break;
            }
            visited[count++] = cursor;
            uint32 disposition = lx_execution_callback32_4(
                execution, procedure, report_address, cursor, base + (uint32)context_offset, 0);
            if (disposition == UINT32_MAX)
            {
                if (!((flags | lx_guest_load_u32(current + 4)) & EXCEPTION_NONCONTINUABLE))
                    handled = TRUE;
                else
                {
                    fatal = EXCEPTION_NONCONTINUABLE_NUMBER;
                    secondary = TRUE;
                }
                break;
            }
            if (disposition != 0)
            {
                fatal = EXCEPTION_INVALID_DISPOSITION_NUMBER;
                secondary = TRUE;
                break;
            }
            cursor = next;
        }
        if (!secondary || pass)
            break;
        uint8 *nested = context + EXCEPTION_CONTEXT_SIZE;
        memset(nested, 0, 36);
        lx_guest_store_u32(nested, fatal);
        lx_guest_store_u32(nested + 4, EXCEPTION_NONCONTINUABLE | EXCEPTION_NESTED_CALL);
        lx_guest_store_u32(nested + 8, base);
        lx_guest_store_u32(nested + 12, registers.eip);
        flags = EXCEPTION_NONCONTINUABLE;
    }
    if (handled)
    {
        const uint32 mask = lx_guest_load_u32(context);
        if (mask & EXCEPTION_CONTEXT_SEGMENTS)
        {
            registers.gs = (uint16)lx_guest_load_u32(context + 112);
            registers.fs = (uint16)lx_guest_load_u32(context + 116);
            registers.es = (uint16)lx_guest_load_u32(context + 120);
            registers.ds = (uint16)lx_guest_load_u32(context + 124);
        }
        if (mask & EXCEPTION_CONTEXT_INTEGER)
        {
            registers.edi = lx_guest_load_u32(context + 128);
            registers.esi = lx_guest_load_u32(context + 132);
            registers.eax = lx_guest_load_u32(context + 136);
            registers.ebx = lx_guest_load_u32(context + 140);
            registers.ecx = lx_guest_load_u32(context + 144);
            registers.edx = lx_guest_load_u32(context + 148);
        }
        if (mask & EXCEPTION_CONTEXT_CONTROL)
        {
            registers.ebp = lx_guest_load_u32(context + 152);
            registers.eip = lx_guest_load_u32(context + 156);
            registers.cs = (uint16)lx_guest_load_u32(context + 160);
            registers.eflags = lx_guest_load_u32(context + 164);
            registers.esp = lx_guest_load_u32(context + 168);
            registers.ss = (uint16)lx_guest_load_u32(context + 172);
        }
        if (!lx_execution_api_resume32(execution, &registers))
            result = ERROR_INVALID_ADDRESS;
    }
    thread->raise_depth--;
    thread->raised_report = previous_report;
    free(visited);
    lx_execution_unmap(execution, mapping);
    if (!handled && !result)
        DosExit(EXIT_PROCESS, fatal);
    return result;
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
