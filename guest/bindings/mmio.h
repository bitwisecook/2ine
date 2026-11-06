/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_mmioOpen(uint8 *a)
{
    LxGuestBorrow info = {0};
    if (lx_guest_load_u32(a) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 80,
                             LX_GUEST_READ | LX_GUEST_WRITE, &info))
        return 0;
    uint32 result = mmioOpen(NULL, info.host, lx_guest_load_u32(a + 8));
    lx_execution_release(&info);
    return result;
}
static uint32 bridge32_mmioClose(uint8 *a)
{
    return mmioClose(lx_guest_load_u32(a), (USHORT)lx_guest_load_u32(a + 4));
}
static uint32 mmio_transfer_bridge(uint8 *a, int writing)
{
    LxGuestBorrow data = {0};
    int32_t count = (int32_t)lx_guest_load_u32(a + 8);
    uint32 result = UINT32_MAX;
    if (!count && lx_guest_load_u32(a + 4))
        return 0;
    if (count >= 0 && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
            (size_t)count, writing ? LX_GUEST_READ : LX_GUEST_WRITE, &data))
        result = writing ? mmioWrite(lx_guest_load_u32(a), data.host, count)
                         : mmioRead(lx_guest_load_u32(a), data.host, count);
    lx_execution_release(&data);
    return result;
}
static uint32 bridge32_mmioRead(uint8 *a) { return mmio_transfer_bridge(a, 0); }
static uint32 bridge32_mmioWrite(uint8 *a) { return mmio_transfer_bridge(a, 1); }
static uint32 bridge32_mmioSeek(uint8 *a)
{
    return mmioSeek(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                     (LONG)lx_guest_load_u32(a + 8));
}
static uint32 bridge32_mmioGetHeader(uint8 *a)
{
    LxGuestBorrow data = {0}, count = {0};
    int32_t capacity = (int32_t)lx_guest_load_u32(a + 8);
    uint32 result = 0x1969;
    if (capacity > 0 && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
            (size_t)capacity, LX_GUEST_WRITE, &data) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), 4,
            LX_GUEST_WRITE, &count))
    {
        LONG bytes = 0;
        result = mmioGetHeader(lx_guest_load_u32(a), data.host, capacity, &bytes,
                               lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20));
        lx_guest_store_u32(count.host, bytes);
    }
    lx_execution_release(&count);
    lx_execution_release(&data);
    return result;
}
