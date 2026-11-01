/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_WinCreateHelpInstance(uint8 *arguments)
{
    LxGuestBorrow init = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(arguments + 4), 44,
                             LX_GUEST_READ | LX_GUEST_WRITE, &init))
        return 0;
    const uint32 result = WinCreateHelpInstance(lx_guest_load_u32(arguments), init.host);
    lx_execution_release(&init);
    return result;
}
static uint32 bridge32_WinDestroyHelpInstance(uint8 *arguments)
{
    return WinDestroyHelpInstance(lx_guest_load_u32(arguments));
}
static uint32 bridge32_WinAssociateHelpInstance(uint8 *arguments)
{
    return WinAssociateHelpInstance(lx_guest_load_u32(arguments), lx_guest_load_u32(arguments + 4));
}
