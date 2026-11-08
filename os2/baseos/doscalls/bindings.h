/* Public domain. No rights reserved. Wholly AI-authored. */
/* DOSCALLS flat 32-bit ABI. Host pointer outputs use private temporary storage. */
static uint32 bridge32_DosQueryAppType(uint8 *arguments)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(arguments), 32768);
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if (name && lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(arguments + 4), 4,
                                    LX_GUEST_WRITE, &output))
    {
        ULONG flags = 0;
        result = DosQueryAppType(name, &flags);
        if (!result)
            lx_guest_store_u32(output.host, flags);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosAllocMem(uint8 *arguments)
{
    const uint32 output = lx_guest_load_u32(arguments);
    const uint32 size = lx_guest_load_u32(arguments + 4);
    const uint32 flags = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow slot;
    if (!lx_execution_borrow(GProcessState.execution, output, 4, LX_GUEST_WRITE, &slot))
        return ERROR_INVALID_PARAMETER;
    void *memory = NULL;
    uint32 result = DosAllocMem(&memory, size, flags);
    if (!result)
    {
        uint32 address;
        if (lx_execution_address(GProcessState.execution, memory, 0, 0, &address))
            lx_guest_store_u32(slot.host, address);
        else
            result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&slot);
    return result;
}

static uint32 bridge32_DosQuerySysInfo(uint8 *arguments)
{
    const uint32 first = lx_guest_load_u32(arguments), last = lx_guest_load_u32(arguments + 4);
    const uint32 pointer = lx_guest_load_u32(arguments + 8),
                 capacity = lx_guest_load_u32(arguments + 12);
    if (last < first || first < QSV_MAX_PATH_LENGTH || last > QSV_FOREGROUND_PROCESS)
        return ERROR_INVALID_PARAMETER;
    const uint32 count = last - first + 1;
    if (capacity < count * 4)
        return ERROR_BUFFER_OVERFLOW;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, pointer, count * 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 values[QSV_FOREGROUND_PROCESS];
    const uint32 result = DosQuerySysInfo(first, last, values, count * 4);
    if (!result)
        for (uint32 i = 0; i < count; i++)
            lx_guest_store_u32((uint8 *)output.host + i * 4, values[i]);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosWrite(uint8 *arguments)
{
    const uint32 handle = lx_guest_load_u32(arguments), pointer = lx_guest_load_u32(arguments + 4);
    const uint32 size = lx_guest_load_u32(arguments + 8),
                 actual_pointer = lx_guest_load_u32(arguments + 12);
    LxGuestBorrow data = {0}, actual = {0};
    if ((size &&
         !lx_execution_borrow(GProcessState.execution, pointer, size, LX_GUEST_READ, &data)) ||
        !lx_execution_borrow(GProcessState.execution, actual_pointer, 4, LX_GUEST_WRITE, &actual))
    {
        lx_execution_release(&data);
        return ERROR_INVALID_PARAMETER;
    }
    uint32 written = 0;
    const uint32 result = DosWrite(handle, data.host, size, &written);
    lx_guest_store_u32(actual.host, written);
    lx_execution_release(&actual);
    lx_execution_release(&data);
    return result;
}

static uint32 bridge32_DosRead(uint8 *arguments)
{
    const uint32 handle = lx_guest_load_u32(arguments), pointer = lx_guest_load_u32(arguments + 4);
    const uint32 size = lx_guest_load_u32(arguments + 8),
                 actual_pointer = lx_guest_load_u32(arguments + 12);
    LxGuestBorrow data = {0}, actual = {0};
    if ((size &&
         !lx_execution_borrow(GProcessState.execution, pointer, size, LX_GUEST_WRITE, &data)) ||
        !lx_execution_borrow(GProcessState.execution, actual_pointer, 4, LX_GUEST_WRITE, &actual))
    {
        lx_execution_release(&data);
        return ERROR_INVALID_PARAMETER;
    }
    uint32 count = 0;
    const uint32 result = DosRead(handle, data.host, size, &count);
    lx_guest_store_u32(actual.host, count);
    lx_execution_release(&actual);
    lx_execution_release(&data);
    return result;
}

static uint32 bridge32_DosClose(uint8 *arguments)
{
    return DosClose(lx_guest_load_u32(arguments));
}

static uint32 bridge32_DosFreeMem(uint8 *arguments)
{
    void *p = lx_execution_memory(GProcessState.execution, lx_guest_load_u32(arguments), 0, 0);
    return p ? DosFreeMem(p) : ERROR_INVALID_ADDRESS;
}

static uint32 bridge32_DosExit(uint8 *arguments)
{
    DosExit(lx_guest_load_u32(arguments), lx_guest_load_u32(arguments + 4));
    return 0;
}

static uint32 bridge32_DosSetRelMaxFH(uint8 *arguments)
{
    const uint32 request_pointer = lx_guest_load_u32(arguments),
                 current_pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow request = {0}, current = {0};
    if ((request_pointer &&
         !lx_execution_borrow(GProcessState.execution, request_pointer, 4, 3, &request)) ||
        (current_pointer && !lx_execution_borrow(GProcessState.execution, current_pointer, 4,
                                                 LX_GUEST_WRITE, &current)))
    {
        lx_execution_release(&request);
        return ERROR_INVALID_PARAMETER;
    }
    LONG requested = request.host ? (LONG)lx_guest_load_u32(request.host) : 0;
    ULONG count = 0;
    const uint32 result =
        DosSetRelMaxFH(request.host ? &requested : NULL, current.host ? &count : NULL);
    if (request.host)
        lx_guest_store_u32(request.host, (uint32)requested);
    if (current.host)
        lx_guest_store_u32(current.host, count);
    lx_execution_release(&request);
    lx_execution_release(&current);
    return result;
}

static uint32 bridge32_DosGetInfoBlocks(uint8 *arguments)
{
    const uint32 tib_pointer = lx_guest_load_u32(arguments),
                 pib_pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow tib = {0}, pib = {0};
    if ((tib_pointer &&
         !lx_execution_borrow(GProcessState.execution, tib_pointer, 4, LX_GUEST_WRITE, &tib)) ||
        (pib_pointer &&
         !lx_execution_borrow(GProcessState.execution, pib_pointer, 4, LX_GUEST_WRITE, &pib)))
    {
        lx_execution_release(&tib);
        return ERROR_INVALID_PARAMETER;
    }
    uint32 address;
    uint32 result = NO_ERROR;
    if (tib.host)
    {
        if (lx_execution_address(GProcessState.execution, GProcessState.getOs2Tib(),
                                 sizeof(LxGuestTib), 0, &address))
            lx_guest_store_u32(tib.host, address);
        else
            result = ERROR_INVALID_ADDRESS;
    }
    if (pib.host)
        lx_guest_store_u32(pib.host, lx_execution_mapping_address(GProcessState.guest_pib));
    lx_execution_release(&tib);
    lx_execution_release(&pib);
    return result;
}

static uint32 bridge32_DosDevConfig(uint8 *arguments)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(arguments), 1,
                             LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosDevConfig(output.host, lx_guest_load_u32(arguments + 4));
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosQueryHType(uint8 *arguments)
{
    const uint32 type_pointer = lx_guest_load_u32(arguments + 4),
                 attr_pointer = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow type = {0}, attr = {0};
    if ((type_pointer &&
         !lx_execution_borrow(GProcessState.execution, type_pointer, 4, LX_GUEST_WRITE, &type)) ||
        (attr_pointer &&
         !lx_execution_borrow(GProcessState.execution, attr_pointer, 4, LX_GUEST_WRITE, &attr)))
    {
        lx_execution_release(&type);
        return ERROR_INVALID_PARAMETER;
    }
    ULONG type_value = 0, attr_value = 0;
    const uint32 result = DosQueryHType(lx_guest_load_u32(arguments), &type_value, &attr_value);
    if (!result)
    {
        if (type.host)
            lx_guest_store_u32(type.host, type_value);
        if (attr.host)
            lx_guest_store_u32(attr.host, attr_value);
    }
    lx_execution_release(&type);
    lx_execution_release(&attr);
    return result;
}

static uint32 bridge32_DosExitList(uint8 *arguments)
{
    return DosExitList(lx_guest_load_u32(arguments),
                       (PFNEXITLIST)(uintptr_t)lx_guest_load_u32(arguments + 4));
}

static uint32 bridge32_DosSetSignalExceptionFocus(uint8 *arguments)
{
    const uint32 pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow count = {0};
    if (pointer &&
        !lx_execution_borrow(GProcessState.execution, pointer, 4, LX_GUEST_WRITE, &count))
        return ERROR_INVALID_PARAMETER;
    ULONG value = 0;
    const uint32 result =
        DosSetSignalExceptionFocus(lx_guest_load_u32(arguments), count.host ? &value : NULL);
    if (!result && count.host)
        lx_guest_store_u32(count.host, value);
    lx_execution_release(&count);
    return result;
}

static uint32 bridge32_DosSetExceptionHandler(uint8 *arguments)
{
    LxGuestBorrow record;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(arguments), 8, 3, &record))
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosSetExceptionHandler(record.host);
    lx_execution_release(&record);
    return result;
}

