/* DOSCALLS flat 32-bit ABI. Host pointer outputs use private temporary storage. */
static uint32 bridge32_DosAllocMem(uint8 *arguments)
{
    const uint32 output = lx_guest_load_u32(arguments);
    const uint32 size = lx_guest_load_u32(arguments + 4);
    const uint32 flags = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow slot;
    if (!lx_execution_borrow(GLoaderState.execution, output, 4, LX_GUEST_WRITE, &slot))
        return ERROR_INVALID_PARAMETER;
    void *memory = NULL;
    uint32 result = DosAllocMem(&memory, size, flags);
    if (!result) {
        uint32 address;
        if (lx_execution_address(GLoaderState.execution, memory, 0, 0, &address))
            lx_guest_store_u32(slot.host, address);
        else result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&slot);
    return result;
}
static uint32 bridge32_DosQuerySysInfo(uint8 *arguments)
{
    const uint32 first = lx_guest_load_u32(arguments), last = lx_guest_load_u32(arguments + 4);
    const uint32 pointer = lx_guest_load_u32(arguments + 8), capacity = lx_guest_load_u32(arguments + 12);
    if (last < first || first < QSV_MAX_PATH_LENGTH || last > QSV_FOREGROUND_PROCESS)
        return ERROR_INVALID_PARAMETER;
    const uint32 count = last - first + 1;
    if (capacity < count * 4) return ERROR_BUFFER_OVERFLOW;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, pointer, count * 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 values[QSV_FOREGROUND_PROCESS];
    const uint32 result = DosQuerySysInfo(first, last, values, count * 4);
    if (!result) for (uint32 i = 0; i < count; i++) lx_guest_store_u32((uint8 *) output.host + i * 4, values[i]);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_DosWrite(uint8 *arguments)
{
    const uint32 handle = lx_guest_load_u32(arguments), pointer = lx_guest_load_u32(arguments + 4);
    const uint32 size = lx_guest_load_u32(arguments + 8), actual_pointer = lx_guest_load_u32(arguments + 12);
    LxGuestBorrow data = {0}, actual = {0};
    if ((size && !lx_execution_borrow(GLoaderState.execution, pointer, size, LX_GUEST_READ, &data)) ||
        !lx_execution_borrow(GLoaderState.execution, actual_pointer, 4, LX_GUEST_WRITE, &actual)) {
        lx_execution_release(&data); return ERROR_INVALID_PARAMETER;
    }
    uint32 written = 0;
    const uint32 result = DosWrite(handle, data.host, size, &written);
    lx_guest_store_u32(actual.host, written);
    lx_execution_release(&actual); lx_execution_release(&data);
    return result;
}
static uint32 bridge32_DosRead(uint8 *arguments)
{
    const uint32 handle = lx_guest_load_u32(arguments), pointer = lx_guest_load_u32(arguments + 4);
    const uint32 size = lx_guest_load_u32(arguments + 8), actual_pointer = lx_guest_load_u32(arguments + 12);
    LxGuestBorrow data = {0}, actual = {0};
    if ((size && !lx_execution_borrow(GLoaderState.execution, pointer, size, LX_GUEST_WRITE, &data)) ||
        !lx_execution_borrow(GLoaderState.execution, actual_pointer, 4, LX_GUEST_WRITE, &actual)) {
        lx_execution_release(&data); return ERROR_INVALID_PARAMETER;
    }
    uint32 count = 0;
    const uint32 result = DosRead(handle, data.host, size, &count);
    lx_guest_store_u32(actual.host, count);
    lx_execution_release(&actual); lx_execution_release(&data);
    return result;
}
static uint32 bridge32_DosClose(uint8 *arguments) { return DosClose(lx_guest_load_u32(arguments)); }
static uint32 bridge32_DosFreeMem(uint8 *arguments)
{
    void *p = lx_execution_memory(GLoaderState.execution, lx_guest_load_u32(arguments), 0, 0);
    return p ? DosFreeMem(p) : ERROR_INVALID_ADDRESS;
}
static uint32 bridge32_DosExit(uint8 *arguments)
{
    DosExit(lx_guest_load_u32(arguments), lx_guest_load_u32(arguments + 4));
    return 0;
}
static uint32 bridge32_DosSetRelMaxFH(uint8 *arguments)
{
    const uint32 request_pointer = lx_guest_load_u32(arguments), current_pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow request = {0}, current = {0};
    if ((request_pointer && !lx_execution_borrow(GLoaderState.execution, request_pointer, 4, 3, &request)) ||
        (current_pointer && !lx_execution_borrow(GLoaderState.execution, current_pointer, 4, LX_GUEST_WRITE, &current))) {
        lx_execution_release(&request); return ERROR_INVALID_PARAMETER;
    }
    LONG requested = request.host ? (LONG) lx_guest_load_u32(request.host) : 0;
    ULONG count = 0;
    const uint32 result = DosSetRelMaxFH(request.host ? &requested : NULL, current.host ? &count : NULL);
    if (request.host) lx_guest_store_u32(request.host, (uint32) requested);
    if (current.host) lx_guest_store_u32(current.host, count);
    lx_execution_release(&request); lx_execution_release(&current);
    return result;
}
static uint32 bridge32_DosGetInfoBlocks(uint8 *arguments)
{
    const uint32 tib_pointer = lx_guest_load_u32(arguments), pib_pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow tib = {0}, pib = {0};
    if ((tib_pointer && !lx_execution_borrow(GLoaderState.execution, tib_pointer, 4, LX_GUEST_WRITE, &tib)) ||
        (pib_pointer && !lx_execution_borrow(GLoaderState.execution, pib_pointer, 4, LX_GUEST_WRITE, &pib))) {
        lx_execution_release(&tib); return ERROR_INVALID_PARAMETER;
    }
    uint32 address;
    uint32 result = NO_ERROR;
    if (tib.host) {
        if (lx_execution_address(GLoaderState.execution, GLoaderState.getOs2Tib(), sizeof(LxGuestTib), 0, &address))
            lx_guest_store_u32(tib.host, address);
        else result = ERROR_INVALID_ADDRESS;
    }
    if (pib.host) lx_guest_store_u32(pib.host, lx_execution_mapping_address(GLoaderState.guest_pib));
    lx_execution_release(&tib); lx_execution_release(&pib);
    return result;
}
static uint32 bridge32_DosDevConfig(uint8 *arguments)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(arguments), 1,
                              LX_GUEST_WRITE, &output)) return ERROR_INVALID_PARAMETER;
    const uint32 result = DosDevConfig(output.host, lx_guest_load_u32(arguments + 4));
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosQueryHType(uint8 *arguments)
{
    const uint32 type_pointer = lx_guest_load_u32(arguments + 4), attr_pointer = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow type = {0}, attr = {0};
    if ((type_pointer && !lx_execution_borrow(GLoaderState.execution, type_pointer, 4, LX_GUEST_WRITE, &type)) ||
        (attr_pointer && !lx_execution_borrow(GLoaderState.execution, attr_pointer, 4, LX_GUEST_WRITE, &attr))) {
        lx_execution_release(&type); return ERROR_INVALID_PARAMETER;
    }
    ULONG type_value = 0, attr_value = 0;
    const uint32 result = DosQueryHType(lx_guest_load_u32(arguments), &type_value, &attr_value);
    if (!result) {
        if (type.host) lx_guest_store_u32(type.host, type_value);
        if (attr.host) lx_guest_store_u32(attr.host, attr_value);
    }
    lx_execution_release(&type); lx_execution_release(&attr); return result;
}
static uint32 bridge32_DosExitList(uint8 *arguments)
{
    return DosExitList(lx_guest_load_u32(arguments), (PFNEXITLIST) (uintptr_t) lx_guest_load_u32(arguments + 4));
}
static uint32 bridge32_DosSetSignalExceptionFocus(uint8 *arguments)
{
    const uint32 pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow count = {0};
    if (pointer && !lx_execution_borrow(GLoaderState.execution, pointer, 4, LX_GUEST_WRITE, &count))
        return ERROR_INVALID_PARAMETER;
    ULONG value = 0;
    const uint32 result = DosSetSignalExceptionFocus(lx_guest_load_u32(arguments), count.host ? &value : NULL);
    if (!result && count.host) lx_guest_store_u32(count.host, value);
    lx_execution_release(&count); return result;
}
static uint32 bridge32_DosSetExceptionHandler(uint8 *arguments)
{
    LxGuestBorrow record;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(arguments), 8, 3, &record))
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosSetExceptionHandler(record.host);
    lx_execution_release(&record); return result;
}
static uint32 bridge32_DosUnsetExceptionHandler(uint8 *arguments)
{
    LxGuestBorrow record;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(arguments), 8, 3, &record))
        return ERROR_INVALID_PARAMETER;
    const uint32 result = DosUnsetExceptionHandler(record.host);
    lx_execution_release(&record); return result;
}
static uint32 bridge32_DosLoadModule(uint8 *arguments)
{
    const uint32 error_pointer = lx_guest_load_u32(arguments), error_size = lx_guest_load_u32(arguments + 4);
    const uint32 name_pointer = lx_guest_load_u32(arguments + 8), module_pointer = lx_guest_load_u32(arguments + 12);
    char *name = lx_execution_copy_string(GLoaderState.execution, name_pointer, CCHMAXPATH);
    if (!name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow error = {0}, module = {0};
    if ((error_size && !lx_execution_borrow(GLoaderState.execution, error_pointer, error_size, LX_GUEST_WRITE, &error)) ||
        !lx_execution_borrow(GLoaderState.execution, module_pointer, 4, LX_GUEST_WRITE, &module)) {
        lx_execution_release(&error); free(name); return ERROR_INVALID_PARAMETER;
    }
    char unused = 0; HMODULE value = 0;
    const uint32 result = DosLoadModule(error.host ? error.host : &unused, error_size, name, &value);
    if (!result) lx_guest_store_u32(module.host, value);
    lx_execution_release(&module); lx_execution_release(&error); free(name);
    return result;
}
static uint32 bridge32_DosQueryProcAddr(uint8 *arguments)
{
    const uint32 module = lx_guest_load_u32(arguments), ordinal = lx_guest_load_u32(arguments + 4);
    const uint32 name_pointer = lx_guest_load_u32(arguments + 8), output_pointer = lx_guest_load_u32(arguments + 12);
    char *name = NULL;
    if (!ordinal) {
        name = lx_execution_copy_string(GLoaderState.execution, name_pointer, 256);
        if (!name) return ERROR_INVALID_PARAMETER;
    }
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, output_pointer, 4, LX_GUEST_WRITE, &output)) {
        free(name); return ERROR_INVALID_PARAMETER;
    }
    PFN function = NULL;
    uint32 result = DosQueryProcAddr(module, ordinal, name, &function);
    if (!result) {
        uint32 entry;
        if (lx_execution_entry_address(GLoaderState.execution, (void *) function, &entry))
            lx_guest_store_u32(output.host, entry);
        else result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&output); free(name); return result;
}
static uint32 bridge32_DosFreeModule(uint8 *arguments) { return DosFreeModule(lx_guest_load_u32(arguments)); }
static uint32 wireCreateSemaphore(uint8 *arguments, int mutex)
{
    const uint32 name_pointer = lx_guest_load_u32(arguments), output_pointer = lx_guest_load_u32(arguments + 4);
    char *name = name_pointer ? lx_execution_copy_string(GLoaderState.execution, name_pointer, CCHMAXPATH) : NULL;
    if (name_pointer && !name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, output_pointer, 4, LX_GUEST_WRITE, &output)) {
        free(name); return ERROR_INVALID_PARAMETER;
    }
    uint32 handle = 0, attrs = lx_guest_load_u32(arguments + 8), state = lx_guest_load_u32(arguments + 12);
    const uint32 result = mutex ? DosCreateMutexSem(name, &handle, attrs, state) : DosCreateEventSem(name, &handle, attrs, state);
    if (!result) lx_guest_store_u32(output.host, handle);
    lx_execution_release(&output); free(name); return result;
}
static uint32 bridge32_DosCreateMutexSem(uint8 *a) { return wireCreateSemaphore(a, 1); }
static uint32 bridge32_DosCreateEventSem(uint8 *a) { return wireCreateSemaphore(a, 0); }
static uint32 wireOpenSemaphore(uint8 *arguments, int mutex)
{
    const uint32 name_pointer = lx_guest_load_u32(arguments), output_pointer = lx_guest_load_u32(arguments + 4);
    char *name = name_pointer ? lx_execution_copy_string(GLoaderState.execution, name_pointer, CCHMAXPATH) : NULL;
    if (name_pointer && !name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, output_pointer, 4, LX_GUEST_WRITE, &output)) {
        free(name); return ERROR_INVALID_PARAMETER;
    }
    uint32 handle = 0;
    const uint32 result = mutex ? DosOpenMutexSem(name, &handle) : DosOpenEventSem(name, &handle);
    if (!result) lx_guest_store_u32(output.host, handle);
    lx_execution_release(&output); free(name); return result;
}
static uint32 bridge32_DosOpenMutexSem(uint8 *a) { return wireOpenSemaphore(a, 1); }
static uint32 bridge32_DosOpenEventSem(uint8 *a) { return wireOpenSemaphore(a, 0); }
static uint32 bridge32_DosRequestMutexSem(uint8 *a) { return DosRequestMutexSem(lx_guest_load_u32(a), lx_guest_load_u32(a + 4)); }
static uint32 bridge32_DosReleaseMutexSem(uint8 *a) { return DosReleaseMutexSem(lx_guest_load_u32(a)); }
static uint32 bridge32_DosCloseMutexSem(uint8 *a) { return DosCloseMutexSem(lx_guest_load_u32(a)); }
static uint32 bridge32_DosPostEventSem(uint8 *a) { return DosPostEventSem(lx_guest_load_u32(a)); }
static uint32 bridge32_DosCloseEventSem(uint8 *a) { return DosCloseEventSem(lx_guest_load_u32(a)); }
static uint32 bridge32_DosWaitEventSem(uint8 *a) { return DosWaitEventSem(lx_guest_load_u32(a), lx_guest_load_u32(a + 4)); }
static uint32 wireQueryEvent(uint8 *arguments, int reset)
{
    const uint32 pointer = lx_guest_load_u32(arguments + 4);
    LxGuestBorrow output = {0};
    if (pointer && !lx_execution_borrow(GLoaderState.execution, pointer, 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 count = 0;
    const uint32 result = reset ? DosResetEventSem(lx_guest_load_u32(arguments), &count) :
                                 DosQueryEventSem(lx_guest_load_u32(arguments), &count);
    if (output.host && (!result || result == ERROR_ALREADY_RESET)) lx_guest_store_u32(output.host, count);
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosResetEventSem(uint8 *a) { return wireQueryEvent(a, 1); }
static uint32 bridge32_DosQueryEventSem(uint8 *a) { return wireQueryEvent(a, 0); }

static uint32 bridge32_DosQueryModuleName(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 4);
    LxGuestBorrow output;
    if (!size || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), size,
                                       LX_GUEST_WRITE, &output)) return ERROR_INVALID_PARAMETER;
    const uint32 result = DosQueryModuleName(lx_guest_load_u32(a), size, output.host);
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosQueryModuleHandle(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4,
                              LX_GUEST_WRITE, &output)) { free(name); return ERROR_INVALID_PARAMETER; }
    HMODULE value = 0;
    const uint32 result = DosQueryModuleHandle(name, &value);
    if (!result) lx_guest_store_u32(output.host, value);
    lx_execution_release(&output); free(name); return result;
}

