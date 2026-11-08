/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy resolver results borrow per-thread host resolver snapshots. */
#include "os2/include/private/export.h"
#include "os2/include/stack16/netdb.h"
#include "host/socket.h"
#include "os2/mptn/tcp32dll/generated/tcp32dll-lx.h"
static LX_HOST_THREAD_LOCAL int ResolverError;

uint32_t Os2LegacyLongSwap(uint32_t value)
{
    return ((value & UINT32_C(0xff)) << 24) | ((value & UINT32_C(0xff00)) << 8) |
           ((value >> 8) & UINT32_C(0xff00)) | (value >> 24);
}

uint16_t Os2LegacyShortSwap(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

uint32_t Os2LegacyInetAddr(const char *name)
{
    return name ? lx_host_socket_parse_address(name) : UINT32_MAX;
}

Os2LegacyHost *Os2LegacyGetHostByName(const char *name)
{
    LxHostSocketHost host;
    static LX_HOST_THREAD_LOCAL Os2LegacyHost output;
    int ok = lx_host_socket_host(name, &host);
    ResolverError = lx_host_socket_resolver_error();
    if (!ok)
        return NULL;
    output.name = host.name;
    output.aliases = host.aliases;
    output.family = host.family;
    output.length = host.length;
    output.addresses = host.addresses;
    return &output;
}

int Os2LegacyGetHostName(char *name, int length)
{
    return name && length > 0 ? lx_host_socket_hostname(name, (size_t)length) : -1;
}

char *Os2LegacyInetText(uint32_t address)
{
    static LX_HOST_THREAD_LOCAL char text[16];
    const char *formatted = lx_host_socket_format_address(address);
    if (!formatted)
        return NULL;
    snprintf(text, sizeof text, "%s", formatted);
    return text;
}

Os2LegacyHost *Os2LegacyGetHostByAddress(const void *address, int length, int family)
{
    static LX_HOST_THREAD_LOCAL Os2LegacyHost output;
    LxHostSocketHost host;
    if (!address || length != 4 || family != 2)
    {
        ResolverError = 3;
        return NULL;
    }
    uint32_t ip;
    memcpy(&ip, address, 4);
    int ok = lx_host_socket_reverse(ip, &host);
    ResolverError = lx_host_socket_resolver_error();
    if (!ok)
        return NULL;
    output = (Os2LegacyHost){host.name, host.aliases, host.family, host.length, host.addresses};
    return &output;
}

static Os2LegacyProtocol *legacy_protocol(const char *name, int number)
{
    static LX_HOST_THREAD_LOCAL Os2LegacyProtocol output;
    return lx_host_socket_protocol(name, number, &output.name, &output.aliases, &output.number)
               ? &output
               : NULL;
}

Os2LegacyProtocol *Os2LegacyGetProtocolByName(const char *name)
{
    return name ? legacy_protocol(name, 0) : NULL;
}

Os2LegacyProtocol *Os2LegacyGetProtocolByNumber(int number)
{
    return legacy_protocol(NULL, number);
}

static Os2LegacyService *legacy_service(LxHostSocketService *host)
{
    static LX_HOST_THREAD_LOCAL Os2LegacyService output;
    output = (Os2LegacyService){host->name, host->aliases, host->port, host->protocol};
    return &output;
}

Os2LegacyService *Os2LegacyGetServiceByPort(int port, const char *protocol)
{
    LxHostSocketService host;
    return lx_host_socket_service_port(port, protocol, &host) ? legacy_service(&host) : NULL;
}

Os2LegacyService *Os2LegacyGetServiceByName(const char *name, const char *protocol)
{
    LxHostSocketService host;
    return lx_host_socket_service(name, protocol, &host) ? legacy_service(&host) : NULL;
}

int *Os2LegacyResolverError(void)
{
    return &ResolverError;
}
