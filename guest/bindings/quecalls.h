/* Public domain. No rights reserved. Wholly AI-authored. */
/* Flat queue bindings translate pointer outputs independently of host pointer width. */
static uint32 queue32_open(uint8 *a, int create)
{
    uint32 first = lx_guest_load_u32(a), second = lx_guest_load_u32(a + 4);
    uint32 name_pointer = lx_guest_load_u32(a + 8);
    char *name = lx_execution_copy_string(GLoaderState.execution, name_pointer, 256);
    LxGuestBorrow output = {0}, owner = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if (name &&
        lx_execution_borrow(GLoaderState.execution, create ? first : second, 4, LX_GUEST_WRITE,
                            &output) &&
        (create || lx_execution_borrow(GLoaderState.execution, first, 4, LX_GUEST_WRITE, &owner)))
    {
        HQUEUE handle = 0;
        PID pid = 0;
        result = create ? DosCreateQueue(&handle, second, name) : DosOpenQueue(&pid, &handle, name);
        if (!result)
        {
            lx_guest_store_u32(output.host, handle);
            if (!create)
                lx_guest_store_u32(owner.host, pid);
        }
    }
    free(name);
    lx_execution_release(&output);
    lx_execution_release(&owner);
    return result;
}
static uint32 bridge32_DosCreateQueue(uint8 *a)
{
    return queue32_open(a, 1);
}
static uint32 bridge32_DosOpenQueue(uint8 *a)
{
    return queue32_open(a, 0);
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
    LxGuestBorrow output;
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
    uint32 size = lx_guest_load_u32(a + 8), pointer = lx_guest_load_u32(a + 12);
    if (size > QUEUE_PAYLOAD_LIMIT)
        return ERROR_INVALID_PARAMETER;
    LxGuestBorrow data = {0};
    if (size && !lx_execution_borrow(GLoaderState.execution, pointer, size, LX_GUEST_READ, &data))
        return ERROR_INVALID_PARAMETER;
    uint32 result =
        DosWriteQueue(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), size,
                      size ? data.host : (void *)(uintptr_t)pointer, lx_guest_load_u32(a + 16));
    lx_execution_release(&data);
    return result;
}
static uint32 queue32_receive(uint8 *a, int peek)
{
    LxGuestBorrow slots[5] = {{0}};
    static const unsigned offsets[] = {4, 8, 12, 24, 16};
    static const unsigned sizes[] = {8, 4, 4, 1, 4};
    uint32 result = ERROR_INVALID_PARAMETER;
    for (unsigned i = 0; i < (peek ? 5u : 4u); i++)
        if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + offsets[i]),
                                 sizes[i], i == 4 ? 3 : LX_GUEST_WRITE, &slots[i]))
            goto done;
    REQUESTDATA request;
    ULONG length, element = peek ? lx_guest_load_u32(slots[4].host) : lx_guest_load_u32(a + 16);
    BYTE priority;
    void *memory = NULL;
    result = peek ? DosPeekQueue(lx_guest_load_u32(a), &request, &length, &memory, &element,
                                 lx_guest_load_u32(a + 20), &priority, lx_guest_load_u32(a + 28))
                  : DosReadQueue(lx_guest_load_u32(a), &request, &length, &memory, element,
                                 lx_guest_load_u32(a + 20), &priority, lx_guest_load_u32(a + 28));
    if (!result)
    {
        uint32 address = (uint32)(uintptr_t)memory;
        if (length && memory &&
            !lx_execution_address(GLoaderState.execution, memory, length, LX_GUEST_READ, &address))
        {
            result = ERROR_QUE_MEMORY_ERROR;
            goto done;
        }
        lx_guest_store_u32(slots[0].host, request.pid);
        lx_guest_store_u32((uint8 *)slots[0].host + 4, request.ulData);
        lx_guest_store_u32(slots[1].host, length);
        lx_guest_store_u32(slots[2].host, address);
        *(uint8 *)slots[3].host = priority;
        if (peek)
            lx_guest_store_u32(slots[4].host, element);
    }
done:
    for (unsigned i = 0; i < 5; i++)
        lx_execution_release(&slots[i]);
    return result;
}
static uint32 bridge32_DosReadQueue(uint8 *a)
{
    return queue32_receive(a, 0);
}
static uint32 bridge32_DosPeekQueue(uint8 *a)
{
    return queue32_receive(a, 1);
}