static uint32 bridge32_DosGetDateTime(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 11, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    DATETIME value;
    const uint32 result = DosGetDateTime(&value);
    if (!result) {
        uint8 *p = output.host;
        p[0] = value.hours; p[1] = value.minutes; p[2] = value.seconds; p[3] = value.hundredths;
        p[4] = value.day; p[5] = value.month; lx_guest_store_u16(p + 6, value.year);
        lx_guest_store_u16(p + 8, value.timezone); p[10] = value.weekday;
    }
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosOpen(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name) return ERROR_INVALID_PARAMETER;
    if (lx_guest_load_u32(a + 28)) { free(name); return ERROR_OPEN_FAILED; }
    LxGuestBorrow handle = {0}, action = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE, &handle) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE, &action)) {
        lx_execution_release(&handle); free(name); return ERROR_INVALID_PARAMETER;
    }
    HFILE hf = 0; ULONG taken = 0;
    const uint32 result = DosOpen(name, &hf, &taken, lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16),
                                  lx_guest_load_u32(a + 20), lx_guest_load_u32(a + 24), NULL);
    if (!result) { lx_guest_store_u32(handle.host, hf); lx_guest_store_u32(action.host, taken); }
    lx_execution_release(&handle); lx_execution_release(&action); free(name); return result;
}
static uint32 bridge32_DosSetFilePtr(uint8 *a)
{
    LxGuestBorrow output = {0};
    const uint32 pointer = lx_guest_load_u32(a + 12);
    if (pointer && !lx_execution_borrow(GLoaderState.execution, pointer, 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    ULONG position = 0;
    const uint32 result = DosSetFilePtr(lx_guest_load_u32(a), (LONG) lx_guest_load_u32(a + 4),
                                       lx_guest_load_u32(a + 8), &position);
    if (!result && output.host) lx_guest_store_u32(output.host, position);
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosDelete(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name) return ERROR_INVALID_PARAMETER;
    const uint32 result = DosDelete(name); free(name); return result;
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
    char *name = path ? lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH) : NULL;
    if (path && !name) return ERROR_INVALID_PARAMETER;
    if (level != FIL_STANDARD && level != FIL_QUERYEASIZE && !(path && (level == FIL_QUERYFULLNAME || level == 7))) {
        free(name); return ERROR_INVALID_LEVEL;
    }
    const uint32 wire_size = level == FIL_STANDARD ? 24 : level == FIL_QUERYEASIZE ? 28 : size;
    if (size < wire_size || !wire_size) { free(name); return ERROR_BUFFER_OVERFLOW; }
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, pointer, wire_size, LX_GUEST_WRITE, &output)) {
        free(name); return ERROR_INVALID_PARAMETER;
    }
    FILESTATUS4 value = {0};
    void *data = (level == FIL_STANDARD || level == FIL_QUERYEASIZE) ? (void *) &value : output.host;
    const uint32 capacity = data == output.host ? size : sizeof value;
    const uint32 result = path ? DosQueryPathInfo(name, level, data, capacity) :
                                 DosQueryFileInfo(lx_guest_load_u32(a), level, data, capacity);
    if (!result && data != output.host) {
        uint8 *p = output.host;
        wireDateTime(p, value.fdateCreation, value.ftimeCreation);
        wireDateTime(p + 4, value.fdateLastAccess, value.ftimeLastAccess);
        wireDateTime(p + 8, value.fdateLastWrite, value.ftimeLastWrite);
        lx_guest_store_u32(p + 12, value.cbFile); lx_guest_store_u32(p + 16, value.cbFileAlloc);
        lx_guest_store_u32(p + 20, value.attrFile);
        if (level == FIL_QUERYEASIZE) lx_guest_store_u32(p + 24, value.cbList);
    }
    lx_execution_release(&output); free(name); return result;
}
static uint32 bridge32_DosQueryFileInfo(uint8 *a) { return wireQueryFileInfo(a, 0); }
static uint32 bridge32_DosQueryPathInfo(uint8 *a) { return wireQueryFileInfo(a, 1); }

