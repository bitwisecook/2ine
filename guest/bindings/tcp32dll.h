/* Public domain. No rights reserved. Wholly AI-authored. */
/* Resolver pointers and pointer arrays are serialized into per-thread guest storage. */
static uint32 bridge32_Os2LegacyLongSwap(uint8 *a)
{
    return Os2LegacyLongSwap(lx_guest_load_u32(a));
}
static uint32 bridge32_Os2LegacyShortSwap(uint8 *a)
{
    return Os2LegacyShortSwap((uint16_t)lx_guest_load_u32(a));
}
static uint32 bridge32_Os2LegacyInetAddr(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 1024);
    uint32 result = Os2LegacyInetAddr(name);
    free(name);
    return result;
}
static uint32 bridge32_Os2LegacyGetHostName(uint8 *a)
{
    uint32 length = lx_guest_load_u32(a + 4);
    LxGuestBorrow output = {0};
    uint32 result = (uint32)-1;
    if (length && length <= INT32_MAX &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), length, LX_GUEST_WRITE,
                            &output))
        result = Os2LegacyGetHostName(output.host, (int)length);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_Os2LegacyGetHostByName(uint8 *a)
{
    enum
    {
        RESOLVER_STORAGE_SIZE = 4096,
        RESOLVER_ADDRESS_LIMIT = 32
    };
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 1024);
    Os2LegacyHost *host = Os2LegacyGetHostByName(name);
    free(name);
    if (!host || host->length != 4 || host->family != 2)
        return 0;
    if (!storage)
        storage = lx_execution_map(GLoaderState.execution, 0, RESOLVER_STORAGE_SIZE,
                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    uint8 *out = lx_execution_mapping_host(storage);
    uint32 base = lx_execution_mapping_address(storage);
    unsigned count = 0, aliases = 0;
    while (count < RESOLVER_ADDRESS_LIMIT && host->addresses[count])
        count++;
    while (aliases < RESOLVER_ADDRESS_LIMIT && host->aliases[aliases])
        aliases++;
    if (host->addresses[count] || host->aliases[aliases])
        return 0;
    unsigned addresses = 20 + (aliases + 1) * 4;
    unsigned data = addresses + (count + 1) * 4;
    size_t size = data + count * 4 + strlen(host->name) + 1;
    for (unsigned i = 0; i < aliases; i++)
        size += strlen(host->aliases[i]) + 1;
    if (size > RESOLVER_STORAGE_SIZE)
        return 0;
    memset(out, 0, RESOLVER_STORAGE_SIZE);
    lx_guest_store_u32(out + 4, base + 20);
    lx_guest_store_u32(out + 8, 2);
    lx_guest_store_u32(out + 12, 4);
    lx_guest_store_u32(out + 16, base + addresses);
    for (unsigned i = 0; i < count; i++)
    {
        lx_guest_store_u32(out + addresses + i * 4, base + data + i * 4);
        memcpy(out + data + i * 4, host->addresses[i], 4);
    }
    data += count * 4;
    lx_guest_store_u32(out, base + data);
    memcpy(out + data, host->name, strlen(host->name) + 1);
    data += (unsigned)strlen(host->name) + 1;
    for (unsigned i = 0; i < aliases; i++)
    {
        size_t length = strlen(host->aliases[i]) + 1;
        lx_guest_store_u32(out + 20 + i * 4, base + data);
        memcpy(out + data, host->aliases[i], length);
        data += (unsigned)length;
    }
    return base;
}

static uint32 bridge32_Os2LegacyInetText(uint8 *a)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    const char *text = Os2LegacyInetText(lx_guest_load_u32(a));
    if (!text)
        return 0;
    if (!storage)
        storage =
            lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    memcpy(lx_execution_mapping_host(storage), text, strlen(text) + 1);
    return lx_execution_mapping_address(storage);
}
