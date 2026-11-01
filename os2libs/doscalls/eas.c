/* Public domain. No rights reserved. Wholly AI-authored. */
/* Family API packed extended attributes persist as opaque file metadata. */
#include "os2libs/doscalls/internal.h"
APIRET queryAllEas16(HFILE handle, void *operation)
{
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, operation, 12, LX_GUEST_READ, &address))
        return ERROR_INVALID_PARAMETER;
    uint32 pointer = lx_guest_load_u32((uint8 *)operation + 4);
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow_far(GLoaderState.execution, pointer, 4, LX_GUEST_READ | LX_GUEST_WRITE,
                                 &output))
        return ERROR_INVALID_PARAMETER;
    uint32 capacity = lx_guest_load_u32(output.host);
    lx_execution_release(&output);
    if (capacity < 4)
        return ERROR_BUFFER_OVERFLOW;
    LxHandleBorrow file;
    if (!borrowFile(handle, &file))
        return ERROR_INVALID_HANDLE;
    size_t size = 0;
    LxHostFsError error = lx_host_file_metadata_get(file.resource, NULL, 0, &size);
    if (error == LX_HOST_FS_INVALID)
        error = LX_HOST_FS_OK;
    if (!error && size > 65535)
    {
        lx_handle_release(&file);
        return ERROR_EA_LIST_INCONSISTENT;
    }
    uint32 required = size ? size : 4;
    APIRET result = fileError(error);
    if (!result &&
        !lx_execution_borrow_far(GLoaderState.execution, pointer,
                                 capacity < required ? 4 : required, LX_GUEST_WRITE, &output))
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
    uint32 pointer = lx_guest_load_u32((const uint8 *)operation + 4);
    if (!pointer)
        return NO_ERROR;
    LxGuestBorrow list = {0};
    if (!lx_execution_borrow_far(GLoaderState.execution, pointer, 4, LX_GUEST_READ, &list))
        return ERROR_INVALID_PARAMETER;
    uint32 size = lx_guest_load_u32(list.host);
    lx_execution_release(&list);
    if (size < 4 || size > 65535)
        return ERROR_EA_LIST_INCONSISTENT;
    if (!lx_execution_borrow_far(GLoaderState.execution, pointer, size, LX_GUEST_READ, &list))
        return ERROR_INVALID_PARAMETER;
    const uint8 *bytes = list.host;
    APIRET result = NO_ERROR;
    uint32 at = 4;
    while (at < size)
    {
        if (size - at < 5)
            break;
        const uint32 name = bytes[at + 1], value = lx_guest_load_u16(bytes + at + 2);
        const uint32 count = 5 + name + value;
        if (!name || count > size - at || bytes[at + 4 + name] || memchr(bytes + at + 4, 0, name))
            break;
        at += count;
    }
    if (at != size)
        result = ERROR_EA_LIST_INCONSISTENT;
    else if (size > 4)
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