static uint32 wireFind(uint8 *a, int first)
{
    const uint32 pointer = lx_guest_load_u32(a + (first ? 12 : 4));
    const uint32 size = lx_guest_load_u32(a + (first ? 16 : 8));
    if (size > 0xFFFF) return ERROR_INVALID_PARAMETER;
    const uint32 count_pointer = lx_guest_load_u32(a + (first ? 20 : 12));
    const uint32 level = first ? lx_guest_load_u32(a + 24) : 0;
    char *name = first ? lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH) : NULL;
    if (first && !name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow handle = {0}, data = {0}, count = {0};
    if ((first && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, 3, &handle)) ||
        (size && !lx_execution_borrow(GLoaderState.execution, pointer, size, LX_GUEST_WRITE, &data)) ||
        !lx_execution_borrow(GLoaderState.execution, count_pointer, 4, 3, &count)) {
        lx_execution_release(&handle); lx_execution_release(&data); free(name); return ERROR_INVALID_PARAMETER;
    }
    ULONG matches = lx_guest_load_u32(count.host);
    HDIR value = first ? lx_guest_load_u32(handle.host) : lx_guest_load_u32(a);
    const uint32 result = first ? DosFindFirst(name, &value, lx_guest_load_u32(a + 8), data.host, size, &matches, level) :
                                  DosFindNext(value, data.host, size, &matches);
    lx_guest_store_u32(count.host, matches);
    if (!result && handle.host) lx_guest_store_u32(handle.host, value);
    lx_execution_release(&handle); lx_execution_release(&data); lx_execution_release(&count);
    free(name); return result;
}
static uint32 bridge32_DosFindFirst(uint8 *a) { return wireFind(a, 1); }
static uint32 bridge32_DosFindNext(uint8 *a) { return wireFind(a, 0); }
static uint32 bridge32_DosFindClose(uint8 *a) { return DosFindClose(lx_guest_load_u32(a)); }

