/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy OS/2 datagram sockets; addresses carry network-order port and address. */
#include "os2/include/private/export.h"
#include "os2/include/stack16/sys/socket.h"
#include "os2/include/sys/socket.h"
#include "os2/include/sys/ioctl.h"
#include "os2/include/netinet/in.h"
#include "host/socket.h"
#include "os2/mptn/common/network.h"
#include "os2/mptn/common/socket_errors.h"
static int socket_result(int result);
#include "os2/mptn/so32dll/generated/so32dll-lx.h"
static LX_HOST_THREAD_LOCAL int socket_error;

uint32_t Os2LegacyGetHostId(void)
{
    return lx_os2_network_host_id();
}

/* The legacy stack takes OOB, PEEK, DONTROUTE and FULLREAD; the first three match the
   host's bits and FULLREAD is the host's WAITALL. */
enum
{
    LEGACY_MSG_FLAGS = OS2_MSG_OOB | OS2_MSG_PEEK | OS2_MSG_DONTROUTE | OS2_MSG_FULLREAD,
    LEGACY_MSG_HOST_FLAGS = OS2_MSG_OOB | OS2_MSG_PEEK | OS2_MSG_DONTROUTE
};

static int failure(int code)
{
    return lx_mptn_socket_error(&socket_error, code);
}

static int socket_result(int result)
{
    return lx_mptn_socket_result(&socket_error, result);
}

static int input_address(const Os2LegacyAddress *address, int length, LxHostSocketAddress *output)
{
    if (!address || length < (int)sizeof *address)
        return failure(SOCEFAULT);
    if (address->family != OS2_AF_INET)
        return failure(SOCEAFNOSUPPORT);
    output->address = address->address;
    output->port = address->port;
    return 0;
}

static int input_flags(int flags)
{
    if (flags & ~LEGACY_MSG_FLAGS)
        return -1;
    return (flags & LEGACY_MSG_HOST_FLAGS) | ((flags & OS2_MSG_FULLREAD) ? LX_MSG_WAITALL : 0);
}

int Os2LegacySocket(int family, int type, int protocol)
{
    TRACE_NATIVE("SO32DLL socket(%d,%d,%d)", family, type, protocol);
    if (family != OS2_AF_INET)
        return failure(SOCEAFNOSUPPORT);
    if (type != LX_SOCKET_STREAM && type != LX_SOCKET_DGRAM && type != LX_SOCKET_RAW)
        return failure(SOCESOCKTNOSUPPORT);
    if (type != LX_SOCKET_RAW && protocol &&
        protocol != (type == LX_SOCKET_DGRAM ? OS2_IPPROTO_UDP : OS2_IPPROTO_TCP))
        return failure(SOCEPROTONOSUPPORT);
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
        return failure(SOCEFAULT);
    LxHostSocketAddress host;
    int result = socket_result(lx_host_socket_accept(socket, &host));
    if (result >= 0 && address)
    {
        memset(address, 0, sizeof *address);
        address->family = OS2_AF_INET;
        address->address = host.address;
        address->port = host.port;
        *length = sizeof *address;
    }
    return result;
}

int Os2LegacyRecv(int socket, void *buffer, int length, int flags)
{
    if (length < 0 || (!buffer && length) || input_flags(flags) < 0)
        return failure(SOCEINVAL);
    return socket_result(
        (int)lx_host_socket_recv(socket, buffer, (size_t)length, input_flags(flags)));
}

int Os2LegacySend(int socket, const void *buffer, int length, int flags)
{
    if (length < 0 || (!buffer && length) || input_flags(flags) < 0)
        return failure(SOCEINVAL);
    return socket_result(
        (int)lx_host_socket_send(socket, buffer, (size_t)length, input_flags(flags)));
}

int Os2LegacyShutdown(int socket, int how)
{
    /* 0 stops receiving, 1 sending, 2 both. */
    if (how < 0 || how > 2)
        return failure(SOCEINVAL);
    return socket_result(lx_host_socket_shutdown(socket, how));
}