static uint32 bridge32_DosUnsetExceptionHandler(uint8 *arguments)
{
    LxGuestBorrow record;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(arguments), 8, 3, &record))
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosUnsetExceptionHandler(record.host);
    lx_execution_release(&record);
    return result;
}

static uint32 bridge32_DosLoadModule(uint8 *arguments)
{
    const uint32 error_pointer = lx_guest_load_u32(arguments),
                 error_size = lx_guest_load_u32(arguments + 4);
    const uint32 name_pointer = lx_guest_load_u32(arguments + 8),
                 module_pointer = lx_guest_load_u32(arguments + 12);
    char *name = lx_execution_copy_string(GProcessState.execution, name_pointer, CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow error = {0}, module = {0};
    if ((error_size && !lx_execution_borrow(GProcessState.execution, error_pointer, error_size,
                                            LX_GUEST_WRITE, &error)) ||
        !lx_execution_borrow(GProcessState.execution, module_pointer, 4, LX_GUEST_WRITE, &module))
    {
        lx_execution_release(&error);
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    char unused = 0;
    HMODULE value = 0;
    const uint32 result =
        DosLoadModule(error.host ? error.host : &unused, error_size, name, &value);
    lx_guest_store_u32(module.host, value);
    lx_execution_release(&module);
    lx_execution_release(&error);
    free(name);
    return result;
}

static uint32 bridge32_DosSetCurrentDir(uint8 *arguments)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(arguments), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    uint32 result = DosSetCurrentDir(name);
    free(name);
    return result;
}

static uint32 bridge32_DosQueryProcAddr(uint8 *arguments)
{
    const uint32 module = lx_guest_load_u32(arguments), ordinal = lx_guest_load_u32(arguments + 4);
    const uint32 name_pointer = lx_guest_load_u32(arguments + 8),
                 output_pointer = lx_guest_load_u32(arguments + 12);
    char *name = NULL;
    if (!ordinal)
    {
        name = lx_execution_copy_string(GProcessState.execution, name_pointer, 256);
        if (!name)
            return ERROR_INVALID_PARAMETER;
    }
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, output_pointer, 4, LX_GUEST_WRITE, &output))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    PFN function = NULL;
    uint32 result = DosQueryProcAddr(module, ordinal, name, &function);
    if (!result)
    {
        uint32 entry;
        if (lx_execution_entry_address(GProcessState.execution, (void *)function, &entry))
            lx_guest_store_u32(output.host, entry);
        else
            result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosFreeModule(uint8 *arguments)
{
    return DosFreeModule(lx_guest_load_u32(arguments));
}

static uint32 wireCreateSemaphore(uint8 *arguments, int mutex)
{
    const uint32 name_pointer = lx_guest_load_u32(arguments),
                 output_pointer = lx_guest_load_u32(arguments + 4);
    char *name = name_pointer
                     ? lx_execution_copy_string(GProcessState.execution, name_pointer, CCHMAXPATH)
                     : NULL;
    if (name_pointer && !name)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, output_pointer, 4, LX_GUEST_WRITE, &output))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    uint32 handle = 0, attrs = lx_guest_load_u32(arguments + 8),
           state = lx_guest_load_u32(arguments + 12);
    const uint32 result = mutex ? DosCreateMutexSem(name, &handle, attrs, state)
                                : DosCreateEventSem(name, &handle, attrs, state);
    if (!result)
        lx_guest_store_u32(output.host, handle);
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosCreateMutexSem(uint8 *a)
{
    return wireCreateSemaphore(a, 1);
}

static uint32 bridge32_DosCreateEventSem(uint8 *a)
{
    return wireCreateSemaphore(a, 0);
}

static uint32 wireOpenSemaphore(uint8 *arguments, int mutex)
{
    const uint32 name_pointer = lx_guest_load_u32(arguments),
                 output_pointer = lx_guest_load_u32(arguments + 4);
    char *name = name_pointer
                     ? lx_execution_copy_string(GProcessState.execution, name_pointer, CCHMAXPATH)
                     : NULL;
    if (name_pointer && !name)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, output_pointer, 4, LX_GUEST_WRITE, &output))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    uint32 handle = 0;
    const uint32 result = mutex ? DosOpenMutexSem(name, &handle) : DosOpenEventSem(name, &handle);
    if (!result)
        lx_guest_store_u32(output.host, handle);
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosOpenMutexSem(uint8 *a)
{
    return wireOpenSemaphore(a, 1);
}

static uint32 bridge32_DosOpenEventSem(uint8 *a)
{
    return wireOpenSemaphore(a, 0);
}

static uint32 bridge32_DosRequestMutexSem(uint8 *a)
{
    return DosRequestMutexSem(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_DosReleaseMutexSem(uint8 *a)
{
    return DosReleaseMutexSem(lx_guest_load_u32(a));
}

static uint32 bridge32_DosCloseMutexSem(uint8 *a)
{
    return DosCloseMutexSem(lx_guest_load_u32(a));
}

static uint32 bridge32_DosPostEventSem(uint8 *a)
{
    return DosPostEventSem(lx_guest_load_u32(a));
}

static uint32 bridge32_DosKillProcess(uint8 *a)
{
    return DosKillProcess(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_DosWaitChild(uint8 *a)
{
    LxGuestBorrow result = {0}, pid = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 8, LX_GUEST_WRITE,
                             &result) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                             &pid))
    {
        lx_execution_release(&result);
        lx_execution_release(&pid);
        return ERROR_INVALID_PARAMETER;
    }
    RESULTCODES status = {0};
    PID process = 0;
    uint32 rc = DosWaitChild(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), &status, &process,
                             lx_guest_load_u32(a + 16));
    if (!rc)
    {
        lx_guest_store_u32(result.host, status.codeTerminate);
        lx_guest_store_u32((uint8 *)result.host + 4, status.codeResult);
        lx_guest_store_u32(pid.host, process);
    }
    lx_execution_release(&result);
    lx_execution_release(&pid);
    return rc;
}

static uint32 bridge32_DosCloseEventSem(uint8 *a)
{
    return DosCloseEventSem(lx_guest_load_u32(a));
}

