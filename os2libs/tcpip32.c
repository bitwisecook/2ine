/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native.h"
#include "tcpip32.h"

#include "hal/socket.h"
#include "hal/host_thread.h"

#include "os2libs/generated/tcpip32-lx.h"

int OS2_sock_init(void)
{
    TRACE_NATIVE("sock_init()");
    return 1; // always succeeds.
}

static void mapOS2SockAddrIn(const OS2_sockaddr *source, LxHostSocketAddress *target)
{
    const OS2_sockaddr_in *address = (const OS2_sockaddr_in *)source;
    target->address = address->sin_addr.s_addr;
    target->port = address->sin_port;
}

static void mapBSDSockAddrIn(const LxHostSocketAddress *source, OS2_sockaddr *target)
{
    OS2_sockaddr_in *address = (OS2_sockaddr_in *)target;
    memset(address, 0, sizeof *address);
    address->sin_family = OS2_AF_INET;
    address->sin_port = source->port;
    address->sin_addr.s_addr = source->address;
}

static int mapOS2MsgFlags(const int os2flags)
{
    int bsdflags = 0;

#define MAPSOCKFLAGS(f)                                                                            \
    if (os2flags & OS2_##f)                                                                        \
    {                                                                                              \
        bsdflags |= LX_##f;                                                                        \
    }
    MAPSOCKFLAGS(MSG_OOB);
    MAPSOCKFLAGS(MSG_PEEK);
    MAPSOCKFLAGS(MSG_DONTROUTE);
    MAPSOCKFLAGS(MSG_EOR);
    MAPSOCKFLAGS(MSG_TRUNC);
    MAPSOCKFLAGS(MSG_CTRUNC);
    MAPSOCKFLAGS(MSG_WAITALL);
    MAPSOCKFLAGS(MSG_DONTWAIT);
#undef MAPSOCKFLAGS

    return bsdflags;
}

int OS2_recv(int sock, void *buf, size_t len, int os2flags)
{
    TRACE_NATIVE("recv(%d, %p, %d, %d)", sock, buf, (int)len, os2flags);
    FIXME("this should fail if not a socket");
    return (int)lx_host_socket_recv(sock, buf, len, mapOS2MsgFlags(os2flags));
}

int OS2_connect(int sock, const struct OS2_sockaddr *os2addr, int addrlen)
{
    TRACE_NATIVE("connect(%d, %p, %d)", sock, os2addr, addrlen);

    FIXME("this should fail if not a socket");

    if (os2addr->sa_family != OS2_AF_INET)
    {
        FIXME("currently demands IPv4 socket");
        return -1;
    }

    LxHostSocketAddress bsdaddr;
    mapOS2SockAddrIn(os2addr, &bsdaddr);
    return lx_host_socket_connect(sock, &bsdaddr);
}

int OS2_shutdown(int sock, int kind)
{
    TRACE_NATIVE("shutdown(%d, %d)", sock, kind);
    FIXME("this should fail if not a socket");
    return lx_host_socket_shutdown(sock, kind);
}

int OS2_socket(int os2family, int os2socktype, int protocol)
{
    TRACE_NATIVE("socket(%d, %d, %d)", os2family, os2socktype, protocol);
    if (os2family != OS2_AF_INET)
    {
        FIXME("currently demands IPv4 socket");
        return -1;
    }

    if (protocol != 0)
    {
        FIXME("Only protocol 0 supported at the moment");
        return -1;
    }

    int bsdsocktype;
    if (os2socktype == OS2_SOCK_STREAM)
    {
        bsdsocktype = LX_SOCKET_STREAM;
    }
    else if (os2socktype == OS2_SOCK_DGRAM)
    {
        bsdsocktype = LX_SOCKET_DGRAM;
    }
    else
    {
        FIXME("Only SOCK_STREAM and SOCK_DGRAM supported at the moment");
        return -1;
    }

    return lx_host_socket_open(bsdsocktype);
}

ssize_t OS2_send(int sock, const void *buf, size_t len, int os2flags)
{
    TRACE_NATIVE("send(%d, %p, %d, %d)", sock, buf, (int)len, os2flags);
    FIXME("this should fail if not a socket");
    return (ssize_t)lx_host_socket_send(sock, buf, len, mapOS2MsgFlags(os2flags));
}

int OS2_soclose(int sock)
{
    TRACE_NATIVE("soclose(%d)", sock);
    FIXME("this should fail if not a socket");
    return lx_host_socket_close(sock);
}

int OS2_sock_errno(void)
{
    TRACE_NATIVE("sock_errno()");
    FIXME("this should cache errno when an OS/2 socket function fails");
    return lx_host_socket_error();
}

unsigned long OS2_inet_addr(const char *name)
{
    TRACE_NATIVE("inet_addr('%s')", name);
    return lx_host_socket_parse_address(name);
}

struct OS2_hostent *OS2_gethostbyname(const char *name)
{
    static LX_HOST_THREAD_LOCAL OS2_hostent output;
    LxHostSocketHost host;
    if (!lx_host_socket_host(name, &host))
        return NULL;
    output.h_name = host.name;
    output.h_aliases = host.aliases;
    output.h_addrtype = host.family;
    output.h_length = host.length;
    output.h_addr_list = host.addresses;
    return &output;
}

unsigned short OS2_htons(unsigned short val)
{
    TRACE_NATIVE("htons(%u)", (unsigned int)val);
    return lx_host_socket_network16(val);
}

char *OS2_inet_ntoa(struct OS2_in_addr address)
{
    return lx_host_socket_format_address(address.s_addr);
}

struct OS2_servent *OS2_getservbyname(const char *name, const char *protocol)
{
    static LX_HOST_THREAD_LOCAL OS2_servent output;
    LxHostSocketService service;
    if (!lx_host_socket_service(name, protocol, &service))
        return NULL;
    output.s_name = service.name;
    output.s_aliases = service.aliases;
    output.s_port = service.port;
    output.s_proto = service.protocol;
    return &output;
}

static LxHostSocketSet mapOS2FdSet(const OS2_fd_set *source)
{
    LxHostSocketSet result = {source->fd_count, source->fd_array};
    return result;
}

int OS2_select(int limit, OS2_fd_set *read, OS2_fd_set *write, OS2_fd_set *error,
               OS2_timeval *timeout)
{
    LxHostSocketSet r, w, e;
    if (read)
        r = mapOS2FdSet(read);
    if (write)
        w = mapOS2FdSet(write);
    if (error)
        e = mapOS2FdSet(error);
    /* The API returns the readiness count and retains the supplied descriptor lists. */
    return lx_host_socket_select(limit, read ? &r : NULL, write ? &w : NULL, error ? &e : NULL,
                                 timeout ? timeout->tv_sec : 0, timeout ? timeout->tv_usec : 0,
                                 timeout != NULL);
}

int OS2_os2_select(int *sockets, int reads, int writes, int errors, long timeout)
{
    return lx_host_socket_poll(sockets, reads, writes, errors, (int)(timeout * 1000));
}

int OS2_getsockname(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    TRACE_NATIVE("getsockname(%d, %p, %p)", sock, os2name, os2namelen);

    LxHostSocketAddress bsdaddr;
    const int rc = lx_host_socket_name(sock, &bsdaddr);
    if (rc == -1)
    {
        return -1;
    }
    else if (*os2namelen < sizeof(OS2_sockaddr_in))
    {
        return -1;
    }

    mapBSDSockAddrIn(&bsdaddr, os2name);
    *os2namelen = sizeof(OS2_sockaddr_in);
    return rc;
}

int OS2_bind(int sock, const OS2_sockaddr *os2name, int os2namelen)
{
    TRACE_NATIVE("bind(%d, %p, %d)", sock, os2name, os2namelen);
    LxHostSocketAddress bsdaddr;
    mapOS2SockAddrIn(os2name, &bsdaddr);
    return lx_host_socket_bind(sock, &bsdaddr);
}

int OS2_listen(int sock, int backlog)
{
    TRACE_NATIVE("listen(%d, %d)", sock, backlog);
    return lx_host_socket_listen(sock, backlog);
}

int OS2_accept(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    TRACE_NATIVE("bind(%d, %p, %p)", sock, os2name, os2namelen);
    LxHostSocketAddress bsdaddr;
    const int retval = lx_host_socket_accept(sock, &bsdaddr);

    if (os2name != NULL)
    {
        mapBSDSockAddrIn(&bsdaddr, os2name);
    }

    if (os2namelen != NULL)
    {
        *os2namelen = sizeof(OS2_sockaddr_in);
    }

    return retval;
}

static int mapOS2SocketOption(const int os2name)
{
    switch (os2name)
    {
#define MAPSOOPT(f)                                                                                \
    case OS2_##f:                                                                                  \
        return LX_##f
        MAPSOOPT(SO_DEBUG);
        MAPSOOPT(SO_ACCEPTCONN);
        MAPSOOPT(SO_REUSEADDR);
        MAPSOOPT(SO_KEEPALIVE);
        MAPSOOPT(SO_DONTROUTE);
        MAPSOOPT(SO_BROADCAST);

        MAPSOOPT(SO_LINGER);
        MAPSOOPT(SO_OOBINLINE);

        MAPSOOPT(SO_REUSEPORT);

        MAPSOOPT(SO_SNDBUF);
        MAPSOOPT(SO_RCVBUF);
        MAPSOOPT(SO_SNDLOWAT);
        MAPSOOPT(SO_RCVLOWAT);
        MAPSOOPT(SO_SNDTIMEO);
        MAPSOOPT(SO_RCVTIMEO);
        MAPSOOPT(SO_ERROR);
        MAPSOOPT(SO_TYPE);

#undef MAPSOOPT
        default:
            break;
    }

    FIXME("unsupported OS/2 socket option");
    return 0;
}

int OS2_setsockopt(int sock, int os2level, int os2name, const void *value, int len)
{
    const int bsdname = mapOS2SocketOption(os2name);
    if (!bsdname)
    {
        return -1;
    }
    else if (os2level != OS2_SOL_SOCKET)
    {
        return -1;
    }
    return lx_host_socket_option(sock, (LxHostSocketOption)bsdname, value, len);
}

int OS2_gettimeofday(OS2_timeval *time, OS2_timezone *zone)
{
    int64_t seconds;
    int32_t micros, west, dst;
    int result = lx_host_wall_time(&seconds, &micros, &west, &dst);
    if (time)
    {
        time->tv_sec = (int32_t)seconds;
        time->tv_usec = micros;
    }
    if (zone)
    {
        zone->tz_minuteswest = west;
        zone->tz_dsttime = dst;
    }
    return result;
}

// SOCKS support...

int OS2_Rgetsockname(int sock, OS2_sockaddr *os2name, int *os2namelen)
{

    TRACE_NATIVE("Rgetsockname(%d, %p, %p)", sock, os2name, os2namelen);
    return OS2_getsockname(sock, os2name, os2namelen);
}

int OS2_Rbind(int sock, OS2_sockaddr *os2name, int os2namelen, OS2_sockaddr *os2remote)
{

    TRACE_NATIVE("Rbind(%d, %p, %d, %p)", sock, os2name, os2namelen, os2remote);
    return OS2_bind(sock, os2name, os2namelen);
}

int OS2_Raccept(int sock, OS2_sockaddr *os2name, int *os2namelen)
{

    TRACE_NATIVE("Raccept(%d, %p, %p)", sock, os2name, os2namelen);
    return OS2_accept(sock, os2name, os2namelen);
}

int OS2_Rconnect(int sock, const OS2_sockaddr *os2name, int os2namelen)
{

    TRACE_NATIVE("Rconnect(%d, %p, %d)", sock, os2name, os2namelen);
    return OS2_connect(sock, os2name, os2namelen);
}

int OS2_Rlisten(int sock, int backlog)
{

    TRACE_NATIVE("Rlisten(%d, %d)", sock, backlog);
    return OS2_listen(sock, backlog);
}

OS2_hostent *OS2_Rgethostbyname(const char *name)
{

    TRACE_NATIVE("Rgethostbyname('%s')", name);
    return OS2_gethostbyname(name);
}
