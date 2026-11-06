/* Public domain. No rights reserved. Wholly AI-authored. */
/* Flat socket calls pin guest buffers and copy fixed-width legacy addresses. */
static uint32 bridge32_Os2LegacyGetHostId(uint8 *a)
{
    (void)a;
    return Os2LegacyGetHostId();
}
static uint32 socket_word(const uint8 *a, unsigned i)
{
    return lx_guest_load_u32(a + i * 4);
}
static uint32 bridge32_Os2LegacySocket(uint8 *a)
{
    return Os2LegacySocket(socket_word(a, 0), socket_word(a, 1), socket_word(a, 2));
}
static uint32 bridge32_Os2LegacySocketClose(uint8 *a)
{
    return Os2LegacySocketClose(socket_word(a, 0));
}
static uint32 bridge32_Os2LegacySocketError(uint8 *a)
{
    (void)a;
    return Os2LegacySocketError();
}
static uint32 bridge32_Os2LegacySocketInit(uint8 *a)
{
    (void)a;
    return Os2LegacySocketInit();
}
static uint32 bridge32_Os2LegacyBind(uint8 *a)
{
    Os2LegacyAddress address;
    if (!lx_execution_read(GLoaderState.execution, socket_word(a, 1), &address, sizeof address))
        return (uint32)-1;
    return Os2LegacyBind(socket_word(a, 0), &address, socket_word(a, 2));
}
static uint32 bridge32_Os2LegacyConnect(uint8 *a)
{
    Os2LegacyAddress address;
    if (!lx_execution_read(GLoaderState.execution, socket_word(a, 1), &address, sizeof address))
        return (uint32)-1;
    return Os2LegacyConnect(socket_word(a, 0), &address, socket_word(a, 2));
}
static uint32 bridge32_Os2LegacyListen(uint8 *a)
{
    return Os2LegacyListen(socket_word(a, 0), socket_word(a, 1));
}
static uint32 bridge32_Os2LegacyShutdown(uint8 *a)
{
    return Os2LegacyShutdown(socket_word(a, 0), socket_word(a, 1));
}
static uint32 bridge32_Os2LegacyAccept(uint8 *a)
{
    LxGuestBorrow output = {0}, count = {0};
    uint32 result = (uint32)-1;
    if (!socket_word(a, 1)) return Os2LegacyAccept(socket_word(a, 0), NULL, NULL);
    if (lx_execution_borrow(GLoaderState.execution, socket_word(a, 2), 4,
                            LX_GUEST_READ | LX_GUEST_WRITE, &count))
    {
        int length = lx_guest_load_u32(count.host);
        Os2LegacyAddress address;
        if (length >= (int)sizeof address && lx_execution_borrow(GLoaderState.execution,
                socket_word(a, 1), sizeof address, LX_GUEST_WRITE, &output))
        {
            result = Os2LegacyAccept(socket_word(a, 0), &address, &length);
            if ((int32_t)result >= 0)
            {
                memcpy(output.host, &address, sizeof address);
                lx_guest_store_u32(count.host, length);
            }
        }
    }
    lx_execution_release(&output);
    lx_execution_release(&count);
    return result;
}
static uint32 socket_transfer(uint8 *a, int writing)
{
    LxGuestBorrow data = {0};
    uint32 length = socket_word(a, 2), result = (uint32)-1;
    if (length <= INT32_MAX && lx_execution_borrow(GLoaderState.execution, socket_word(a, 1),
            length, writing ? LX_GUEST_READ : LX_GUEST_WRITE, &data))
        result = writing ? Os2LegacySend(socket_word(a, 0), data.host, length, socket_word(a, 3)) :
                           Os2LegacyRecv(socket_word(a, 0), data.host, length, socket_word(a, 3));
    lx_execution_release(&data);
    return result;
}
static uint32 bridge32_Os2LegacySend(uint8 *a) { return socket_transfer(a, 1); }
static uint32 bridge32_Os2LegacyRecv(uint8 *a) { return socket_transfer(a, 0); }
static uint32 bridge32_Os2LegacyIoctl(uint8 *a)
{
    LxGuestBorrow value = {0};
    uint32 result = (uint32)-1;
    uint32 length = socket_word(a, 3), command = socket_word(a, 1);
    if (length > INT32_MAX)
        return result;
    if (lx_execution_borrow(GLoaderState.execution, socket_word(a, 2), length,
                            LX_GUEST_READ | LX_GUEST_WRITE, &value))
    {
        if ((command & 0xffff) == 0x6914 && length >= 8)
        {
            LxGuestBorrow entries = {0};
            uint32 capacity = lx_guest_load_u32(value.host);
            size_t used;
            if (capacity <= INT32_MAX && lx_execution_borrow(GLoaderState.execution,
                    lx_guest_load_u32((uint8 *)value.host + 4), capacity, LX_GUEST_WRITE, &entries))
            {
                result = socket_result(lx_os2_network_ifconf(entries.host, capacity, &used, 0));
                if (!result) lx_guest_store_u32(value.host, (uint32)used);
            }
            lx_execution_release(&entries);
        }
        else
        result =
            Os2LegacyIoctl(socket_word(a, 0), command, value.host, length);
    }
    lx_execution_release(&value);
    return result;
}
static uint32 bridge32_Os2LegacySendTo(uint8 *a)
{
    LxGuestBorrow data = {0};
    Os2LegacyAddress address;
    uint32 result = (uint32)-1;
    uint32 length = socket_word(a, 2);
    if (length <= INT32_MAX &&
        lx_execution_read(GLoaderState.execution, socket_word(a, 4), &address, sizeof address) &&
        lx_execution_borrow(GLoaderState.execution, socket_word(a, 1), length, LX_GUEST_READ,
                            &data))
        result = Os2LegacySendTo(socket_word(a, 0), data.host, length, socket_word(a, 3), &address,
                                 socket_word(a, 5));
    lx_execution_release(&data);
    return result;
}
static uint32 bridge32_Os2LegacyRecvFrom(uint8 *a)
{
    LxGuestBorrow data = {0}, output = {0}, count = {0};
    Os2LegacyAddress address;
    int address_length = sizeof address;
    uint32 result = (uint32)-1;
    uint32 length = socket_word(a, 2), address_pointer = socket_word(a, 4);
    if (length > INT32_MAX || !lx_execution_borrow(GLoaderState.execution, socket_word(a, 1),
                                                   length, LX_GUEST_WRITE, &data))
        goto done;
    if (address_pointer)
    {
        if (!lx_execution_borrow(GLoaderState.execution, socket_word(a, 5), 4,
                                 LX_GUEST_READ | LX_GUEST_WRITE, &count))
            goto done;
        memcpy(&address_length, count.host, 4);
        if (address_length < (int)sizeof address ||
            !lx_execution_borrow(GLoaderState.execution, address_pointer, sizeof address,
                                 LX_GUEST_WRITE, &output))
            goto done;
    }
    result = Os2LegacyRecvFrom(socket_word(a, 0), data.host, length, socket_word(a, 3),
                               address_pointer ? &address : NULL, &address_length);
    if ((int32_t)result >= 0 && address_pointer)
    {
        memcpy(output.host, &address, sizeof address);
        memcpy(count.host, &address_length, 4);
    }
done:
    lx_execution_release(&data);
    lx_execution_release(&output);
    lx_execution_release(&count);
    return result;
}