static uint32 bridge32_DosWaitEventSem(uint8 *a)
{
    return DosWaitEventSem(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 wireQueryEvent(uint8 *arguments, int reset)
{
    const uint32 pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow output = {0};
    if (pointer &&
        !lx_execution_borrow(GProcessState.execution, pointer, 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 count = 0;
    const uint32 result = reset ? DosResetEventSem(lx_guest_load_u32(arguments), &count)
                                : DosQueryEventSem(lx_guest_load_u32(arguments), &count);
    if (output.host && (!result || result == ERROR_ALREADY_RESET))
        lx_guest_store_u32(output.host, count);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosResetEventSem(uint8 *a)
{
    return wireQueryEvent(a, 1);
}

static uint32 bridge32_DosQueryEventSem(uint8 *a)
{
    return wireQueryEvent(a, 0);
}

static uint32 bridge32_DosQueryModuleName(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 4);
    LxGuestBorrow output;
    if (!size || !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), size,
                                      LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosQueryModuleName(lx_guest_load_u32(a), size, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosQueryModuleHandle(uint8 *a)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    HMODULE value = 0;
    const uint32 result = DosQueryModuleHandle(name, &value);
    if (!result)
        lx_guest_store_u32(output.host, value);
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosGetDateTime(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 11, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    DATETIME value;
    const uint32 result = DosGetDateTime(&value);
    if (!result)
    {
        uint8 *p = output.host;
        p[0] = value.hours;
        p[1] = value.minutes;
        p[2] = value.seconds;
        p[3] = value.hundredths;
        p[4] = value.day;
        p[5] = value.month;
        lx_guest_store_u16(p + 6, value.year);
        lx_guest_store_u16(p + 8, value.timezone);
        p[10] = value.weekday;
    }
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosSetDateTime(uint8 *a)
{
    uint8 p[11];
    if (!lx_execution_read(GProcessState.execution, lx_guest_load_u32(a), p, sizeof p))
        return ERROR_INVALID_PARAMETER;
    DATETIME value = {0};
    value.hours = p[0];
    value.minutes = p[1];
    value.seconds = p[2];
    value.hundredths = p[3];
    value.day = p[4];
    value.month = p[5];
    value.year = lx_guest_load_u16(p + 6);
    value.timezone = (int16_t)lx_guest_load_u16(p + 8);
    value.weekday = p[10];
    return DosSetDateTime(&value);
}

static uint32 bridge32_DosOpen(uint8 *a)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    if (lx_guest_load_u32(a + 28))
    {
        free(name);
        return ERROR_OPEN_FAILED;
    }
    LxGuestBorrow handle = {0}, action = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &handle) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE,
                             &action))
    {
        lx_execution_release(&handle);
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    HFILE hf = 0;
    ULONG taken = 0;
    const uint32 result =
        DosOpen(name, &hf, &taken, lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16),
                lx_guest_load_u32(a + 20), lx_guest_load_u32(a + 24), NULL);
    if (!result)
    {
        lx_guest_store_u32(handle.host, hf);
        lx_guest_store_u32(action.host, taken);
    }
    lx_execution_release(&handle);
    lx_execution_release(&action);
    free(name);
    return result;
}

static uint32 bridge32_DosSetFilePtr(uint8 *a)
{
    LxGuestBorrow output = {0};
    const uint32 pointer = lx_guest_load_u32(a + 12);
    if (pointer &&
        !lx_execution_borrow(GProcessState.execution, pointer, 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    ULONG position = 0;
    const uint32 result = DosSetFilePtr(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                        lx_guest_load_u32(a + 8), &position);
    if (!result && output.host)
        lx_guest_store_u32(output.host, position);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosDelete(uint8 *a)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosDelete(name);
    free(name);
    return result;
}

static void wireDateTime(uint8 *p, FDATE date, FTIME time)
{
    lx_guest_store_u16(p, date.day | date.month << 5 | date.year << 9);
    lx_guest_store_u16(p + 2, time.twosecs | time.minutes << 5 | time.hours << 11);
}

static uint32 wireQueryFileInfo(uint8 *a, int path)
{
    const uint32 level = lx_guest_load_u32(a + 4), pointer = lx_guest_load_u32(a + 8);
    const uint32 size = lx_guest_load_u32(a + 12);
    char *name =
        path ? lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH)
             : NULL;
    if (path && !name)
        return ERROR_INVALID_PARAMETER;
    if (level != FIL_STANDARD && level != FIL_QUERYEASIZE &&
        !(path && (level == FIL_QUERYFULLNAME || level == 7)))
    {
        free(name);
        return ERROR_INVALID_LEVEL;
    }
    const uint32 wire_size = level == FIL_STANDARD      ? (size < 24 ? size : 24)
                             : level == FIL_QUERYEASIZE ? 28
                                                        : size;
    if (size < wire_size || !wire_size || (level == FIL_STANDARD && size < 22))
    {
        free(name);
        return ERROR_BUFFER_OVERFLOW;
    }
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, pointer, wire_size, LX_GUEST_WRITE, &output))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    FILESTATUS4 value = {0};
    void *data = (level == FIL_STANDARD || level == FIL_QUERYEASIZE) ? (void *)&value : output.host;
    const uint32 capacity = data == output.host ? size : sizeof value;
    const uint32 result = path ? DosQueryPathInfo(name, level, data, capacity)
                               : DosQueryFileInfo(lx_guest_load_u32(a), level, data, capacity);
    if (!result && data != output.host)
    {
        uint8 wire[28] = {0};
        uint8 *p = wire;
        wireDateTime(p, value.fdateCreation, value.ftimeCreation);
        wireDateTime(p + 4, value.fdateLastAccess, value.ftimeLastAccess);
        wireDateTime(p + 8, value.fdateLastWrite, value.ftimeLastWrite);
        lx_guest_store_u32(p + 12, value.cbFile);
        lx_guest_store_u32(p + 16, value.cbFileAlloc);
        lx_guest_store_u32(p + 20, value.attrFile);
        if (level == FIL_QUERYEASIZE)
            lx_guest_store_u32(p + 24, value.cbList);
        memcpy(output.host, wire, wire_size);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosQueryFileInfo(uint8 *a)
{
    return wireQueryFileInfo(a, 0);
}

static uint32 bridge32_DosQueryPathInfo(uint8 *a)
{
    return wireQueryFileInfo(a, 1);
}

static uint32 wireFind(uint8 *a, int first)
{
    const uint32 pointer = lx_guest_load_u32(a + (first ? 12 : 4));
    const uint32 size = lx_guest_load_u32(a + (first ? 16 : 8));
    if (size > 0xFFFF)
        return ERROR_INVALID_PARAMETER;
    const uint32 count_pointer = lx_guest_load_u32(a + (first ? 20 : 12));
    const uint32 level = first ? lx_guest_load_u32(a + 24) : 0;
    char *name =
        first ? lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH)
              : NULL;
    if (first && !name)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow handle = {0}, data = {0}, count = {0};
    if ((first &&
         !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, 3, &handle)) ||
        (size &&
         !lx_execution_borrow(GProcessState.execution, pointer, size, LX_GUEST_WRITE, &data)) ||
        !lx_execution_borrow(GProcessState.execution, count_pointer, 4, 3, &count))
    {
        lx_execution_release(&handle);
        lx_execution_release(&data);
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    ULONG matches = lx_guest_load_u32(count.host);
    HDIR value = first ? lx_guest_load_u32(handle.host) : lx_guest_load_u32(a);
    const uint32 result = first ? DosFindFirst(name, &value, lx_guest_load_u32(a + 8), data.host,
                                               size, &matches, level)
                                : DosFindNext(value, data.host, size, &matches);
    lx_guest_store_u32(count.host, matches);
    if (!result && handle.host)
        lx_guest_store_u32(handle.host, value);
    lx_execution_release(&handle);
    lx_execution_release(&data);
    lx_execution_release(&count);
    free(name);
    return result;
}

static uint32 bridge32_DosFindFirst(uint8 *a)
{
    return wireFind(a, 1);
}

static uint32 bridge32_DosFindNext(uint8 *a)
{
    return wireFind(a, 0);
}

static uint32 bridge32_DosFindClose(uint8 *a)
{
    return DosFindClose(lx_guest_load_u32(a));
}

static uint32 bridge32_DosQueryCurrentDisk(uint8 *a)
{
    LxGuestBorrow disk = {0}, logical = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE,
                             &disk) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &logical))
    {
        lx_execution_release(&disk);
        return ERROR_INVALID_PARAMETER;
    }
    ULONG drive = 0, mask = 0;
    const uint32 result = DosQueryCurrentDisk(&drive, &mask);
    if (!result)
    {
        lx_guest_store_u32(disk.host, drive);
        lx_guest_store_u32(logical.host, mask);
    }
    lx_execution_release(&disk);
    lx_execution_release(&logical);
    return result;
}

