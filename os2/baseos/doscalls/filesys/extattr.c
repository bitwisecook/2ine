/* Public domain. No rights reserved. Wholly AI-authored. */
/* Family API packed extended attributes persist as opaque file metadata. */
#include "os2/baseos/doscalls/internal.h"

/* FEA: fEA, cbName, cbValue, then the NUL-terminated name and the value. */
enum
{
    FEALIST_HEADER_SIZE = EA_LIST_EMPTY_SIZE,
    FEA_NAME_LENGTH_OFFSET = 1,
    FEA_VALUE_LENGTH_OFFSET = 2,
    FEA_HEADER_SIZE = 4,
    FEA_MINIMUM_SIZE = FEA_HEADER_SIZE + 1,
    /* A file's EAs are limited to one 64 KB list. */
    EA_LIST_MAXIMUM = 0xFFFF
};

/* DENA2 is an FEA prefixed by oNextEntryOffset; entries start on ULONG boundaries. */
enum
{
    DENA2_FEA_OFFSET = 4,
    DENA2_ALIGNMENT = 4
};

APIRET queryAllEas16(HFILE handle, void *operation)
{
    uint32 address;
    if (!lx_execution_address(GProcessState.execution, operation, EAOP16_SIZE, LX_GUEST_READ,
                              &address))
        return ERROR_INVALID_PARAMETER;
    uint32 pointer = lx_guest_load_u32((uint8 *)operation + EAOP16_FEALIST_OFFSET);
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow_far(GProcessState.execution, pointer, FEALIST_HEADER_SIZE,
                                 LX_GUEST_READ | LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 capacity = lx_guest_load_u32(output.host);
    lx_execution_release(&output);
    if (capacity < FEALIST_HEADER_SIZE)
        return ERROR_BUFFER_OVERFLOW;
    LxHandleBorrow file;
    if (!borrowFile(handle, &file))
        return ERROR_INVALID_HANDLE;
    size_t size = 0;
    LxHostFsError error = lx_host_file_metadata_get(file.resource, NULL, 0, &size);
    if (error == LX_HOST_FS_INVALID)
        error = LX_HOST_FS_OK;
    if (!error && size > EA_LIST_MAXIMUM)
    {
        lx_handle_release(&file);
        return ERROR_EA_LIST_INCONSISTENT;
    }
    uint32 required = size ? size : EA_LIST_EMPTY_SIZE;
    APIRET result = fileError(error);
    if (!result && !lx_execution_borrow_far(GProcessState.execution, pointer,
                                            capacity < required ? FEALIST_HEADER_SIZE : required,
                                            LX_GUEST_WRITE, &output))
        result = ERROR_INVALID_PARAMETER;
    if (!result)
    {
        if (capacity < required)
            result = ERROR_BUFFER_OVERFLOW;
        else if (size)
            result =
                fileError(lx_host_file_metadata_get(file.resource, output.host, required, &size));
        if (!result || result == ERROR_BUFFER_OVERFLOW)
            lx_guest_store_u32(output.host, required);
    }
    lx_execution_release(&output);
    lx_handle_release(&file);
    return result;
}

APIRET applyEas16(HFILE handle, const void *operation)
{
    uint32 pointer = lx_guest_load_u32((const uint8 *)operation + EAOP16_FEALIST_OFFSET);
    if (!pointer)
        return NO_ERROR;
    LxGuestBorrow list = {0};
    if (!lx_execution_borrow_far(GProcessState.execution, pointer, FEALIST_HEADER_SIZE,
                                 LX_GUEST_READ, &list))
        return ERROR_INVALID_PARAMETER;
    uint32 size = lx_guest_load_u32(list.host);
    lx_execution_release(&list);
    if (size < FEALIST_HEADER_SIZE || size > EA_LIST_MAXIMUM)
        return ERROR_EA_LIST_INCONSISTENT;
    if (!lx_execution_borrow_far(GProcessState.execution, pointer, size, LX_GUEST_READ, &list))
        return ERROR_INVALID_PARAMETER;
    const uint8 *bytes = list.host;
    APIRET result = NO_ERROR;
    uint32 at = FEALIST_HEADER_SIZE;
    while (at < size)
    {
        if (size - at < FEA_MINIMUM_SIZE)
            break;
        const uint32 name = bytes[at + FEA_NAME_LENGTH_OFFSET],
                     value = lx_guest_load_u16(bytes + at + FEA_VALUE_LENGTH_OFFSET);
        const uint32 count = FEA_MINIMUM_SIZE + name + value;
        if (!name || count > size - at || bytes[at + FEA_HEADER_SIZE + name] ||
            memchr(bytes + at + FEA_HEADER_SIZE, 0, name))
            break;
        at += count;
    }
    if (at != size)
        result = ERROR_EA_LIST_INCONSISTENT;
    else if (size > FEALIST_HEADER_SIZE)
    {
        LxHandleBorrow file;
        if (!borrowFile(handle, &file))
            result = ERROR_INVALID_HANDLE;
        else
        {
            LxHostFsError error = lx_host_file_metadata_set(file.resource, bytes, size);
            result = error == LX_HOST_FS_INVALID ? ERROR_EAS_NOT_SUPPORTED : fileError(error);
            lx_handle_release(&file);
        }
    }
    lx_execution_release(&list);
    return result;
}

APIRET DosEnumAttribute(ULONG reference_type, PVOID reference, ULONG first, PVOID buffer,
                        ULONG capacity, PULONG count, ULONG level)
{
    if (!count || !reference || (!buffer && capacity) || !first ||
        reference_type > ENUMEA_REFTYPE_PATH)
        return ERROR_INVALID_PARAMETER;
    if (level != ENUMEA_LEVEL_NO_VALUE)
        return ERROR_INVALID_LEVEL;
    ULONG requested = *count;
    *count = 0;
    HFILE handle = 0;
    int opened = 0;
    APIRET result;
    if (reference_type == ENUMEA_REFTYPE_FHANDLE)
        memcpy(&handle, reference, sizeof handle);
    else
    {
        ULONG action;
        result = DosOpen(reference, &handle, &action, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
                         OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL);
        if (result)
            return result;
        opened = 1;
    }
    LxHandleBorrow file;
    if (!borrowFile(handle, &file))
        return ERROR_INVALID_HANDLE;
    size_t size = 0;
    LxHostFsError error = lx_host_file_metadata_get(file.resource, NULL, 0, &size);
    if (error == LX_HOST_FS_INVALID)
        error = LX_HOST_FS_OK;
    unsigned char *metadata = size ? malloc(size) : NULL;
    if (!error && size && !metadata)
        error = LX_HOST_FS_NO_MEMORY;
    if (!error && size)
        error = lx_host_file_metadata_get(file.resource, metadata, size, &size);
    lx_handle_release(&file);
    if (opened)
        DosClose(handle);
    result = fileError(error);
    if (result || !size || !requested)
    {
        free(metadata);
        return result;
    }
    if (size < FEALIST_HEADER_SIZE || lx_guest_load_u32(metadata) != size)
    {
        free(metadata);
        return ERROR_EA_LIST_INCONSISTENT;
    }
    size_t at = FEALIST_HEADER_SIZE, used = 0, previous = 0;
    ULONG index = 1;
    while (at < size && *count < requested)
    {
        if (size - at < FEA_MINIMUM_SIZE)
        {
            result = ERROR_EA_LIST_INCONSISTENT;
            break;
        }
        const unsigned char *entry = metadata + at;
        size_t name = entry[FEA_NAME_LENGTH_OFFSET],
               value = lx_guest_load_u16(entry + FEA_VALUE_LENGTH_OFFSET);
        size_t span = FEA_MINIMUM_SIZE + name + value;
        if (!name || span > size - at || entry[FEA_HEADER_SIZE + name] ||
            memchr(entry + FEA_HEADER_SIZE, 0, name))
        {
            result = ERROR_EA_LIST_INCONSISTENT;
            break;
        }
        if (index++ >= first)
        {
            /* ENUMEA_LEVEL_NO_VALUE copies the FEA header and name, not the value. */
            size_t output_size = DENA2_FEA_OFFSET + FEA_MINIMUM_SIZE + name;
            size_t aligned = (used + DENA2_ALIGNMENT - 1) & ~(size_t)(DENA2_ALIGNMENT - 1);
            if (aligned > capacity || output_size > capacity - aligned)
            {
                result = *count ? NO_ERROR : ERROR_BUFFER_OVERFLOW;
                break;
            }
            unsigned char *out = (unsigned char *)buffer + aligned;
            if (*count)
                lx_guest_store_u32((unsigned char *)buffer + previous, aligned - previous);
            lx_guest_store_u32(out, 0);
            memcpy(out + DENA2_FEA_OFFSET, entry, FEA_MINIMUM_SIZE + name);
            previous = aligned;
            used = aligned + output_size;
            (*count)++;
        }
        at += span;
    }
    free(metadata);
    return result;
}
