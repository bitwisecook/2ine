/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy OS/2 datagram sockets; addresses carry network-order port and address. */
#include "os2native.h"
#include "so32dll.h"
#include "hal/socket.h"
#include "hal/network.h"
#include "network.h"
static int socket_result(int result);
#include "os2libs/generated/so32dll-lx.h"
static LX_HOST_THREAD_LOCAL int socket_error;
uint32_t Os2LegacyGetHostId(void)
{
    LxHostNetwork network;
    if (lx_host_network_snapshot(&network))
        return 0;
    uint32_t address = 0;
    for (size_t i = 0; i < network.interface_count; i++)
        if ((network.interfaces[i].flags & LX_IFF_UP) &&
            !(network.interfaces[i].flags & LX_IFF_LOOPBACK))
        {
            address = network.interfaces[i].address;
            break;
        }
    lx_host_network_destroy(&network);
    return address;
}
enum
{
    OS2_AF_INET_LEGACY = 2,
    OS2_SOCKET_ERROR_BASE = 10000,
    OS2_FIONBIO_LEGACY = 0x667e,
    OS2_FIONREAD_LEGACY = 0x667f
};
static int failure(int number)
{
    socket_error = OS2_SOCKET_ERROR_BASE + number;
    return -1;
}
static int socket_result(int result)
{
    return result < 0 ? failure(lx_host_socket_bsd_error()) : result;
}
static int input_address(const Os2LegacyAddress *address, int length, LxHostSocketAddress *output)
{
    if (!address || length < (int)sizeof *address)
        return failure(14);
    if (address->family != OS2_AF_INET_LEGACY)
        return failure(47);
    output->address = address->address;
    output->port = address->port;
    return 0;
}
static int input_flags(int flags)
{
    /* The legacy stack exposes OOB, PEEK, DONTROUTE and FULLREAD. */
    if (flags & ~15)
        return -1;
    return (flags & 7) | ((flags & 8) ? LX_MSG_WAITALL : 0);
}
int Os2LegacySocket(int family, int type, int protocol)
{
    TRACE_NATIVE("SO32DLL socket(%d,%d,%d)", family, type, protocol);
    if (family != OS2_AF_INET_LEGACY)
        return failure(47);
    if (type != LX_SOCKET_STREAM && type != LX_SOCKET_DGRAM && type != LX_SOCKET_RAW)
        return failure(44);
    if (type != LX_SOCKET_RAW && protocol && protocol != (type == LX_SOCKET_DGRAM ? 17 : 6))
        return failure(43);
    return socket_result(lx_host_socket_open_protocol(type, protocol));
}
int Os2LegacyBind(int socket, const Os2LegacyAddress *address, int length)
{
    LxHostSocketAddress host;
    if (input_address(address, length, &host))
        return -1;
    return socket_result(lx_host_socket_bind(socket, &host));
}
int Os2LegacyConnect(int socket, const Os2LegacyAddress *address, int length)
{
    LxHostSocketAddress host;
    if (input_address(address, length, &host))
        return -1;
    return socket_result(lx_host_socket_connect(socket, &host));
}
int Os2LegacyListen(int socket, int backlog)
{
    return socket_result(lx_host_socket_listen(socket, backlog));
}
int Os2LegacyAccept(int socket, Os2LegacyAddress *address, int *length)
{
    if (address && (!length || *length < (int)sizeof *address))
        return failure(14);
    LxHostSocketAddress host;
    int result = socket_result(lx_host_socket_accept(socket, &host));
    if (result >= 0 && address)
    {
        memset(address, 0, sizeof *address);
        address->family = 2;
        address->address = host.address;
        address->port = host.port;
        *length = sizeof *address;
    }
    return result;
}
int Os2LegacyRecv(int socket, void *buffer, int length, int flags)
{
    if (length < 0 || (!buffer && length) || input_flags(flags) < 0)
        return failure(22);
    return socket_result(
        (int)lx_host_socket_recv(socket, buffer, (size_t)length, input_flags(flags)));
}
int Os2LegacySend(int socket, const void *buffer, int length, int flags)
{
    if (length < 0 || (!buffer && length) || input_flags(flags) < 0)
        return failure(22);
    return socket_result(
        (int)lx_host_socket_send(socket, buffer, (size_t)length, input_flags(flags)));
}
int Os2LegacyShutdown(int socket, int how)
{
    if (how < 0 || how > 2)
        return failure(22);
    return socket_result(lx_host_socket_shutdown(socket, how));
}
int Os2LegacyIoctl(int socket, int command, void *argument, int length)
{
    if (!argument || length < 4)
        return failure(14);
    int value;
    memcpy(&value, argument, 4);
    /* Accept the legacy command and its BSD size/direction-decorated form. */
    if ((command & 0xffff) == OS2_FIONBIO_LEGACY)
        return socket_result(lx_host_socket_nonblocking(socket, value != 0));
    if ((command & 0xffff) == OS2_FIONREAD_LEGACY)
    {
        int result = socket_result(lx_host_socket_available(socket, &value));
        if (!result)
            memcpy(argument, &value, 4);
        return result;
    }
    if (length < 0)
        return failure(22);
    if (((unsigned)command & 0xffff) == 0x6914)
    {
        /* The native worker has 32-bit pointers. Portable guests translate ifconf. */
        uint32_t capacity, pointer;
        if (length < 8)
            return failure(14);
        memcpy(&capacity, argument, 4);
        memcpy(&pointer, (char *)argument + 4, 4);
        if (capacity > INT32_MAX)
            return failure(22);
        size_t used;
        int result =
            socket_result(lx_os2_network_ifconf((void *)(uintptr_t)pointer, capacity, &used, 0));
        if (!result)
        {
            capacity = (uint32_t)used;
            memcpy(argument, &capacity, 4);
        }
        return result;
    }
    return socket_result(lx_os2_network_ioctl((unsigned)command, argument, (size_t)length, 0));
}
int Os2LegacySendTo(int socket, const void *buffer, int length, int flags,
                    const Os2LegacyAddress *address, int address_length)
{
    LxHostSocketAddress host;
    if (input_address(address, address_length, &host))
        return -1;
    if (length < 0 || (!buffer && length) || input_flags(flags) < 0)
        return failure(22);
    return socket_result((int)lx_host_socket_sendto(socket, buffer, (size_t)length,
                                                    (unsigned)input_flags(flags), &host));
}
int Os2LegacyRecvFrom(int socket, void *buffer, int length, int flags, Os2LegacyAddress *address,
                      int *address_length)
{
    if (length < 0 || (!buffer && length) || input_flags(flags) < 0)
        return failure(22);
    if (address && (!address_length || *address_length < (int)sizeof *address))
        return failure(14);
    LxHostSocketAddress host;
    int result = socket_result((int)lx_host_socket_recvfrom(socket, buffer, (size_t)length,
                                                            (unsigned)input_flags(flags), &host));
    if (result >= 0 && address)
    {
        memset(address, 0, sizeof *address);
        address->family = OS2_AF_INET_LEGACY;
        address->port = host.port;
        address->address = host.address;
        *address_length = sizeof *address;
    }
    return result;
}
int Os2LegacySocketClose(int socket)
{
    return socket_result(lx_host_socket_close(socket));
}
int Os2LegacySocketError(void)
{
    return socket_error;
}
int Os2LegacySocketInit(void)
{
    return 0;
}