static uint32 bridge32_DosQueryCurrentDir(uint8 *a)
{
    LxGuestBorrow length = {0}, data = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 4, 3, &length))
        return ERROR_INVALID_PARAMETER;
    ULONG size = lx_guest_load_u32(length.host);
    if (size && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), size,
                                     LX_GUEST_WRITE, &data))
    {
        lx_execution_release(&length);
        return ERROR_INVALID_PARAMETER;
    }
    const uint32 result = DosQueryCurrentDir(lx_guest_load_u32(a), data.host, &size);
    lx_guest_store_u32(length.host, size);
    lx_execution_release(&data);
    lx_execution_release(&length);
    return result;
}

static uint32 bridge32_DosQueryCp(uint8 *a)
{
    const uint32 capacity = lx_guest_load_u32(a);
    const uint32 count = capacity / 4 > 3 ? 3 : capacity / 4;
    LxGuestBorrow list = {0}, length = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE,
                             &length) ||
        (count && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), count * 4,
                                       LX_GUEST_WRITE, &list)))
    {
        lx_execution_release(&length);
        return ERROR_INVALID_PARAMETER;
    }
    ULONG values[3] = {0}, size = 0;
    const uint32 result = DosQueryCp(count * 4, values, &size);
    for (uint32 i = 0; i < count; i++)
        lx_guest_store_u32((uint8 *)list.host + i * 4, values[i]);
    lx_guest_store_u32(length.host, size);
    lx_execution_release(&list);
    lx_execution_release(&length);
    return result;
}

static uint32 bridge32_DosSetFileSize(uint8 *a)
{
    return DosSetFileSize(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_DosError(uint8 *a)
{
    return DosError(lx_guest_load_u32(a));
}

static uint32 bridge32_DosAcknowledgeSignalException(uint8 *a)
{
    return DosAcknowledgeSignalException(lx_guest_load_u32(a));
}

static uint32 bridge32_DosAllocThreadLocalMemory(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    ULONG *address = NULL;
    const uint32 result = DosAllocThreadLocalMemory(lx_guest_load_u32(a), &address);
    if (!result)
        lx_guest_store_u32(output.host, (uint32)(uintptr_t)address);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosFreeThreadLocalMemory(uint8 *a)
{
    return DosFreeThreadLocalMemory((ULONG *)(uintptr_t)lx_guest_load_u32(a));
}

static uint32 bridge32_DosQueryMem(uint8 *a)
{
    const uint32 address = lx_guest_load_u32(a);
    LxGuestBorrow memory = {0}, size = {0}, flags = {0};
    if (!lx_execution_borrow(GProcessState.execution, address, 1, 0, &memory))
        return ERROR_INVALID_ADDRESS;
    uint32 result = ERROR_INVALID_PARAMETER;
    if (lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4,
                            LX_GUEST_READ | LX_GUEST_WRITE, &size) &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE,
                            &flags))
    {
        ULONG count = lx_guest_load_u32(size.host), attributes = 0;
        if (count)
        {
            const size_t remaining = lx_execution_mapping_size(memory.mapping) -
                                     (address - lx_execution_mapping_address(memory.mapping));
            if (count > remaining)
                count = (ULONG)remaining;
            result = DosQueryMem(memory.host, &count, &attributes);
            if (!result)
            {
                lx_guest_store_u32(size.host, count);
                lx_guest_store_u32(flags.host, attributes);
            }
        }
    }
    lx_execution_release(&flags);
    lx_execution_release(&size);
    lx_execution_release(&memory);
    return result;
}

static uint32 bridge32_DosSetMem(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 4);
    LxGuestBorrow memory;
    if (!size ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), size, 0, &memory))
        return ERROR_INVALID_ADDRESS;
    const uint32 result = DosSetMem(memory.host, size, lx_guest_load_u32(a + 8));
    lx_execution_release(&memory);
    return result;
}

static uint32 bridge32_DosScanEnv(uint8 *a)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    char *value = NULL;
    uint32 result = DosScanEnv(name, &value), address;
    if (!result)
    {
        if (lx_execution_address(GProcessState.execution, value, 1, LX_GUEST_READ, &address))
            lx_guest_store_u32(output.host, address);
        else
            result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosSearchPath(uint8 *a)
{
    char *path = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 4), 65536);
    char *file =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 8), CCHMAXPATH);
    const uint32 size = lx_guest_load_u32(a + 16);
    LxGuestBorrow output = {0};
    if (!path || !file || !size ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), size,
                             LX_GUEST_WRITE, &output))
    {
        free(path);
        free(file);
        return ERROR_INVALID_PARAMETER;
    }
    const uint32 result = DosSearchPath(lx_guest_load_u32(a), path, file, output.host, size);
    lx_execution_release(&output);
    free(path);
    free(file);
    return result;
}

static uint32 bridge32_DosWaitNPipe(uint8 *a)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosWaitNPipe(name, lx_guest_load_u32(a + 4));
    free(name);
    return result;
}

static uint32 bridge32_DosForceDelete(uint8 *a)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosForceDelete(name);
    free(name);
    return result;
}

static uint32 bridge32_DosQueryFHState(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    ULONG mode = 0;
    const uint32 result = DosQueryFHState(lx_guest_load_u32(a), &mode);
    if (!result)
        lx_guest_store_u32(output.host, mode);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosResetBuffer(uint8 *a)
{
    return DosResetBuffer(lx_guest_load_u32(a));
}

static uint32 bridge32_DosSleep(uint8 *a)
{
    return DosSleep(lx_guest_load_u32(a));
}

static uint32 bridge32_DosSetDefaultDisk(uint8 *a)
{
    return DosSetDefaultDisk(lx_guest_load_u32(a));
}

static uint32 wireSharedMemory(uint8 *a, int create)
{
    uint32 name_pointer = lx_guest_load_u32(a + 4);
    char *name = name_pointer
                     ? lx_execution_copy_string(GProcessState.execution, name_pointer, CCHMAXPATH)
                     : NULL;
    if (name_pointer && !name)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE,
                             &output))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    void *memory = NULL;
    uint32 result = create ? DosAllocSharedMem(&memory, name, lx_guest_load_u32(a + 8),
                                               lx_guest_load_u32(a + 12))
                           : DosGetNamedSharedMem(&memory, name, lx_guest_load_u32(a + 8));
    uint32 address;
    if (!result)
    {
        if (lx_execution_address(GProcessState.execution, memory, 1, 0, &address))
            lx_guest_store_u32(output.host, address);
        else
            result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosGetNamedSharedMem(uint8 *a)
{
    return wireSharedMemory(a, 0);
}

static uint32 bridge32_DosAllocSharedMem(uint8 *a)
{
    return wireSharedMemory(a, 1);
}

static uint32 bridge32_DosExecPgm(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 4), args_pointer = lx_guest_load_u32(a + 12);
    const uint32 env_pointer = lx_guest_load_u32(a + 16);
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 24), CCHMAXPATH);
    char *args = args_pointer
                     ? lx_execution_copy_strings(GProcessState.execution, args_pointer, 65536, 2)
                     : NULL;
    char *env = env_pointer
                    ? lx_execution_copy_strings(GProcessState.execution, env_pointer, 65536, 0)
                    : NULL;
    LxGuestBorrow error = {0}, result = {0};
    if (!name || (args_pointer && !args) || (env_pointer && !env) || size > INT32_MAX ||
        (size && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), size,
                                      LX_GUEST_WRITE, &error)) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 20), 8, LX_GUEST_WRITE,
                             &result))
    {
        lx_execution_release(&error);
        free(name);
        free(args);
        free(env);
        return ERROR_INVALID_PARAMETER;
    }
    RESULTCODES codes = {0};
    const uint32 status =
        DosExecPgm(error.host, size, lx_guest_load_u32(a + 8), args, env, &codes, name);
    if (!status)
    {
        lx_guest_store_u32(result.host, codes.codeTerminate);
        lx_guest_store_u32((uint8 *)result.host + 4, codes.codeResult);
    }
    lx_execution_release(&error);
    lx_execution_release(&result);
    free(name);
    free(args);
    free(env);
    return status;
}