int Os2LegacyIoctl(int socket, int command, void *argument, int length)
{
    if (!argument || length < (int)sizeof(int))
        return failure(SOCEFAULT);
    int value;
    memcpy(&value, argument, sizeof value);
    /* Accept the legacy command and its BSD size/direction-decorated form. */
    if ((command & UINT16_MAX) == OS2_FIONBIO)
        return socket_result(lx_host_socket_nonblocking(socket, value != 0));
    if ((command & UINT16_MAX) == OS2_FIONREAD)
    {
        int result = socket_result(lx_host_socket_available(socket, &value));
        if (!result)
            memcpy(argument, &value, sizeof value);
        return result;
    }
    if (length < 0)
        return failure(SOCEINVAL);
    if (((unsigned)command & UINT16_MAX) == OS2_OSIOCGIFCONF)
    {
        /* The native worker has 32-bit pointers. Portable guests translate ifconf. */
        uint32_t capacity, pointer;
        /* ifconf: the buffer length, then the buffer pointer. */
        if (length < (int)(2 * sizeof(uint32_t)))
            return failure(SOCEFAULT);
        memcpy(&capacity, argument, sizeof capacity);
        memcpy(&pointer, (char *)argument + sizeof capacity, sizeof pointer);
        if (capacity > INT32_MAX)
            return failure(SOCEINVAL);
        size_t used;
        int result =
            socket_result(lx_os2_network_ifconf((void *)(uintptr_t)pointer, capacity, &used, 0));
        if (!result)
        {
            capacity = (uint32_t)used;
            memcpy(argument, &capacity, sizeof capacity);
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
        return failure(SOCEINVAL);
    return socket_result((int)lx_host_socket_sendto(socket, buffer, (size_t)length,
                                                    (unsigned)input_flags(flags), &host));
}

int Os2LegacyRecvFrom(int socket, void *buffer, int length, int flags, Os2LegacyAddress *address,
                      int *address_length)
{
    if (length < 0 || (!buffer && length) || input_flags(flags) < 0)
        return failure(SOCEINVAL);
    if (address && (!address_length || *address_length < (int)sizeof *address))
        return failure(SOCEFAULT);
    LxHostSocketAddress host;
    int result = socket_result((int)lx_host_socket_recvfrom(socket, buffer, (size_t)length,
                                                            (unsigned)input_flags(flags), &host));
    if (result >= 0 && address)
    {
        memset(address, 0, sizeof *address);
        address->family = OS2_AF_INET;
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
        return failure(SOCEFAULT);
    LxHostSocketAddress host;
    int result = socket_result(lx_host_socket_name(socket, &host));
    if (!result)
    {
        memset(address, 0, sizeof *address);
        address->family = OS2_AF_INET;
        address->port = host.port;
        address->address = host.address;
        *length = sizeof *address;
    }
    return result;
}

int Os2LegacySocketOption(int socket, int level, int option, const void *value, int length)
{
    if (level == OS2_IPPROTO_IP)
        return socket_result(lx_host_socket_ip_option(socket, option, value, length));
    if (level != OS2_SOL_SOCKET)
        return failure(SOCENOPROTOOPT);
    LxHostSocketOption host;
    switch (option)
    {
        case OS2_SO_DEBUG:
            host = LX_SO_DEBUG;
            break;
        case OS2_SO_REUSEADDR:
            host = LX_SO_REUSEADDR;
            break;
        case OS2_SO_KEEPALIVE:
            host = LX_SO_KEEPALIVE;
            break;
        case OS2_SO_DONTROUTE:
            host = LX_SO_DONTROUTE;
            break;
        case OS2_SO_BROADCAST:
            host = LX_SO_BROADCAST;
            break;
        case OS2_SO_OOBINLINE:
            host = LX_SO_OOBINLINE;
            break;
        case OS2_SO_SNDBUF:
            host = LX_SO_SNDBUF;
            break;
        case OS2_SO_RCVBUF:
            host = LX_SO_RCVBUF;
            break;
        default:
            return failure(SOCENOPROTOOPT);
    }
    if (!value || length < (int)sizeof(int))
        return failure(SOCEFAULT);
    int number;
    memcpy(&number, value, sizeof number);
    return socket_result(lx_host_socket_option(socket, host, &number, sizeof number));
}