int Os2LegacyGetSocketName(int socket, Os2LegacyAddress *address, int *length)
{
    if (!address || !length || *length < (int)sizeof *address)
        return failure(14);
    LxHostSocketAddress host;
    int result = socket_result(lx_host_socket_name(socket, &host));
    if (!result)
    {
        memset(address, 0, sizeof *address);
        address->family = OS2_AF_INET_LEGACY;
        address->port = host.port;
        address->address = host.address;
        *length = sizeof *address;
    }
    return result;
}
int Os2LegacySocketOption(int socket, int level, int option, const void *value, int length)
{
    if (level == 0)
        return socket_result(lx_host_socket_ip_option(socket, option, value, length));
    if (level != 0xffff)
        return failure(42);
    LxHostSocketOption host;
    switch (option)
    {
        case 0x0001:
            host = LX_SO_DEBUG;
            break;
        case 0x0004:
            host = LX_SO_REUSEADDR;
            break;
        case 0x0008:
            host = LX_SO_KEEPALIVE;
            break;
        case 0x0010:
            host = LX_SO_DONTROUTE;
            break;
        case 0x0020:
            host = LX_SO_BROADCAST;
            break;
        case 0x0100:
            host = LX_SO_OOBINLINE;
            break;
        case 0x1001:
            host = LX_SO_SNDBUF;
            break;
        case 0x1002:
            host = LX_SO_RCVBUF;
            break;
        default:
            return failure(42);
    }
    if (!value || length < 4)
        return failure(14);
    int number;
    memcpy(&number, value, sizeof number);
    return socket_result(lx_host_socket_option(socket, host, &number, sizeof number));
}