static uint32 bridge32_DosCreateThread(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    TID id = 0;
    const uint32 result = DosCreateThread(&id, (PFNTHREAD)(uintptr_t)lx_guest_load_u32(a + 4),
                                          lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12),
                                          lx_guest_load_u32(a + 16));
    if (!result)
        lx_guest_store_u32(output.host, id);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosWaitThread(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 4, 3, &output))
        return ERROR_INVALID_PARAMETER;
    TID id = lx_guest_load_u32(output.host);
    const uint32 result = DosWaitThread(&id, lx_guest_load_u32(a + 4));
    if (!result)
        lx_guest_store_u32(output.host, id);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosCopy(uint8 *a)
{
    char *from =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    char *to =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 4), CCHMAXPATH);
    if (!from || !to)
    {
        free(from);
        free(to);
        return ERROR_INVALID_PARAMETER;
    }
    const uint32 result = DosCopy(from, to, lx_guest_load_u32(a + 8));
    free(from);
    free(to);
    return result;
}

static uint32 bridge32_DosSetMaxFH(uint8 *a)
{
    return DosSetMaxFH(lx_guest_load_u32(a));
}

static uint32 bridge32_DosSetProcessCp(uint8 *a)
{
    return DosSetProcessCp(lx_guest_load_u32(a));
}

static uint32 bridge32_DosUnwindException(uint8 *a)
{
    /* Registration and report addresses remain guest addresses during dispatch. */
    return DosUnwindException((PVOID)(uintptr_t)lx_guest_load_u32(a),
                              (PVOID)(uintptr_t)lx_guest_load_u32(a + 4),
                              (PVOID)(uintptr_t)lx_guest_load_u32(a + 8));
}

static uint32 bridge32_DosRaiseException(uint8 *a)
{
    return DosRaiseException((PVOID)(uintptr_t)lx_guest_load_u32(a));
}

static void wireLoadDateTime(const uint8 *p, FDATE *date, FTIME *time)
{
    uint16 d = lx_guest_load_u16(p), t = lx_guest_load_u16(p + 2);
    date->day = d & 31;
    date->month = (d >> 5) & 15;
    date->year = d >> 9;
    time->twosecs = t & 31;
    time->minutes = (t >> 5) & 63;
    time->hours = t >> 11;
}

static uint32 bridge32_DosSetFileInfo(uint8 *a)
{
    if (lx_guest_load_u32(a + 4) != FIL_STANDARD)
        return ERROR_INVALID_LEVEL;
    if (lx_guest_load_u32(a + 12) < 24)
        return ERROR_BUFFER_OVERFLOW;
    LxGuestBorrow input;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 24, LX_GUEST_READ,
                             &input))
        return ERROR_INVALID_PARAMETER;
    const uint8 *p = input.host;
    FILESTATUS3 value = {0};
    wireLoadDateTime(p, &value.fdateCreation, &value.ftimeCreation);
    wireLoadDateTime(p + 4, &value.fdateLastAccess, &value.ftimeLastAccess);
    wireLoadDateTime(p + 8, &value.fdateLastWrite, &value.ftimeLastWrite);
    value.cbFile = lx_guest_load_u32(p + 12);
    value.cbFileAlloc = lx_guest_load_u32(p + 16);
    value.attrFile = lx_guest_load_u32(p + 20);
    const uint32 result = DosSetFileInfo(lx_guest_load_u32(a), FIL_STANDARD, &value, sizeof value);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_DosDupHandle(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, 3, &output))
        return ERROR_INVALID_PARAMETER;
    HFILE handle = lx_guest_load_u32(output.host);
    const uint32 result = DosDupHandle(lx_guest_load_u32(a), &handle);
    if (!result)
        lx_guest_store_u32(output.host, handle);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosGetResource(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    void *data = NULL;
    uint32 result = DosGetResource(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                   lx_guest_load_u32(a + 8), &data);
    if (!result)
    {
        uint32 address;
        if (lx_execution_address(GProcessState.execution, data, 1, LX_GUEST_READ, &address))
            lx_guest_store_u32(output.host, address);
        else
        {
            DosFreeResource(data);
            result = ERROR_INVALID_ADDRESS;
        }
    }
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosFreeResource(uint8 *a)
{
    void *data =
        lx_execution_memory(GProcessState.execution, lx_guest_load_u32(a), 1, LX_GUEST_READ);
    return data ? DosFreeResource(data) : ERROR_INVALID_PARAMETER;
}

static uint32 bridge32_DosQueryResourceSize(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    ULONG size = 0;
    uint32 result = DosQueryResourceSize(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                         lx_guest_load_u32(a + 8), &size);
    if (!result)
        lx_guest_store_u32(output.host, size);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32MustComplete(uint8 *arguments, int enter)
{
    const uint32 pointer = lx_guest_load_u32(arguments);
    LxGuestBorrow output = {0};
    if (pointer &&
        !lx_execution_borrow(GProcessState.execution, pointer, 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    ULONG count = 0;
    const uint32 result = enter ? DosEnterMustComplete(pointer ? &count : NULL)
                                : DosExitMustComplete(pointer ? &count : NULL);
    if (!result && pointer)
        lx_guest_store_u32(output.host, count);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosEnterMustComplete(uint8 *arguments)
{
    return bridge32MustComplete(arguments, 1);
}

static uint32 bridge32_DosExitMustComplete(uint8 *arguments)
{
    return bridge32MustComplete(arguments, 0);
}

static uint32 bridge32_DosEnterCritSec(uint8 *arguments)
{
    (void)arguments;
    return DosEnterCritSec();
}

static uint32 bridge32_DosExitCritSec(uint8 *arguments)
{
    (void)arguments;
    return DosExitCritSec();
}

static uint32 bridge32_DosMove(uint8 *a)
{
    char *from =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    char *to =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 4), CCHMAXPATH);
    if (!from || !to)
    {
        free(from);
        free(to);
        return ERROR_INVALID_PARAMETER;
    }
    const uint32 result = DosMove(from, to);
    free(from);
    free(to);
    return result;
}

static uint32 bridge32_Dos32QueryResourceSize(uint8 *a)
{
    return bridge32_DosQueryResourceSize(a);
}

static uint32 bridge32_DosSetPriority(uint8 *a)
{
    return DosSetPriority(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                          (LONG)lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_DosCreateDir(uint8 *a)
{
    if (lx_guest_load_u32(a + 4))
        return ERROR_EA_LIST_INCONSISTENT;
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosCreateDir(name, NULL);
    free(name);
    return result;
}

static uint32 bridge32_DosQueryModFromEIP(uint8 *arguments)
{
    const uint32 module_pointer = lx_guest_load_u32(arguments);
    const uint32 object_pointer = lx_guest_load_u32(arguments + 4);
    const uint32 capacity = lx_guest_load_u32(arguments + 8);
    const uint32 name_pointer = lx_guest_load_u32(arguments + 12);
    const uint32 offset_pointer = lx_guest_load_u32(arguments + 16);
    LxGuestBorrow module = {0}, object = {0}, name = {0}, offset = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((module_pointer && !lx_execution_borrow(GProcessState.execution, module_pointer, 4,
                                                LX_GUEST_WRITE, &module)) ||
        (object_pointer && !lx_execution_borrow(GProcessState.execution, object_pointer, 4,
                                                LX_GUEST_WRITE, &object)) ||
        (name_pointer && capacity &&
         !lx_execution_borrow(GProcessState.execution, name_pointer, capacity, LX_GUEST_WRITE,
                              &name)) ||
        (offset_pointer &&
         !lx_execution_borrow(GProcessState.execution, offset_pointer, 4, LX_GUEST_WRITE, &offset)))
        goto done;
    HMODULE handle = 0;
    ULONG number = 0, displacement = 0;
    char empty;
    result = DosQueryModFromEIP(&handle, &number, capacity,
                                name_pointer ? (capacity ? name.host : &empty) : NULL,
                                &displacement, lx_guest_load_u32(arguments + 20));
    if (result == NO_ERROR || result == ERROR_BUFFER_OVERFLOW)
    {
        if (module.host)
            lx_guest_store_u32(module.host, handle);
        if (object.host)
            lx_guest_store_u32(object.host, number);
        if (offset.host)
            lx_guest_store_u32(offset.host, displacement);
    }
done:
    lx_execution_release(&offset);
    lx_execution_release(&name);
    lx_execution_release(&object);
    lx_execution_release(&module);
    return result;
}

static uint32 bridge32_DosCreateMuxWaitSem(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a), count = lx_guest_load_u32(a + 8);
    char *name = pointer ? lx_execution_copy_string(GProcessState.execution, pointer, 256) : NULL;
    LxGuestBorrow output = {0}, records = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((pointer && !name) || count > 64 ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output) ||
        (count && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12),
                                       count * 8, LX_GUEST_READ, &records)))
        goto done;
    SEMRECORD input[64];
    for (uint32 i = 0; i < count; i++)
    {
        input[i].hsemCur = lx_guest_load_u32((uint8 *)records.host + i * 8);
        input[i].ulUser = lx_guest_load_u32((uint8 *)records.host + i * 8 + 4);
    }
    HMUX handle = 0;
    result = DosCreateMuxWaitSem(name, &handle, count, input, lx_guest_load_u32(a + 16));
    if (!result)
        lx_guest_store_u32(output.host, handle);
done:
    lx_execution_release(&records);
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosOpenMuxWaitSem(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a);
    char *name = pointer ? lx_execution_copy_string(GProcessState.execution, pointer, 256) : NULL;
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!pointer || name) && lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4),
                                                  4, LX_GUEST_READ | LX_GUEST_WRITE, &output))
    {
        HMUX handle = lx_guest_load_u32(output.host);
        result = DosOpenMuxWaitSem(name, &handle);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosCloseMuxWaitSem(uint8 *a)
{
    return DosCloseMuxWaitSem(lx_guest_load_u32(a));
}

typedef struct WireMuxWait
{
    HMUX handle;
    ULONG timeout;
    LxGuestBorrow output;
} WireMuxWait;

static void wire_mux_cleanup(void *argument)
{
    WireMuxWait *call = argument;
    lx_execution_release(&call->output);
}

static uint32 wire_mux_operation(void *argument)
{
    WireMuxWait *call = argument;
    ULONG user = 0;
    uint32 result = DosWaitMuxWaitSem(call->handle, call->timeout, &user);
    if (!result || result == ERROR_SEM_OWNER_DIED)
        lx_guest_store_u32(call->output.host, user);
    return result;
}

static uint32 bridge32_DosWaitMuxWaitSem(uint8 *a)
{
    WireMuxWait call = {0};
    call.handle = lx_guest_load_u32(a);
    call.timeout = lx_guest_load_u32(a + 4);
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE,
                             &call.output))
        return ERROR_INVALID_PARAMETER;
    return lx_host_guarded_call(wire_mux_operation, &call, wire_mux_cleanup, &call);
}