static uint32 bridge32_Os2LegacyGetSocketName(uint8 *a)
{
    LxGuestBorrow output = {0}, count = {0};
    uint32 result = (uint32)-1;
    if (lx_execution_borrow(GLoaderState.execution, socket_word(a, 2), 4,
                            LX_GUEST_READ | LX_GUEST_WRITE, &count))
    {
        int length = lx_guest_load_u32(count.host);
        Os2LegacyAddress address;
        if (length >= (int)sizeof address &&
            lx_execution_borrow(GLoaderState.execution, socket_word(a, 1), sizeof address,
                                LX_GUEST_WRITE, &output))
        {
            result = Os2LegacyGetSocketName(socket_word(a, 0), &address, &length);
            if (!result)
            {
                memcpy(output.host, &address, sizeof address);
                lx_guest_store_u32(count.host, length);
            }
        }
    }
    lx_execution_release(&output);
    lx_execution_release(&count);
    return result;
}
static uint32 bridge32_Os2LegacySocketOption(uint8 *a)
{
    LxGuestBorrow value = {0};
    uint32 result = (uint32)-1;
    uint32 length = socket_word(a, 4);
    if (length <= INT32_MAX && lx_execution_borrow(GLoaderState.execution, socket_word(a, 3),
                                                   length, LX_GUEST_READ, &value))
        result = Os2LegacySocketOption(socket_word(a, 0), socket_word(a, 1), socket_word(a, 2),
                                       value.host, length);
    lx_execution_release(&value);
    return result;
}
