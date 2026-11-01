/* Public domain. No rights reserved. Wholly AI-authored. */
/* Queue data addresses are opaque guest values, not host pointers to dereference. */
static uint32 bridge32_DosCreateQueue(uint8 *a)
{
    LxGuestBorrow output = {0};
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 8), 256);
    uint32 result = ERROR_INVALID_PARAMETER;
    if (name && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE,
                                    &output))
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
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 8), 256);
    uint32 result = ERROR_INVALID_PARAMETER;
    if (name &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 4, LX_GUEST_WRITE,
                            &pid) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
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
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
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
static uint32 queue_receive32(uint8 *a, BOOL peek)
{
    LxGuestBorrow outputs[5] = {{0}};
    const unsigned positions[] = {4, 8, 12, 24, 16};
    const unsigned sizes[] = {8, 4, 4, 1, 4};
    uint32 result = ERROR_INVALID_PARAMETER;
    for (unsigned i = 0; i < (peek ? 5u : 4u); i++)
        if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + positions[i]),
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
        lx_guest_store_u32(outputs[2].host, (uint32)(uintptr_t)data);
        *(uint8 *)outputs[3].host = priority;
        if (peek)
            lx_guest_store_u32(outputs[4].host, cursor);
    }
done:
    for (unsigned i = 0; i < 5; i++)
        lx_execution_release(&outputs[i]);
    return result;
}
static uint32 bridge32_DosPeekQueue(uint8 *a)
{
    return queue_receive32(a, TRUE);
}
static uint32 bridge32_DosReadQueue(uint8 *a)
{
    return queue_receive32(a, FALSE);
}
