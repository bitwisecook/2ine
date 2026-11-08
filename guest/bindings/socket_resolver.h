/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 socket_guest_error(int value)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    if (!storage)
        storage =
            lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    lx_guest_store_u32(lx_execution_mapping_host(storage), value);
    return lx_execution_mapping_address(storage);
}

static uint32 socket_guest_host(const LxHostSocketHost *host)
{
    enum
    {
        RESOLVER_STORAGE_SIZE = 4096,
        RESOLVER_ADDRESS_LIMIT = 32
    };

    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
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

static uint32 socket_guest_text(const char *text)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    if (!text || strlen(text) >= 4096)
        return 0;
    if (!storage)
        storage =
            lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    memcpy(lx_execution_mapping_host(storage), text, strlen(text) + 1);
    return lx_execution_mapping_address(storage);
}

static uint32 socket_guest_database(const char *name, char **aliases, int number,
                                    const char *protocol)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    if (!name || !aliases)
        return 0;
    unsigned count = 0;
    while (count < 32 && aliases[count])
        count++;
    if (aliases[count])
        return 0;
    size_t size = 16 + (count + 1) * 4 + strlen(name) + 1 + (protocol ? strlen(protocol) + 1 : 0);
    for (unsigned i = 0; i < count; i++)
        size += strlen(aliases[i]) + 1;
    if (size > 4096)
        return 0;
    if (!storage)
        storage =
            lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    uint8 *out = lx_execution_mapping_host(storage);
    uint32 base = lx_execution_mapping_address(storage), data = 16 + (count + 1) * 4;
    memset(out, 0, 4096);
    lx_guest_store_u32(out, base + data);
    lx_guest_store_u32(out + 4, base + 16);
    lx_guest_store_u32(out + 8, number);
    memcpy(out + data, name, strlen(name) + 1);
    data += strlen(name) + 1;
    for (unsigned i = 0; i < count; i++)
    {
        lx_guest_store_u32(out + 16 + i * 4, base + data);
        memcpy(out + data, aliases[i], strlen(aliases[i]) + 1);
        data += strlen(aliases[i]) + 1;
    }
    if (protocol)
    {
        lx_guest_store_u32(out + 12, base + data);
        memcpy(out + data, protocol, strlen(protocol) + 1);
    }
    return base;
}
