/* Public domain. No rights reserved. Wholly AI-authored. */
/* Checked guest strings, packed queue records and output counts. */
static uint32 spl_queue_bridge(uint8 *a, BOOL query)
{
    uint32 computer = lx_guest_load_u32(a), queue = query ? lx_guest_load_u32(a + 4) : 0;
    unsigned offset = query ? 4 : 0;
    uint32 level = lx_guest_load_u32(a + 4 + offset), address = lx_guest_load_u32(a + 8 + offset),
           capacity = lx_guest_load_u32(a + 12 + offset);
    uint32 counters[3] = {query ? 0 : lx_guest_load_u32(a + 16),
                          query ? 0 : lx_guest_load_u32(a + 20),
                          lx_guest_load_u32(a + (query ? 20 : 24))};
    if (capacity > 16 * 1024 * 1024 || (!query && lx_guest_load_u32(a + 28)))
        return 87;
    char *host = computer ? lx_execution_copy_string(GLoaderState.execution, computer, 256) : NULL,
         *name = queue ? lx_execution_copy_string(GLoaderState.execution, queue, 256) : NULL;
    if ((computer && !host) || (query && (!name || !*name)) || (host && *host))
    {
        free(host);
        free(name);
        return 87;
    }
    LxGuestBorrow buffer = {0}, counts[3] = {{0}};
    ULONG result = 87, returned = 0, total = 0, needed = 0;
    BOOL valid = !capacity || lx_execution_borrow(GLoaderState.execution, address, capacity,
                                                  LX_GUEST_WRITE, &buffer);
    for (unsigned i = query ? 2 : 0; valid && i < 3; i++)
        valid =
            lx_execution_borrow(GLoaderState.execution, counters[i], 4, LX_GUEST_WRITE, &counts[i]);
    if (valid)
    {
        result = lx_spl_catalogue(name, level, buffer.host, capacity, address, &returned, &total,
                                  &needed);
        if (result == 0 || result == 234 || result == 2150)
        {
            if (!query)
            {
                lx_guest_store_u32(counts[0].host, returned);
                lx_guest_store_u32(counts[1].host, total);
            }
            lx_guest_store_u32(counts[2].host, needed);
        }
    }
    for (unsigned i = 0; i < 3; i++)
        lx_execution_release(&counts[i]);
    lx_execution_release(&buffer);
    free(host);
    free(name);
    return result;
}
static uint32 bridge32_SplQueryQueue(uint8 *a)
{
    return spl_queue_bridge(a, TRUE);
}
static uint32 bridge32_SplEnumQueue(uint8 *a)
{
    return spl_queue_bridge(a, FALSE);
}

static uint32 bridge32_SplQueryDevice(uint8 *a)
{
    uint32 computer = lx_guest_load_u32(a), device = lx_guest_load_u32(a + 4),
           capacity = lx_guest_load_u32(a + 16), address = lx_guest_load_u32(a + 12);
    if (capacity > 16 * 1024 * 1024)
        return 87;
    char *host = computer ? lx_execution_copy_string(GLoaderState.execution, computer, 256) : NULL,
         *name = device ? lx_execution_copy_string(GLoaderState.execution, device, 256) : NULL;
    LxGuestBorrow buffer = {0}, count = {0};
    ULONG result = 87, needed = 0;
    if ((!computer || (host && !*host)) && name && *name &&
        (!capacity ||
         lx_execution_borrow(GLoaderState.execution, address, capacity, LX_GUEST_WRITE, &buffer)) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 20), 4, LX_GUEST_WRITE,
                            &count))
    {
        result =
            lx_spl_device(name, lx_guest_load_u32(a + 8), buffer.host, capacity, address, &needed);
        if (result == 0 || result == 234 || result == 2152)
            lx_guest_store_u32(count.host, needed);
    }
    lx_execution_release(&count);
    lx_execution_release(&buffer);
    free(host);
    free(name);
    return result;
}
static uint32 bridge32_SplEnumPrinter(uint8 *a)
{
    ULONG computer = lx_guest_load_u32(a), address = lx_guest_load_u32(a + 12),
          capacity = lx_guest_load_u32(a + 16);
    if (capacity > 16 * 1024 * 1024 || lx_guest_load_u32(a + 32)) return 87;
    char *name = computer ? lx_execution_copy_string(GLoaderState.execution, computer, 256) : NULL;
    LxGuestBorrow buffer = {0}, counts[3] = {{0}};
    ULONG result = 87, returned = 0, total = 0, needed = 0;
    BOOL valid = (!computer || (name && !*name)) && (!capacity ||
        lx_execution_borrow(GLoaderState.execution, address, capacity, LX_GUEST_WRITE, &buffer));
    for (unsigned i = 0; valid && i < 3; i++)
        valid = lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 20 + i * 4),
                                     4, LX_GUEST_WRITE, &counts[i]);
    if (valid)
    {
        result = lx_spl_printers(lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8), buffer.host,
                                 capacity, address, &returned, &total, &needed);
        if (!result || result == 234)
        {
            lx_guest_store_u32(counts[0].host, returned);
            lx_guest_store_u32(counts[1].host, total);
            lx_guest_store_u32(counts[2].host, needed);
        }
    }
    for (unsigned i = 0; i < 3; i++) lx_execution_release(&counts[i]);
    lx_execution_release(&buffer); free(name); return result;
}
