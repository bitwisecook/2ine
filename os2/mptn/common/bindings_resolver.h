/* Public domain. No rights reserved. Wholly AI-authored. */
/* Per-thread guest copies of resolver results: the error word, hostent, servent or
   protoent records and inet_ntoa text, as the OS/2 stacks return them. */
#include "os2/include/sys/socket.h"

enum
{
    /* One guest page of per-thread storage for each kind of result. */
    RESOLVER_STORAGE_SIZE = 4096,
    RESOLVER_ADDRESS_LIMIT = 32,
    GUEST_POINTER_SIZE = 4,
    IPV4_ADDRESS_SIZE = 4
};

/* hostent with 32-bit guest pointers. */
enum
{
    HOSTENT_NAME = 0,
    HOSTENT_ALIASES = 4,
    HOSTENT_ADDRTYPE = 8,
    HOSTENT_LENGTH = 12,
    HOSTENT_ADDR_LIST = 16,
    HOSTENT_SIZE = 20
};

/* servent and protoent with 32-bit guest pointers; protoent stops before the
   protocol name. */
enum
{
    DATABASE_NAME = 0,
    DATABASE_ALIASES = 4,
    DATABASE_NUMBER = 8,
    DATABASE_PROTOCOL = 12,
    DATABASE_SIZE = 16
};

static uint32 socket_guest_error(int value)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    if (!storage)
        storage = lx_execution_map(GProcessState.execution, 0, RESOLVER_STORAGE_SIZE,
                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    lx_guest_store_u32(lx_execution_mapping_host(storage), value);
    return lx_execution_mapping_address(storage);
}

static uint32 socket_guest_host(const LxHostSocketHost *host)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    if (!host || host->length != IPV4_ADDRESS_SIZE || host->family != OS2_AF_INET)
        return 0;
    if (!storage)
        storage = lx_execution_map(GProcessState.execution, 0, RESOLVER_STORAGE_SIZE,
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
    /* The record, its NULL-terminated alias and address lists, the addresses, then the
       strings. */
    unsigned addresses = HOSTENT_SIZE + (aliases + 1) * GUEST_POINTER_SIZE;
    unsigned data = addresses + (count + 1) * GUEST_POINTER_SIZE;
    size_t size = data + count * IPV4_ADDRESS_SIZE + strlen(host->name) + 1;
    for (unsigned i = 0; i < aliases; i++)
        size += strlen(host->aliases[i]) + 1;
    if (size > RESOLVER_STORAGE_SIZE)
        return 0;
    memset(out, 0, RESOLVER_STORAGE_SIZE);
    lx_guest_store_u32(out + HOSTENT_ALIASES, base + HOSTENT_SIZE);
    lx_guest_store_u32(out + HOSTENT_ADDRTYPE, OS2_AF_INET);
    lx_guest_store_u32(out + HOSTENT_LENGTH, IPV4_ADDRESS_SIZE);
    lx_guest_store_u32(out + HOSTENT_ADDR_LIST, base + addresses);
    for (unsigned i = 0; i < count; i++)
    {
        lx_guest_store_u32(out + addresses + i * GUEST_POINTER_SIZE,
                           base + data + i * IPV4_ADDRESS_SIZE);
        memcpy(out + data + i * IPV4_ADDRESS_SIZE, host->addresses[i], IPV4_ADDRESS_SIZE);
    }
    data += count * IPV4_ADDRESS_SIZE;
    lx_guest_store_u32(out + HOSTENT_NAME, base + data);
    memcpy(out + data, host->name, strlen(host->name) + 1);
    data += (unsigned)strlen(host->name) + 1;
    for (unsigned i = 0; i < aliases; i++)
    {
        size_t length = strlen(host->aliases[i]) + 1;
        lx_guest_store_u32(out + HOSTENT_SIZE + i * GUEST_POINTER_SIZE, base + data);
        memcpy(out + data, host->aliases[i], length);
        data += (unsigned)length;
    }
    return base;
}

static uint32 socket_guest_text(const char *text)
{
    static LX_HOST_THREAD_LOCAL LxGuestMapping *storage;
    if (!text || strlen(text) >= RESOLVER_STORAGE_SIZE)
        return 0;
    if (!storage)
        storage = lx_execution_map(GProcessState.execution, 0, RESOLVER_STORAGE_SIZE,
                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
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
    while (count < RESOLVER_ADDRESS_LIMIT && aliases[count])
        count++;
    if (aliases[count])
        return 0;
    size_t size = DATABASE_SIZE + (count + 1) * GUEST_POINTER_SIZE + strlen(name) + 1 +
                  (protocol ? strlen(protocol) + 1 : 0);
    for (unsigned i = 0; i < count; i++)
        size += strlen(aliases[i]) + 1;
    if (size > RESOLVER_STORAGE_SIZE)
        return 0;
    if (!storage)
        storage = lx_execution_map(GProcessState.execution, 0, RESOLVER_STORAGE_SIZE,
                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!storage)
        return 0;
    uint8 *out = lx_execution_mapping_host(storage);
    uint32 base = lx_execution_mapping_address(storage),
           data = DATABASE_SIZE + (count + 1) * GUEST_POINTER_SIZE;
    memset(out, 0, RESOLVER_STORAGE_SIZE);
    lx_guest_store_u32(out + DATABASE_NAME, base + data);
    lx_guest_store_u32(out + DATABASE_ALIASES, base + DATABASE_SIZE);
    lx_guest_store_u32(out + DATABASE_NUMBER, number);
    memcpy(out + data, name, strlen(name) + 1);
    data += strlen(name) + 1;
    for (unsigned i = 0; i < count; i++)
    {
        lx_guest_store_u32(out + DATABASE_SIZE + i * GUEST_POINTER_SIZE, base + data);
        memcpy(out + data, aliases[i], strlen(aliases[i]) + 1);
        data += strlen(aliases[i]) + 1;
    }
    if (protocol)
    {
        lx_guest_store_u32(out + DATABASE_PROTOCOL, base + data);
        memcpy(out + data, protocol, strlen(protocol) + 1);
    }
    return base;
}
