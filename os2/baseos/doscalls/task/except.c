/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Exception handler chains: DosRaiseException, DosUnwindException and signal focus. */
#include "os2/baseos/doscalls/internal.h"

enum
{
    EXCEPTION_CHAIN_END = UINT32_MAX,
    /* Chains longer than this are treated as corrupt or circular. */
    EXCEPTION_CHAIN_LIMIT = 4096,
    /* Nested DosRaiseException calls a thread may make before failing. */
    EXCEPTION_RAISE_DEPTH_LIMIT = 64
};

/* EXCEPTIONREGISTRATIONRECORD: prev_structure, then ExceptionHandler. */
enum
{
    REGISTRATION_HANDLER_OFFSET = 4,
    REGISTRATION_SIZE = 8,
    REGISTRATION_ALIGNMENT = 4
};

/* EXCEPTIONREPORTRECORD fields; ExceptionInfo holds cParameters ULONGs. */
enum
{
    REPORT_NUMBER_OFFSET = 0,
    REPORT_FLAGS_OFFSET = 4,
    REPORT_NESTED_OFFSET = 8,
    REPORT_ADDRESS_OFFSET = 12,
    REPORT_PARAMETERS_OFFSET = 16,
    EXCEPTION_REPORT_HEADER = 20,
    EXCEPTION_REPORT_SIZE = EXCEPTION_REPORT_HEADER + EXCEPTION_MAXIMUM_PARAMETERS * 4
};

/* CONTEXTRECORD: ContextFlags, the x87 environment and stack, then the general
   registers from ctx_SegGs to ctx_SegSs, each in a ULONG. */
enum
{
    EXCEPTION_CONTEXT_SIZE = 176,
    EXCEPTION_CONTEXT_REGISTERS = 112,
    CONTEXT_SEGGS_OFFSET = 112,
    CONTEXT_SEGFS_OFFSET = 116,
    CONTEXT_SEGES_OFFSET = 120,
    CONTEXT_SEGDS_OFFSET = 124,
    CONTEXT_EDI_OFFSET = 128,
    CONTEXT_ESI_OFFSET = 132,
    CONTEXT_EAX_OFFSET = 136,
    CONTEXT_EBX_OFFSET = 140,
    CONTEXT_ECX_OFFSET = 144,
    CONTEXT_EDX_OFFSET = 148,
    CONTEXT_EBP_OFFSET = 152,
    CONTEXT_EIP_OFFSET = 156,
    CONTEXT_SEGCS_OFFSET = 160,
    CONTEXT_EFLAGS_OFFSET = 164,
    CONTEXT_ESP_OFFSET = 168,
    CONTEXT_SEGSS_OFFSET = 172,
    /* The context follows the report on a 16-byte boundary. */
    CONTEXT_ALIGNMENT = 16,
    /* DosUnwindException places its context after the copied report. */
    UNWIND_CONTEXT_OFFSET = 64
};

enum
{
    RETURN_ADDRESS_SIZE = 4
};

static LxGuestTib *exception_tib(void)
{
    return lx_execution_current_tib(GProcessState.execution);
}

static BOOL exception_record(uint32 address, uint32 *previous, uint32 *procedure)
{
    uint8 bytes[REGISTRATION_SIZE];
    if ((address & (REGISTRATION_ALIGNMENT - 1)) ||
        !lx_execution_read(GProcessState.execution, address, bytes, sizeof bytes))
        return FALSE;
    *previous = lx_guest_load_u32(bytes);
    *procedure = lx_guest_load_u32(bytes + REGISTRATION_HANDLER_OFFSET);
    return TRUE;
}

