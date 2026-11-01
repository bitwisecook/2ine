/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_Gre32Entry3(uint8 *a)
{
    return Gre32Entry3(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}
static uint32 bridge32_Gre32Entry5(uint8 *a)
{
    return Gre32Entry5(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8),
                       lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16));
}