static uint32 bridge32_DosQueryCurrentDisk(uint8 *a)
{
    LxGuestBorrow disk = {0}, logical = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE, &disk) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE, &logical)) {
        lx_execution_release(&disk); return ERROR_INVALID_PARAMETER;
    }
    ULONG drive = 0, mask = 0;
    const uint32 result = DosQueryCurrentDisk(&drive, &mask);
    if (!result) { lx_guest_store_u32(disk.host, drive); lx_guest_store_u32(logical.host, mask); }
    lx_execution_release(&disk); lx_execution_release(&logical); return result;
}
static uint32 bridge32_DosQueryCurrentDir(uint8 *a)
{
    LxGuestBorrow length = {0}, data = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 4, 3, &length))
        return ERROR_INVALID_PARAMETER;
    ULONG size = lx_guest_load_u32(length.host);
    if (size && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), size, LX_GUEST_WRITE, &data)) {
        lx_execution_release(&length); return ERROR_INVALID_PARAMETER;
    }
    const uint32 result = DosQueryCurrentDir(lx_guest_load_u32(a), data.host, &size);
    lx_guest_store_u32(length.host, size);
    lx_execution_release(&data); lx_execution_release(&length); return result;
}
static uint32 bridge32_DosQueryCp(uint8 *a)
{
    const uint32 capacity = lx_guest_load_u32(a);
    if (capacity < 8) return ERROR_CPLIST_TOO_SMALL;
    LxGuestBorrow list = {0}, length = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 8, LX_GUEST_WRITE, &list) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE, &length)) {
        lx_execution_release(&list); return ERROR_INVALID_PARAMETER;
    }
    ULONG values[2] = {0}, size = 0;
    const uint32 result = DosQueryCp(sizeof values, values, &size);
    if (!result) {
        lx_guest_store_u32(list.host, values[0]); lx_guest_store_u32((uint8 *) list.host + 4, values[1]);
        lx_guest_store_u32(length.host, size);
    }
    lx_execution_release(&list); lx_execution_release(&length); return result;
}
static uint32 bridge32_DosSetFileSize(uint8 *a) { return DosSetFileSize(lx_guest_load_u32(a), lx_guest_load_u32(a + 4)); }
static uint32 bridge32_DosError(uint8 *a) { return DosError(lx_guest_load_u32(a)); }
static uint32 bridge32_DosAcknowledgeSignalException(uint8 *a) { return DosAcknowledgeSignalException(lx_guest_load_u32(a)); }

