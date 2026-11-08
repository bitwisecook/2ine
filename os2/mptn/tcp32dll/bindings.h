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
    char *name = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), 1024);
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
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a), length, LX_GUEST_WRITE,
                            &output))
        result = Os2LegacyGetHostName(output.host, (int)length);
    lx_execution_release(&output);
    return result;
}

#include "host/socket.h"
#include "os2/mptn/common/bindings_resolver.h"

static uint32 legacy_guest_host(Os2LegacyHost *h)
{
    socket_guest_error(*Os2LegacyResolverError());
    if (!h)
        return 0;
    LxHostSocketHost host = {h->name, h->aliases, h->family, h->length, h->addresses};
    return socket_guest_host(&host);
}

static uint32 bridge32_Os2LegacyGetHostByName(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), 1024);
    uint32 result = legacy_guest_host(Os2LegacyGetHostByName(name));
    free(name);
    return result;
}

static uint32 bridge32_Os2LegacyGetHostByAddress(uint8 *a)
{
    uint32 ip;
    if (lx_guest_load_u32(a + 4) != 4 ||
        !lx_execution_read(GProcessState.execution, lx_guest_load_u32(a), &ip, 4))
        return 0;
    return legacy_guest_host(Os2LegacyGetHostByAddress(&ip, 4, lx_guest_load_u32(a + 8)));
}

static uint32 bridge32_Os2LegacyInetText(uint8 *a)
{
    return socket_guest_text(Os2LegacyInetText(lx_guest_load_u32(a)));
}

static uint32 legacy_guest_protocol(Os2LegacyProtocol *p)
{
    return p ? socket_guest_database(p->name, p->aliases, p->number, NULL) : 0;
}

static uint32 bridge32_Os2LegacyGetProtocolByName(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), 1024);
    uint32 result = legacy_guest_protocol(Os2LegacyGetProtocolByName(name));
    free(name);
    return result;
}

static uint32 bridge32_Os2LegacyGetProtocolByNumber(uint8 *a)
{
    return legacy_guest_protocol(Os2LegacyGetProtocolByNumber(lx_guest_load_u32(a)));
}

static uint32 legacy_guest_service(Os2LegacyService *p)
{
    return p ? socket_guest_database(p->name, p->aliases, p->port, p->protocol) : 0;
}

static uint32 bridge32_Os2LegacyGetServiceByName(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), 1024);
    uint32 protocol = lx_guest_load_u32(a + 4);
    char *text = protocol ? lx_execution_copy_string(GProcessState.execution, protocol, 64) : NULL;
    uint32 result = name && (!protocol || text)
                        ? legacy_guest_service(Os2LegacyGetServiceByName(name, text))
                        : 0;
    free(name);
    free(text);
    return result;
}

static uint32 bridge32_Os2LegacyGetServiceByPort(uint8 *a)
{
    uint32 protocol = lx_guest_load_u32(a + 4);
    char *text = protocol ? lx_execution_copy_string(GProcessState.execution, protocol, 64) : NULL;
    uint32 result =
        !protocol || text
            ? legacy_guest_service(Os2LegacyGetServiceByPort(lx_guest_load_u32(a), text))
            : 0;
    free(text);
    return result;
}

static uint32 bridge32_Os2LegacyResolverError(uint8 *a)
{
    (void)a;
    return socket_guest_error(*Os2LegacyResolverError());
}
