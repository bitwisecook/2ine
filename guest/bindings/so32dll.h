/* Public domain. No rights reserved. Wholly AI-authored. */
/* Flat socket calls pin guest buffers and copy fixed-width legacy addresses. */
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
static uint32 bridge32_Os2LegacyIoctl(uint8 *a)
{
    LxGuestBorrow value = {0};
    uint32 result = (uint32)-1;
    if (lx_execution_borrow(GLoaderState.execution, socket_word(a, 2), 4,
                            LX_GUEST_READ | LX_GUEST_WRITE, &value))
        result =
            Os2LegacyIoctl(socket_word(a, 0), socket_word(a, 1), value.host, socket_word(a, 3));
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
