/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_DrgAddStrHandle(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 65536);
    if (!name)
        return 0;
    ULONG result = DrgAddStrHandle(name);
    free(name);
    return result;
}
static uint32 bridge32_DrgDeleteStrHandle(uint8 *a)
{
    return DrgDeleteStrHandle(lx_guest_load_u32(a));
}
static uint32 bridge32_DrgQueryStrNameLen(uint8 *a)
{
    return DrgQueryStrNameLen(lx_guest_load_u32(a));
}
static uint32 bridge32_DrgQueryStrName(uint8 *a)
{
    ULONG capacity = lx_guest_load_u32(a + 4);
    LxGuestBorrow span = {0};
    if (!capacity || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8),
                                          capacity, LX_GUEST_WRITE, &span))
        return 0;
    ULONG result = DrgQueryStrName(lx_guest_load_u32(a), capacity, span.host);
    lx_execution_release(&span);
    return result;
}