static uint32 bridge32_DosAddMuxWaitSem(uint8 *a)
{
    uint8 bytes[8];
    if (!lx_execution_read(GProcessState.execution, lx_guest_load_u32(a + 4), bytes, sizeof bytes))
        return ERROR_INVALID_PARAMETER;
    SEMRECORD record = {lx_guest_load_u32(bytes), lx_guest_load_u32(bytes + 4)};
    return DosAddMuxWaitSem(lx_guest_load_u32(a), &record);
}

static uint32 bridge32_DosDeleteMuxWaitSem(uint8 *a)
{
    return DosDeleteMuxWaitSem(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_DosQueryMuxWaitSem(uint8 *a)
{
    LxGuestBorrow count = {0}, records = {0}, flags = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4,
                             LX_GUEST_READ | LX_GUEST_WRITE, &count) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                             &flags))
        goto done;
    ULONG capacity = lx_guest_load_u32(count.host), attr = 0;
    if (capacity > 64)
        capacity = 64;
    if (capacity && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8),
                                         capacity * 8, LX_GUEST_WRITE, &records))
        goto done;
    SEMRECORD output[64];
    result = DosQueryMuxWaitSem(lx_guest_load_u32(a), &capacity, output, &attr);
    if (!result || result == ERROR_PARAM_TOO_SMALL)
        lx_guest_store_u32(count.host, capacity);
    if (!result)
    {
        lx_guest_store_u32(flags.host, attr);
        for (ULONG i = 0; i < capacity; i++)
        {
            lx_guest_store_u32((uint8 *)records.host + i * 8, output[i].hsemCur);
            lx_guest_store_u32((uint8 *)records.host + i * 8 + 4, output[i].ulUser);
        }
    }
done:
    lx_execution_release(&flags);
    lx_execution_release(&records);
    lx_execution_release(&count);
    return result;
}

static uint32 bridge32_DosKillThread(uint8 *a)
{
    return DosKillThread(lx_guest_load_u32(a));
}

static uint32 bridge32_DosDeleteDir(uint8 *a)
{
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosDeleteDir(name);
    free(name);
    return result;
}

static uint32 bridge32_DosSetPathInfo(uint8 *a)
{
    if (lx_guest_load_u32(a + 4) != FIL_STANDARD)
        return ERROR_INVALID_LEVEL;
    if (lx_guest_load_u32(a + 12) < 24)
        return ERROR_BUFFER_OVERFLOW;
    char *name =
        lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    LxGuestBorrow input = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if (name && lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), 24,
                                    LX_GUEST_READ, &input))
    {
        FILESTATUS3 value = {0};
        value.attrFile = lx_guest_load_u32((uint8 *)input.host + 20);
        result =
            DosSetPathInfo(name, FIL_STANDARD, &value, sizeof value, lx_guest_load_u32(a + 16));
    }
    free(name);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_DosQueryFSAttach(uint8 *a)
{
    char *name = lx_guest_load_u32(a) ? lx_execution_copy_string(GProcessState.execution,
                                                                 lx_guest_load_u32(a), CCHMAXPATH)
                                      : NULL;
    LxGuestBorrow length = {0}, output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((lx_guest_load_u32(a) && !name) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 16), 4, 3, &length))
        goto done;
    ULONG size = lx_guest_load_u32(length.host);
    if (size > 65536 ||
        (size && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), size,
                                      LX_GUEST_WRITE, &output)))
        goto done;
    result = DosQueryFSAttach(name, lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8), output.host,
                              &size);
    lx_guest_store_u32(length.host, size);
done:
    free(name);
    lx_execution_release(&length);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosDevIOCtl(uint8 *a)
{
    LxGuestBorrow slots[4] = {{0}};
    uint32 offsets[] = {12, 20, 24, 32};
    uint32 sizes[] = {lx_guest_load_u32(a + 16), 4, lx_guest_load_u32(a + 28), 4};
    uint32 result = ERROR_INVALID_PARAMETER;
    for (unsigned i = 0; i < 4; i++)
    {
        uint32 pointer = lx_guest_load_u32(a + offsets[i]);
        if (sizes[i] > 1048576 || (pointer && !lx_execution_borrow(GProcessState.execution, pointer,
                                                                   sizes[i], 3, &slots[i])))
            goto done;
    }
    ULONG parameters = slots[1].host ? lx_guest_load_u32(slots[1].host) : 0;
    ULONG data = slots[3].host ? lx_guest_load_u32(slots[3].host) : 0;
    result = DosDevIOCtl(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8),
                         slots[0].host, sizes[0], slots[1].host ? &parameters : NULL, slots[2].host,
                         sizes[2], slots[3].host ? &data : NULL);
    if (slots[1].host)
        lx_guest_store_u32(slots[1].host, parameters);
    if (slots[3].host)
        lx_guest_store_u32(slots[3].host, data);
done:
    for (unsigned i = 0; i < 4; i++)
        lx_execution_release(&slots[i]);
    return result;
}

static uint32 bridge32_DosQueryFSInfo(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 12);
    if (size > 65536)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8), size,
                             LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 result =
        DosQueryFSInfo(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), output.host, size);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosFlatToSel(uint8 *a)
{
    uint32 address = lx_guest_load_u32(a);
    void *memory = lx_execution_memory(GProcessState.execution, address, 1, 0);
    uint32 result = memory ? GProcessState.convert32to1616(memory) : 0;
    TRACE_RET("DosFlatToSel(%08x) -> %08x", (uint)address, (uint)result);
    return result;
}

