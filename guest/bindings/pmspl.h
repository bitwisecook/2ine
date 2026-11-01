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

static uint32 bridge32_SplEnumQueue(uint8 *a)
{
    LxGuestBorrow returned = {0}, total = {0}, needed = {0};
    ULONG rc = 87, nreturned, ntotal, nneeded;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 16), 4, LX_GUEST_WRITE,
                            &returned) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 20), 4, LX_GUEST_WRITE,
                            &total) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 24), 4, LX_GUEST_WRITE,
                            &needed))
    {
        rc = SplEnumQueue(NULL, lx_guest_load_u32(a + 4), NULL, lx_guest_load_u32(a + 12),
                          &nreturned, &ntotal, &nneeded,
                          (PVOID)(uintptr_t)lx_guest_load_u32(a + 28));
        if (rc == 0)
        {
            lx_guest_store_u32(returned.host, nreturned);
            lx_guest_store_u32(total.host, ntotal);
            lx_guest_store_u32(needed.host, nneeded);
        }
    }
    lx_execution_release(&needed);
    lx_execution_release(&total);
    lx_execution_release(&returned);
    return rc;
}
