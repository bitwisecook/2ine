/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy resolver results borrow thread-local libc resolver storage. */
#include "os2native.h"
#include "tcp32dll.h"
#include "hal/socket.h"
#include "os2libs/generated/tcp32dll-lx.h"
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
    if (!name || !lx_host_socket_host(name, &host))
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
