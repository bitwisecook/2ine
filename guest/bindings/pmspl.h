/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_SplQueryQueue(uint8 *a)
{
    uint32 needed = lx_guest_load_u32(a + 20);
    if (!lx_execution_memory(GLoaderState.execution, needed, 4, LX_GUEST_WRITE))
        return 87;
    ULONG bytes;
    ULONG result = SplQueryQueue(NULL, NULL, lx_guest_load_u32(a + 8), NULL,
                                 lx_guest_load_u32(a + 16), &bytes);
    return lx_execution_write(GLoaderState.execution, needed, &bytes, 4) ? result : 87;
}
