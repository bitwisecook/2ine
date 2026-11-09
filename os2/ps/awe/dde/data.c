/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE data handles: guest memory objects with an owner, item and format. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include "os2/ps/awe/dde/dde.h"

DdeData *dde_data;

static void destroy_dde_data(void *resource)
{
    DdeData *data = resource;
    lx_host_monitor_lock(dde_lock);
    DdeData **link = &dde_data;
    while (*link && *link != data)
        link = &(*link)->next;
    if (*link)
        *link = data->next;
    lx_host_monitor_unlock(dde_lock);
    if (data->mapping)
        lx_execution_unmap(GProcessState.execution, data->mapping);
    free(data);
}

ULONG DdeCreateDataHandle(ULONG id, PVOID input, ULONG count, ULONG offset, ULONG item,
                          ULONG format, ULONG flags)
{
    LxHandleBorrow instance = {0};
    if (!dde_borrow_instance(id, &instance))
        return 0;
    DdeData *data = NULL;
    ULONG handle = 0;
    if ((flags & ~1u) || offset > 16u * 1024u * 1024u || count > 16u * 1024u * 1024u - offset)
    {
        ((DdeInstance *)instance.resource)->error = 0x4006;
        goto data_done;
    }
    data = calloc(1, sizeof *data);
    if (data)
        data->mapping = lx_execution_map(GProcessState.execution, 0,
                                         (((size_t)offset + count + 4095) & ~(size_t)4095)
                                             ? (((size_t)offset + count + 4095) & ~(size_t)4095)
                                             : 4096,
                                         3, 0);
    if (!data || !data->mapping)
    {
        if (data)
            free(data);
        ((DdeInstance *)instance.resource)->error = 0x4007;
        goto data_done;
    }
    data->owner = id;
    data->size = offset + count;
    data->item = item;
    data->format = format;
    data->flags = flags;
    if (input && count)
        memcpy((uint8 *)lx_execution_mapping_host(data->mapping) + offset, input, count);
    handle = lx_handle_create(LX_HANDLE_OPEN32_DDE_DATA, data, destroy_dde_data);
    if (!handle)
    {
        lx_execution_unmap(GProcessState.execution, data->mapping);
        free(data);
        ((DdeInstance *)instance.resource)->error = 0x4007;
        goto data_done;
    }
    data->handle = handle;
    lx_host_monitor_lock(dde_lock);
    data->next = dde_data;
    dde_data = data;
    lx_host_monitor_unlock(dde_lock);
data_done:
    lx_handle_release(&instance);
    return handle;
}

ULONG DdeAccessData(ULONG handle, PVOID size)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    data->accesses++;
    if (size)
        lx_guest_store_u32(size, data->size);
    ULONG address = lx_execution_mapping_address(data->mapping);
    lx_handle_release(&borrow);
    return address;
}

BOOL DdeUnaccessData(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return FALSE;
    DdeData *data = borrow.resource;
    BOOL valid = data->accesses != 0;
    if (valid)
        data->accesses--;
    lx_handle_release(&borrow);
    return valid;
}

BOOL DdeFreeDataHandle(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return FALSE;
    DdeData *data = borrow.resource;
    BOOL valid = !data->accesses;
    if (valid)
        valid = lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_DATA);
    else
        open32DdeSetError(data->owner, 0x4004);
    lx_handle_release(&borrow);
    return valid;
}

ULONG DdeAddData(ULONG handle, PVOID input, ULONG count, ULONG offset)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    ULONG result = 0;
    if (data->accesses || (count && !input) || offset > 16u * 1024u * 1024u ||
        count > 16u * 1024u * 1024u - offset)
    {
        open32DdeSetError(data->owner, 0x4006);
        goto add_done;
    }
    ULONG required = count + offset;
    if (required > lx_execution_mapping_size(data->mapping))
    {
        LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0,
                                                   ((size_t)required + 4095) & ~(size_t)4095, 3, 0);
        if (!mapping)
        {
            open32DdeSetError(data->owner, 0x4007);
            goto add_done;
        }
        memcpy(lx_execution_mapping_host(mapping), lx_execution_mapping_host(data->mapping),
               data->size);
        lx_execution_unmap(GProcessState.execution, data->mapping);
        data->mapping = mapping;
    }
    if (count)
        memcpy((uint8 *)lx_execution_mapping_host(data->mapping) + offset, input, count);
    if (required > data->size)
        data->size = required;
    result = handle;
add_done:
    lx_handle_release(&borrow);
    return result;
}

ULONG DdeGetData(ULONG handle, PVOID output, ULONG capacity, ULONG offset)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    ULONG count = offset > data->size ? 0 : data->size - offset;
    if (output)
    {
        if (count > capacity)
            count = capacity;
        if (count)
            memcpy(output, (uint8 *)lx_execution_mapping_host(data->mapping) + offset, count);
    }
    lx_handle_release(&borrow);
    return count;
}