static uint32 bridge32_DosAllocThreadLocalMemory(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    ULONG *address = NULL;
    const uint32 result = DosAllocThreadLocalMemory(lx_guest_load_u32(a), &address);
    if (!result) lx_guest_store_u32(output.host, (uint32) (uintptr_t) address);
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosFreeThreadLocalMemory(uint8 *a)
{
    return DosFreeThreadLocalMemory((ULONG *) (uintptr_t) lx_guest_load_u32(a));
}

static uint32 bridge32_DosSetMem(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 4);
    LxGuestBorrow memory;
    if (!size || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), size, 0, &memory))
        return ERROR_INVALID_ADDRESS;
    const uint32 result = DosSetMem(memory.host, size, lx_guest_load_u32(a + 8));
    lx_execution_release(&memory); return result;
}

static uint32 bridge32_DosScanEnv(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE, &output)) {
        free(name); return ERROR_INVALID_PARAMETER;
    }
    char *value = NULL;
    uint32 result = DosScanEnv(name, &value), address;
    if (!result) {
        if (lx_execution_address(GLoaderState.execution, value, 1, LX_GUEST_READ, &address))
            lx_guest_store_u32(output.host, address);
        else result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&output); free(name); return result;
}
static uint32 bridge32_DosSearchPath(uint8 *a)
{
    char *path = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), 65536);
    char *file = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 8), CCHMAXPATH);
    const uint32 size = lx_guest_load_u32(a + 16);
    LxGuestBorrow output = {0};
    if (!path || !file || !size || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), size,
                                                      LX_GUEST_WRITE, &output)) {
        free(path); free(file); return ERROR_INVALID_PARAMETER;
    }
    const uint32 result = DosSearchPath(lx_guest_load_u32(a), path, file, output.host, size);
    lx_execution_release(&output); free(path); free(file); return result;
}
static uint32 bridge32_DosWaitNPipe(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name) return ERROR_INVALID_PARAMETER;
    const uint32 result = DosWaitNPipe(name, lx_guest_load_u32(a + 4)); free(name); return result;
}
static uint32 bridge32_DosForceDelete(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    if (!name) return ERROR_INVALID_PARAMETER;
    const uint32 result = DosForceDelete(name); free(name); return result;
}

