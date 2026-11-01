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
