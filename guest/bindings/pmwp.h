/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_WinSetObjectData(uint8 *a)
{
    return WinSetObjectData(lx_guest_load_u32(a), NULL);
}

static uint32 bridge32_WinIsSomDDReady(uint8 *a)
{
    (void)a;
    return WinIsSomDDReady();
}

static uint32 bridge32_WinRestartSomDD(uint8 *a)
{
    return WinRestartSomDD(lx_guest_load_u32(a));
}
