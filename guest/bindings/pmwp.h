/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_WinSetObjectData(uint8 *a)
{
    return WinSetObjectData(lx_guest_load_u32(a), NULL);
}