static uint32 bridge32_DosSelToFlat(uint8 *a)
{
    void *memory = GProcessState.convert1616to32(lx_guest_load_u32(a));
    uint32 address = 0;
    uint32 result = memory && lx_execution_address(GProcessState.execution, memory, 1, 0, &address)
                        ? address
                        : 0;
    TRACE_RET("DosSelToFlat(%08x) -> %08x", (uint)lx_guest_load_u32(a), (uint)result);
    return result;
}

static uint32 bridge32_DosSubSetMem(uint8 *a)
{
    void *base = lx_execution_memory(GProcessState.execution, lx_guest_load_u32(a), 64,
                                     LX_GUEST_READ | LX_GUEST_WRITE);
    return base ? DosSubSetMem(base, lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8))
                : ERROR_INVALID_PARAMETER;
}

static uint32 bridge32_DosSubAllocMem(uint8 *a)
{
    void *base = lx_execution_memory(GProcessState.execution, lx_guest_load_u32(a), 64,
                                     LX_GUEST_READ | LX_GUEST_WRITE);
    LxGuestBorrow output;
    if (!base || !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4,
                                      LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    void *memory = NULL;
    uint32 result = DosSubAllocMem(base, &memory, lx_guest_load_u32(a + 8)), address;
    if (!result)
    {
        if (lx_execution_address(GProcessState.execution, memory, 0, 0, &address))
            lx_guest_store_u32(output.host, address);
        else
            result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosSubFreeMem(uint8 *a)
{
    void *base = lx_execution_memory(GProcessState.execution, lx_guest_load_u32(a), 64,
                                     LX_GUEST_READ | LX_GUEST_WRITE);
    void *memory = lx_execution_memory(GProcessState.execution, lx_guest_load_u32(a + 4), 1, 0);
    return base && memory ? DosSubFreeMem(base, memory, lx_guest_load_u32(a + 8))
                          : ERROR_INVALID_PARAMETER;
}

static uint32 bridge32_DosSubUnsetMem(uint8 *a)
{
    void *base = lx_execution_memory(GProcessState.execution, lx_guest_load_u32(a), 64,
                                     LX_GUEST_READ | LX_GUEST_WRITE);
    return base ? DosSubUnsetMem(base) : ERROR_INVALID_PARAMETER;
}

static uint32 bridge32_DosBeep(uint8 *arguments)
{
    return DosBeep(lx_guest_load_u32(arguments), lx_guest_load_u32(arguments + 4));
}

static uint32 bridge32_DosQueryProcType(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 8);
    char *name = pointer ? lx_execution_copy_string(GProcessState.execution, pointer, 256) : NULL;
    LxGuestBorrow type = {0};
    if ((pointer && !name) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                             &type))
    {
        free(name);
        return ERROR_INVALID_PARAMETER;
    }
    ULONG value;
    uint32 result = DosQueryProcType(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), name, &value);
    if (!result)
        lx_guest_store_u32(type.host, value);
    lx_execution_release(&type);
    free(name);
    return result;
}

static uint32 bridge32_DosEditName(uint8 *a)
{
    char *source = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 4), 260);
    char *edit = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 8), 260);
    LxGuestBorrow output = {0};
    uint32 capacity = lx_guest_load_u32(a + 16), result = ERROR_INVALID_PARAMETER;
    if (source && edit && capacity &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), capacity,
                            LX_GUEST_WRITE, &output))
        result = DosEditName(lx_guest_load_u32(a), source, edit, output.host, capacity);
    lx_execution_release(&output);
    free(source);
    free(edit);
    return result;
}

static uint32 bridge32_DosTmrQueryFreq(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    ULONG value;
    uint32 result = DosTmrQueryFreq(&value);
    if (!result)
        lx_guest_store_u32(output.host, value);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosTmrQueryTime(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 8, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    uint32 result = DosTmrQueryTime(output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosEnumAttribute(uint8 *a)
{
    uint32 reference_type = lx_guest_load_u32(a), pointer = lx_guest_load_u32(a + 4);
    char *path = reference_type == 1
                     ? lx_execution_copy_string(GProcessState.execution, pointer, 260)
                     : NULL;
    LxGuestBorrow reference = {0}, output = {0}, count = {0};
    uint32 capacity = lx_guest_load_u32(a + 16), result = ERROR_INVALID_PARAMETER;
    if (reference_type > 1 || (reference_type == 1 && !path) ||
        (reference_type == 0 &&
         !lx_execution_borrow(GProcessState.execution, pointer, 4, LX_GUEST_READ, &reference)) ||
        (capacity && !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12),
                                          capacity, LX_GUEST_WRITE, &output)) ||
        !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 20), 4,
                             LX_GUEST_READ | LX_GUEST_WRITE, &count))
        goto done;
    ULONG number = lx_guest_load_u32(count.host),
          handle = reference.host ? lx_guest_load_u32(reference.host) : 0;
    result =
        DosEnumAttribute(reference_type, path ? (void *)path : &handle, lx_guest_load_u32(a + 8),
                         output.host, capacity, &number, lx_guest_load_u32(a + 24));
    lx_guest_store_u32(count.host, number);
done:
    lx_execution_release(&reference);
    lx_execution_release(&output);
    lx_execution_release(&count);
    free(path);
    return result;
}

static uint32 bridge32_DosShutdown(uint8 *a)
{
    return DosShutdown(lx_guest_load_u32(a));
}

static uint32 bridge32_DosGetSharedMem(uint8 *a)
{
    return DosGetSharedMemAddress(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_DosGiveSharedMem(uint8 *a)
{
    void *address = lx_execution_memory(GProcessState.execution, lx_guest_load_u32(a), 1, 0);
    return address ? DosGiveSharedMem(address, lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8))
                   : ERROR_INVALID_ADDRESS;
}

static uint32 bridge32_DosSetExtLIBPATH(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a);
    char *path = pointer ? lx_execution_copy_string(GProcessState.execution, pointer, 1024) : NULL;
    uint32 result = pointer && !path ? ERROR_INVALID_PARAMETER
                                     : DosSetExtLIBPATH(path, lx_guest_load_u32(a + 4));
    free(path);
    return result;
}

static uint32 bridge32_DosQueryExtLIBPATH(uint8 *a)
{
    uint32 flags = lx_guest_load_u32(a + 4);
    if (flags < BEGIN_LIBPATH || flags > LIBPATHSTRICT)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a),
                             flags == LIBPATHSTRICT ? 2 : 1024, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 result = DosQueryExtLIBPATH(output.host, flags);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosSetFileLocks(uint8 *arguments)
{
    LxGuestBorrow borrowed[2] = {{0}};
    FILELOCK ranges[2] = {{0}}, *pointers[2] = {NULL, NULL};
    uint32 result = ERROR_INVALID_PARAMETER;
    for (unsigned i = 0; i < 2; i++)
    {
        uint32 address = lx_guest_load_u32(arguments + 4 + i * 4);
        if (!address)
            continue;
        if (!lx_execution_borrow(GProcessState.execution, address, 8, LX_GUEST_READ, &borrowed[i]))
            goto done;
        ranges[i].lOffset = (LONG)lx_guest_load_u32(borrowed[i].host);
        ranges[i].lRange = (LONG)lx_guest_load_u32((uint8 *)borrowed[i].host + 4);
        pointers[i] = &ranges[i];
    }
    result = DosSetFileLocks(lx_guest_load_u32(arguments), pointers[0], pointers[1],
                             lx_guest_load_u32(arguments + 12), lx_guest_load_u32(arguments + 16));
done:
    for (unsigned i = 0; i < 2; i++)
        lx_execution_release(&borrowed[i]);
    return result;
}

static uint32 bridge32_DosSuspendThread(uint8 *a)
{
    return DosSuspendThread(lx_guest_load_u32(a));
}

static uint32 bridge32_DosResumeThread(uint8 *a)
{
    return DosResumeThread(lx_guest_load_u32(a));
}