static uint32 bridge32_DosQueryFHState(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    ULONG mode = 0;
    const uint32 result = DosQueryFHState(lx_guest_load_u32(a), &mode);
    if (!result) lx_guest_store_u32(output.host, mode);
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosResetBuffer(uint8 *a) { return DosResetBuffer(lx_guest_load_u32(a)); }
static uint32 bridge32_DosSleep(uint8 *a) { return DosSleep(lx_guest_load_u32(a)); }
static uint32 bridge32_DosSetDefaultDisk(uint8 *a) { return DosSetDefaultDisk(lx_guest_load_u32(a)); }

static uint32 wireSharedMemory(uint8 *a, int create)
{
    uint32 name_pointer = lx_guest_load_u32(a + 4);
    char *name = name_pointer ? lx_execution_copy_string(GLoaderState.execution, name_pointer, CCHMAXPATH) : NULL;
    if (name_pointer && !name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE, &output)) {
        free(name); return ERROR_INVALID_PARAMETER;
    }
    void *memory = NULL;
    uint32 result = create ? DosAllocSharedMem(&memory, name, lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12)) :
                             DosGetNamedSharedMem(&memory, name, lx_guest_load_u32(a + 8));
    uint32 address;
    if (!result) {
        if (lx_execution_address(GLoaderState.execution, memory, 1, 0, &address)) lx_guest_store_u32(output.host, address);
        else result = ERROR_INVALID_ADDRESS;
    }
    lx_execution_release(&output); free(name); return result;
}
static uint32 bridge32_DosGetNamedSharedMem(uint8 *a) { return wireSharedMemory(a, 0); }
static uint32 bridge32_DosAllocSharedMem(uint8 *a) { return wireSharedMemory(a, 1); }

