/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 tcpip_ioctl(uint8 *a, int has_length)
{
    uint32 command = lx_guest_load_u32(a + 4);
    uint32 length = has_length ? lx_guest_load_u32(a + 12) : (command >> 16) & 0x1fff;
    uint32 result = (uint32)-1;
    if (length > INT32_MAX)
        return result;
    LxGuestBorrow outer = {0}, entries = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), length,
                             LX_GUEST_READ | LX_GUEST_WRITE, &outer))
        return result;
    unsigned cmd = command & 0xffff;
    if ((cmd == 0x6914 || cmd == 0x6924) && length >= 8)
    {
        uint32 capacity = lx_guest_load_u32(outer.host);
        size_t used;
        if (capacity <= INT32_MAX &&
            lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32((uint8 *)outer.host + 4),
                                capacity, LX_GUEST_WRITE, &entries))
        {
            result = lx_os2_network_ifconf(entries.host, capacity, &used, 1);
            if (!result)
                lx_guest_store_u32(outer.host, (uint32)used);
        }
    }
    else
        result = OS2_os2_ioctl(lx_guest_load_u32(a), command, outer.host, length);
    lx_execution_release(&entries);
    lx_execution_release(&outer);
    return result;
}
static uint32 bridge32_OS2_ioctl(uint8 *a)
{
    return tcpip_ioctl(a, 0);
}
static uint32 bridge32_OS2_os2_ioctl(uint8 *a)
{
    return tcpip_ioctl(a, 1);
}