static uint32 armTimerBridge(uint8 *a, APIRET (*arm)(ULONG, HSEM, PHTIMER))
{
    const uint32 address = lx_guest_load_u32(a + 8);
    LxGuestBorrow output = {0};
    if (address &&
        !lx_execution_borrow(GProcessState.execution, address, 2, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 result = arm(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosAsyncTimer(uint8 *a)
{
    return armTimerBridge(a, DosAsyncTimer);
}

static uint32 bridge32_DosStartTimer(uint8 *a)
{
    return armTimerBridge(a, DosStartTimer);
}

static uint32 bridge32_DosStopTimer(uint8 *a)
{
    return DosStopTimer(lx_guest_load_u32(a));
}

static uint32 bridge32_DosQueryMutexSem(uint8 *a)
{
    PID pid = 0;
    TID tid = 0;
    ULONG count = 0;
    const uint32 result = DosQueryMutexSem(lx_guest_load_u32(a), &pid, &tid, &count);
    if (result != NO_ERROR && result != ERROR_SEM_OWNER_DIED)
        return result;
    uint8 word[4];
    if (count)
    {
        lx_guest_store_u32(word, pid);
        if (!lx_execution_write(GProcessState.execution, lx_guest_load_u32(a + 4), word, 4))
            return ERROR_INVALID_PARAMETER;
        lx_guest_store_u32(word, tid);
        if (!lx_execution_write(GProcessState.execution, lx_guest_load_u32(a + 8), word, 4))
            return ERROR_INVALID_PARAMETER;
    }
    lx_guest_store_u32(word, count);
    if (!lx_execution_write(GProcessState.execution, lx_guest_load_u32(a + 12), word, 4))
        return ERROR_INVALID_PARAMETER;
    return result;
}

/* The drive and level are refused before the label is examined. */
static uint32 bridge32_DosSetFSInfo(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 12) & 0xffff;
    uint8 label[12];
    const size_t bytes = size < sizeof label ? size : sizeof label;
    const int readable =
        lx_execution_read(GProcessState.execution, lx_guest_load_u32(a + 8), label, bytes);
    return DosSetFSInfo(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), readable ? label : NULL,
                        size);
}

/* No argument frame: the entry reads the caller's registers and stack itself. */
static uint32 bridge32_THK_UNITHUNK(uint8 *a)
{
    (void)a;
    return THK_UNITHUNK();
}

/* NLS wire structures have 32-bit fields and explicit byte order. */
static uint32 bridge32_DosQueryDBCSEnv(uint8 *arguments)
{
    const uint32 size = lx_guest_load_u32(arguments), cc_pointer = lx_guest_load_u32(arguments + 4);
    const uint32 pointer = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow codepage = {0}, buffer = {0};
    if (!lx_execution_borrow(GProcessState.execution, cc_pointer, 8, LX_GUEST_READ, &codepage) ||
        (size &&
         !lx_execution_borrow(GProcessState.execution, pointer, size, LX_GUEST_WRITE, &buffer)))
    {
        lx_execution_release(&codepage);
        return ERROR_INVALID_PARAMETER;
    }
    COUNTRYCODE cc = {lx_guest_load_u32(codepage.host),
                      lx_guest_load_u32((uint8 *)codepage.host + 4)};
    const uint32 result = DosQueryDBCSEnv(size, &cc, buffer.host);
    lx_execution_release(&buffer);
    lx_execution_release(&codepage);
    return result;
}

/* Queue data addresses are opaque guest values, not host pointers to dereference. */
static uint32 bridge32_DosCreateQueue(uint8 *a)
{
    LxGuestBorrow output = {0};
    char *name = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 8), 256);
    uint32 result = ERROR_INVALID_PARAMETER;
    if (name && lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 4,
                                    LX_GUEST_WRITE, &output))
    {
        HQUEUE handle = 0;
        result = DosCreateQueue(&handle, lx_guest_load_u32(a + 4), name);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_DosOpenQueue(uint8 *a)
{
    LxGuestBorrow pid = {0}, output = {0};
    char *name = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a + 8), 256);
    uint32 result = ERROR_INVALID_PARAMETER;
    if (name &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE,
                            &pid) &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                            &output))
    {
        HQUEUE handle = 0;
        PID owner = 0;
        result = DosOpenQueue(&owner, &handle, name);
        if (!result)
        {
            lx_guest_store_u32(pid.host, owner);
            lx_guest_store_u32(output.host, handle);
        }
    }
    lx_execution_release(&output);
    lx_execution_release(&pid);
    free(name);
    return result;
}

static uint32 bridge32_DosCloseQueue(uint8 *a)
{
    return DosCloseQueue(lx_guest_load_u32(a));
}

static uint32 bridge32_DosPurgeQueue(uint8 *a)
{
    return DosPurgeQueue(lx_guest_load_u32(a));
}

static uint32 bridge32_DosQueryQueue(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output))
        return ERROR_INVALID_PARAMETER;
    ULONG count = 0;
    uint32 result = DosQueryQueue(lx_guest_load_u32(a), &count);
    if (!result)
        lx_guest_store_u32(output.host, count);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DosWriteQueue(uint8 *a)
{
    return DosWriteQueue(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8),
                         (PVOID)(uintptr_t)lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16));
}

typedef struct QueueReceive32
{
    uint8 *arguments;
    BOOL peek;
    LxGuestBorrow outputs[5];
} QueueReceive32;

static void queue_receive32_cleanup(void *argument)
{
    QueueReceive32 *call = argument;
    for (unsigned i = 0; i < 5; i++)
        lx_execution_release(&call->outputs[i]);
}

static uint32 queue_receive32_operation(void *argument)
{
    QueueReceive32 *call = argument;
    uint8 *a = call->arguments;
    BOOL peek = call->peek;
    LxGuestBorrow *outputs = call->outputs;
    const unsigned positions[] = {4, 8, 12, 24, 16};
    const unsigned sizes[] = {8, 4, 4, 1, 4};
    uint32 result = ERROR_INVALID_PARAMETER;
    for (unsigned i = 0; i < (peek ? 5u : 4u); i++)
        if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + positions[i]),
                                 sizes[i], LX_GUEST_WRITE | (i == 4 ? LX_GUEST_READ : 0),
                                 &outputs[i]))
            goto done;
    REQUESTDATA request = {0};
    ULONG size = 0, cursor = peek ? lx_guest_load_u32(outputs[4].host) : lx_guest_load_u32(a + 16);
    PVOID data = NULL;
    BYTE priority = 0;
    if (peek)
        result = DosPeekQueue(lx_guest_load_u32(a), &request, &size, &data, &cursor,
                              lx_guest_load_u32(a + 20), &priority, lx_guest_load_u32(a + 28));
    else
        result = DosReadQueue(lx_guest_load_u32(a), &request, &size, &data, cursor,
                              lx_guest_load_u32(a + 20), &priority, lx_guest_load_u32(a + 28));
    if (!result)
    {
        lx_guest_store_u32(outputs[0].host, request.pid);
        lx_guest_store_u32((uint8 *)outputs[0].host + 4, request.ulData);
        lx_guest_store_u32(outputs[1].host, size);
        uint32 address = (uint32)(uintptr_t)data;
        if (data && !lx_execution_address(GProcessState.execution, data, 0, 0, &address) &&
            (uintptr_t)data > UINT32_MAX)
        {
            result = ERROR_QUE_MEMORY_ERROR;
            goto done;
        }
        lx_guest_store_u32(outputs[2].host, address);
        *(uint8 *)outputs[3].host = priority;
        if (peek)
            lx_guest_store_u32(outputs[4].host, cursor);
    }
done:
    return result;
}

static uint32 queue_receive32(uint8 *a, BOOL peek)
{
    QueueReceive32 call = {0};
    call.arguments = a;
    call.peek = peek;
    return lx_host_guarded_call(queue_receive32_operation, &call, queue_receive32_cleanup, &call);
}

static uint32 bridge32_DosPeekQueue(uint8 *a)
{
    return queue_receive32(a, TRUE);
}

static uint32 bridge32_DosReadQueue(uint8 *a)
{
    return queue_receive32(a, FALSE);
}