static uint32 bridge32_DosExecPgm(uint8 *a)
{
    const uint32 size = lx_guest_load_u32(a + 4), args_pointer = lx_guest_load_u32(a + 12);
    const uint32 env_pointer = lx_guest_load_u32(a + 16);
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 24), CCHMAXPATH);
    char *args = args_pointer ? lx_execution_copy_strings(GLoaderState.execution, args_pointer, 65536, 2) : NULL;
    char *env = env_pointer ? lx_execution_copy_strings(GLoaderState.execution, env_pointer, 65536, 0) : NULL;
    LxGuestBorrow error = {0}, result = {0};
    if (!name || (args_pointer && !args) || (env_pointer && !env) || size > INT32_MAX ||
        (size && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), size, LX_GUEST_WRITE, &error)) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 20), 8, LX_GUEST_WRITE, &result)) {
        lx_execution_release(&error); free(name); free(args); free(env); return ERROR_INVALID_PARAMETER;
    }
    RESULTCODES codes = {0};
    const uint32 status = DosExecPgm(error.host, size, lx_guest_load_u32(a + 8), args, env, &codes, name);
    if (!status) {
        lx_guest_store_u32(result.host, codes.codeTerminate);
        lx_guest_store_u32((uint8 *) result.host + 4, codes.codeResult);
    }
    lx_execution_release(&error); lx_execution_release(&result); free(name); free(args); free(env); return status;
}