APIRET DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosSetExceptionHandler(%p)", rec);
    LxGuestTib *tib = exception_tib();
    uint32 address, previous, procedure;
    if (!tib ||
        !lx_execution_address(GProcessState.execution, rec, REGISTRATION_SIZE,
                              LX_GUEST_READ | LX_GUEST_WRITE, &address) ||
        !exception_record(address, &previous, &procedure) ||
        !lx_execution_memory(GProcessState.execution, procedure, 1, LX_GUEST_EXECUTE))
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
        !lx_execution_address(GProcessState.execution, rec, REGISTRATION_SIZE,
                              LX_GUEST_READ | LX_GUEST_WRITE, &address) ||
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
        if (GProcessState.main_module->signal_exception_focus_count == 0)
            return ERROR_ALREADY_RESET;
        GProcessState.main_module->signal_exception_focus_count--;
    }
    else if (flag == 1)
    {
        if (GProcessState.main_module->signal_exception_focus_count == UINT32_MAX)
            return ERROR_NESTING_TOO_DEEP;
        GProcessState.main_module->signal_exception_focus_count++;
    }
    else
    {
    }

    if (pulTimes)
        *pulTimes = GProcessState.main_module->signal_exception_focus_count;

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
    LxExecution *execution = GProcessState.execution;
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
    const uint32 parameters = lx_guest_load_u32(header + REPORT_PARAMETERS_OFFSET);
    const uint64 report_size = EXCEPTION_REPORT_HEADER + (uint64)parameters * sizeof(ULONG);
    const uint64 context_offset =
        (report_size + CONTEXT_ALIGNMENT - 1) & ~(uint64)(CONTEXT_ALIGNMENT - 1);
    /* The mapping also holds a nested report for a failed continuation. */
    const uint64 total = context_offset + EXCEPTION_CONTEXT_SIZE + EXCEPTION_REPORT_SIZE;
    if (total > UINT32_MAX || report_size > UINT32_MAX - input)
        return ERROR_INVALID_PARAMETER;
    if (thread->raise_depth >= EXCEPTION_RAISE_DEPTH_LIMIT)
        return ERROR_NESTING_TOO_DEEP;
    LxGuestMapping *mapping = lx_execution_map(
        execution, 0, (size_t)((total + GUEST_PAGE_SIZE - 1) & ~(uint64)(GUEST_PAGE_SIZE - 1)),
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
    lx_guest_store_u32(report + REPORT_ADDRESS_OFFSET, registers.eip);
    /* EH_NONCONTINUABLE is the only flag a caller may set. */
    uint32 flags = lx_guest_load_u32(report + REPORT_FLAGS_OFFSET) & EH_NONCONTINUABLE;
    if (thread->raised_report)
    {
        flags |= EH_NESTED_CALL;
        lx_guest_store_u32(report + REPORT_NESTED_OFFSET, thread->raised_report);
    }
    lx_guest_store_u32(report + REPORT_FLAGS_OFFSET, flags);
    memset(context, 0, EXCEPTION_CONTEXT_SIZE);
    lx_guest_store_u32(context, CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS);
    registers.eax = NO_ERROR;
    const uint32 fields[] = {registers.gs,  registers.fs,     registers.es,  registers.ds,
                             registers.edi, registers.esi,    registers.eax, registers.ebx,
                             registers.ecx, registers.edx,    registers.ebp, registers.eip,
                             registers.cs,  registers.eflags, registers.esp, registers.ss};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
        lx_guest_store_u32(context + EXCEPTION_CONTEXT_REGISTERS + i * sizeof(ULONG), fields[i]);
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
            if (disposition == XCPT_CONTINUE_EXECUTION)
            {
                if (!((flags | lx_guest_load_u32(current + REPORT_FLAGS_OFFSET)) &
                      EH_NONCONTINUABLE))
                    handled = TRUE;
                else
                {
                    fatal = XCPT_NONCONTINUABLE_EXCEPTION;
                    secondary = TRUE;
                }
                break;
            }
            if (disposition != XCPT_CONTINUE_SEARCH)
            {
                fatal = XCPT_INVALID_DISPOSITION;
                secondary = TRUE;
                break;
            }
            cursor = next;
        }
        if (!secondary || pass)
            break;
        uint8 *nested = context + EXCEPTION_CONTEXT_SIZE;
        memset(nested, 0, EXCEPTION_REPORT_SIZE);
        lx_guest_store_u32(nested + REPORT_NUMBER_OFFSET, fatal);
        lx_guest_store_u32(nested + REPORT_FLAGS_OFFSET, EH_NONCONTINUABLE | EH_NESTED_CALL);
        lx_guest_store_u32(nested + REPORT_NESTED_OFFSET, base);
        lx_guest_store_u32(nested + REPORT_ADDRESS_OFFSET, registers.eip);
        flags = EH_NONCONTINUABLE;
    }
    if (handled)
    {
        const uint32 mask = lx_guest_load_u32(context);
        if (mask & CONTEXT_SEGMENTS)
        {
            registers.gs = (uint16)lx_guest_load_u32(context + CONTEXT_SEGGS_OFFSET);
            registers.fs = (uint16)lx_guest_load_u32(context + CONTEXT_SEGFS_OFFSET);
            registers.es = (uint16)lx_guest_load_u32(context + CONTEXT_SEGES_OFFSET);
            registers.ds = (uint16)lx_guest_load_u32(context + CONTEXT_SEGDS_OFFSET);
        }
        if (mask & CONTEXT_INTEGER)
        {
            registers.edi = lx_guest_load_u32(context + CONTEXT_EDI_OFFSET);
            registers.esi = lx_guest_load_u32(context + CONTEXT_ESI_OFFSET);
            registers.eax = lx_guest_load_u32(context + CONTEXT_EAX_OFFSET);
            registers.ebx = lx_guest_load_u32(context + CONTEXT_EBX_OFFSET);
            registers.ecx = lx_guest_load_u32(context + CONTEXT_ECX_OFFSET);
            registers.edx = lx_guest_load_u32(context + CONTEXT_EDX_OFFSET);
        }
        if (mask & CONTEXT_CONTROL)
        {
            registers.ebp = lx_guest_load_u32(context + CONTEXT_EBP_OFFSET);
            registers.eip = lx_guest_load_u32(context + CONTEXT_EIP_OFFSET);
            registers.cs = (uint16)lx_guest_load_u32(context + CONTEXT_SEGCS_OFFSET);
            registers.eflags = lx_guest_load_u32(context + CONTEXT_EFLAGS_OFFSET);
            registers.esp = lx_guest_load_u32(context + CONTEXT_ESP_OFFSET);
            registers.ss = (uint16)lx_guest_load_u32(context + CONTEXT_SEGSS_OFFSET);
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
    LxExecution *execution = GProcessState.execution;
    LxGuestTib *tib = exception_tib();
    uint32 target = (uint32)(uintptr_t)phandler, ip = (uint32)(uintptr_t)pTargetIP;
    uint32 input = (uint32)(uintptr_t)pERepRec;
    uint32 arguments = lx_execution_api_arguments(execution);
    LxExecutionRegisters32 registers;
    if (!tib || !arguments || !lx_execution_api_registers32(execution, &registers))
        return ERROR_NOT_SUPPORTED;
    if (!ip || !lx_execution_memory(execution, ip, 1, LX_GUEST_EXECUTE) ||
        arguments < RETURN_ADDRESS_SIZE ||
        !lx_execution_memory(execution, arguments - RETURN_ADDRESS_SIZE, RETURN_ADDRESS_SIZE,
                             LX_GUEST_WRITE))
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
    uint8 report[EXCEPTION_REPORT_SIZE] = {0};
    if (input)
    {
        if (!lx_execution_read(execution, input, report, sizeof report) ||
            lx_guest_load_u32(report + REPORT_PARAMETERS_OFFSET) > EXCEPTION_MAXIMUM_PARAMETERS)
            goto finished;
    }
    else
    {
        lx_guest_store_u32(report + REPORT_NUMBER_OFFSET, XCPT_UNWIND);
        lx_guest_store_u32(report + REPORT_ADDRESS_OFFSET, registers.eip);
    }
    /* A null target unwinds the whole chain, which OS/2 reports as an exit unwind. */
    lx_guest_store_u32(report + REPORT_FLAGS_OFFSET,
                       lx_guest_load_u32(report + REPORT_FLAGS_OFFSET) | EH_UNWINDING |
                           (target ? 0 : EH_EXIT_UNWIND));
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, GUEST_PAGE_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
    {
        result = ERROR_NOT_ENOUGH_MEMORY;
        goto finished;
    }
    uint32 base = lx_execution_mapping_address(mapping);
    uint8 *context = (uint8 *)lx_execution_mapping_host(mapping) + UNWIND_CONTEXT_OFFSET;
    memset(context, 0, EXCEPTION_CONTEXT_SIZE);
    /* x87 state is not supplied. */
    lx_guest_store_u32(context, CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS);
    const uint32 fields[] = {registers.gs,  registers.fs,     registers.es,  registers.ds,
                             registers.edi, registers.esi,    registers.eax, registers.ebx,
                             registers.ecx, registers.edx,    registers.ebp, registers.eip,
                             registers.cs,  registers.eflags, registers.esp, registers.ss};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
        lx_guest_store_u32(context + EXCEPTION_CONTEXT_REGISTERS + i * sizeof(ULONG), fields[i]);
    memcpy(lx_execution_mapping_host(mapping), report, sizeof report);
    result = NO_ERROR;
    for (size_t i = 0; i < count; i++)
    {
        UnwindEntry *entry = &entries[i];
        uint32 disposition = lx_execution_callback32_4(
            execution, entry->procedure, base, entry->address, base + UNWIND_CONTEXT_OFFSET, 0);
        if (disposition != XCPT_CONTINUE_SEARCH)
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
        /* Resume at the target by replacing the API call's return address. */
        uint8 address[RETURN_ADDRESS_SIZE];
        lx_guest_store_u32(address, ip);
        if (!lx_execution_write(execution, arguments - RETURN_ADDRESS_SIZE, address,
                                sizeof address))
            result = ERROR_INVALID_ADDRESS;
    }
finished:
    free(entries);
    return result;
}
