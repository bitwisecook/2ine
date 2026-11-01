/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy OS/2 datagram sockets; addresses carry network-order port and address. */
#include "os2native.h"
#include "so32dll.h"
#include "hal/socket.h"
#include "os2libs/generated/so32dll-lx.h"
static LX_HOST_THREAD_LOCAL int socket_error;
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
    if (type != LX_SOCKET_STREAM && type != LX_SOCKET_DGRAM)
        return failure(44);
    if (protocol && protocol != (type == LX_SOCKET_DGRAM ? 17 : 6))
        return failure(43);
    return socket_result(lx_host_socket_open(type));
}
int Os2LegacyBind(int socket, const Os2LegacyAddress *address, int length)
{
    LxHostSocketAddress host;
    if (input_address(address, length, &host))
        return -1;
    return socket_result(lx_host_socket_bind(socket, &host));
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
    return failure(45);
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