static uint32 bridge32_DosCreateThread(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    TID id = 0;
    const uint32 result = DosCreateThread(&id, (PFNTHREAD) (uintptr_t) lx_guest_load_u32(a + 4),
        lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16));
    if (!result) lx_guest_store_u32(output.host, id);
    lx_execution_release(&output); return result;
}
static uint32 bridge32_DosWaitThread(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 4, 3, &output))
        return ERROR_INVALID_PARAMETER;
    TID id = lx_guest_load_u32(output.host);
    const uint32 result = DosWaitThread(&id, lx_guest_load_u32(a + 4));
    if (!result) lx_guest_store_u32(output.host, id);
    lx_execution_release(&output); return result;
}

static uint32 bridge32_DosCopy(uint8 *a)
{
    char *from = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    char *to = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), CCHMAXPATH);
    if (!from || !to) { free(from); free(to); return ERROR_INVALID_PARAMETER; }
    const uint32 result = DosCopy(from, to, lx_guest_load_u32(a + 8)); free(from); free(to); return result;
}

static uint32 bridge32_DosSetMaxFH(uint8 *a) { return DosSetMaxFH(lx_guest_load_u32(a)); }
static uint32 bridge32_DosSetProcessCp(uint8 *a) { return DosSetProcessCp(lx_guest_load_u32(a)); }
static uint32 bridge32_DosUnwindException(uint8 *a) { (void) a; return ERROR_NOT_SUPPORTED; }

static void wireLoadDateTime(const uint8 *p, FDATE *date, FTIME *time)
{
    uint16 d = lx_guest_load_u16(p), t = lx_guest_load_u16(p + 2);
    date->day = d & 31; date->month = (d >> 5) & 15; date->year = d >> 9;
    time->twosecs = t & 31; time->minutes = (t >> 5) & 63; time->hours = t >> 11;
}
static uint32 bridge32_DosSetFileInfo(uint8 *a)
{
    if (lx_guest_load_u32(a + 4) != FIL_STANDARD) return ERROR_INVALID_LEVEL;
    if (lx_guest_load_u32(a + 12) < 24) return ERROR_BUFFER_OVERFLOW;
    LxGuestBorrow input;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 24, LX_GUEST_READ, &input))
        return ERROR_INVALID_PARAMETER;
    const uint8 *p = input.host;
    FILESTATUS3 value = {0};
    wireLoadDateTime(p, &value.fdateCreation, &value.ftimeCreation);
    wireLoadDateTime(p + 4, &value.fdateLastAccess, &value.ftimeLastAccess);
    wireLoadDateTime(p + 8, &value.fdateLastWrite, &value.ftimeLastWrite);
    value.cbFile = lx_guest_load_u32(p + 12); value.cbFileAlloc = lx_guest_load_u32(p + 16);
    value.attrFile = lx_guest_load_u32(p + 20);
    const uint32 result = DosSetFileInfo(lx_guest_load_u32(a), FIL_STANDARD, &value, sizeof value);
    lx_execution_release(&input); return result;
}

static uint32 bridge32_DosDupHandle(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, 3, &output))
        return ERROR_INVALID_PARAMETER;
    HFILE handle = lx_guest_load_u32(output.host);
    const uint32 result = DosDupHandle(lx_guest_load_u32(a), &handle);
    if (!result) lx_guest_store_u32(output.host, handle);
    lx_execution_release(&output); return result;
}

static uint32 bridge32_DosMove(uint8 *a)
{
    char *from = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), CCHMAXPATH);
    char *to = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), CCHMAXPATH);
    if (!from || !to) { free(from); free(to); return ERROR_INVALID_PARAMETER; }
    const uint32 result = DosMove(from, to); free(from); free(to); return result;
}
