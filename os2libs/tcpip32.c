/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2libs/os2native.h"
#include "os2libs/tcpip32.h"

#include "host/socket.h"
#include "host/thread.h"
#include "os2libs/network.h"

#include "host/network.h"
#include "host/sync.h"
#include <ctype.h>
static LX_HOST_THREAD_LOCAL int SocketError, ResolverError;

static int socket_failure(int number)
{
    SocketError = 10000 + number;
    return -1;
}

static int socket_result(int result)
{
    return result < 0 ? socket_failure(lx_host_socket_bsd_error()) : result;
}

static int initializeResolverData(void);
#define lxNativeModuleInit lxTcpipGeneratedInit
#define lxNativeModuleDeinit lxTcpipGeneratedDeinit
#include "os2libs/generated/tcpip32-lx.h"
#undef lxNativeModuleInit
#undef lxNativeModuleDeinit

int OS2_sysctl(const int *name, unsigned count, void *output, size_t *length, const void *input,
               size_t input_length)
{
    if (input || input_length)
    {
        errno = EPERM;
        return socket_result(-1);
    }
    return socket_result(lx_os2_network_sysctl(name, count, output, length));
}

int OS2_os2_ioctl(int sock, uint32_t command, void *argument, int length)
{
    if (!argument || length < 0)
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    unsigned cmd = command & 0xffff;
    if ((cmd == 0x667e || cmd == 0x667f) && length >= 4)
    {
        int value;
        memcpy(&value, argument, 4);
        int result = cmd == 0x667e ? lx_host_socket_nonblocking(sock, value != 0)
                                   : lx_host_socket_available(sock, &value);
        if (!result && cmd == 0x667f)
            memcpy(argument, &value, 4);
        return socket_result(result);
    }
    if (cmd == 0x6914 || cmd == 0x6924)
    {
        uint32_t capacity, pointer;
        if (length < 8)
        {
            errno = EFAULT;
            return socket_result(-1);
        }
        memcpy(&capacity, argument, 4);
        memcpy(&pointer, (char *)argument + 4, 4);
        if (capacity > INT32_MAX)
        {
            errno = EINVAL;
            return socket_result(-1);
        }
        size_t used;
        int result = lx_os2_network_ifconf((void *)(uintptr_t)pointer, capacity, &used, 1);
        if (!result)
        {
            capacity = (uint32_t)used;
            memcpy(argument, &capacity, 4);
        }
        return socket_result(result);
    }
    return socket_result(lx_os2_network_ioctl(command, argument, (size_t)length, 1));
}

int OS2_ioctl(int sock, uint32_t command, void *argument)
{
    /* The three-argument BSD API carries the extent in the command itself. */
    return OS2_os2_ioctl(sock, command, argument, (int)((command >> 16) & 0x1fff));
}

int OS2_sock_init(void)
{
    TRACE_NATIVE("sock_init()");
    return 0;
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
    address->sin_len = sizeof *address;
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
    return socket_result((int)lx_host_socket_recv(sock, buf, len, mapOS2MsgFlags(os2flags)));
}

int OS2_connect(int sock, const struct OS2_sockaddr *os2addr, int addrlen)
{
    TRACE_NATIVE("connect(%d, %p, %d)", sock, os2addr, addrlen);

    if (!os2addr || addrlen < 16)
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    if (os2addr->sa_family != OS2_AF_INET)
    {
        errno = EAFNOSUPPORT;
        return socket_result(-1);
    }

    LxHostSocketAddress bsdaddr;
    mapOS2SockAddrIn(os2addr, &bsdaddr);
    return socket_result(lx_host_socket_connect(sock, &bsdaddr));
}

int OS2_shutdown(int sock, int kind)
{
    TRACE_NATIVE("shutdown(%d, %d)", sock, kind);
    return socket_result(lx_host_socket_shutdown(sock, kind));
}

int OS2_socket(int os2family, int os2socktype, int protocol)
{
    TRACE_NATIVE("socket(%d, %d, %d)", os2family, os2socktype, protocol);
    if (os2family != OS2_AF_INET)
    {
        errno = EAFNOSUPPORT;
        return socket_result(-1);
    }

    if (os2socktype != OS2_SOCK_STREAM && os2socktype != OS2_SOCK_DGRAM &&
        os2socktype != OS2_SOCK_RAW)
    {
        errno = ESOCKTNOSUPPORT;
        return socket_result(-1);
    }
    return socket_result(lx_host_socket_open_protocol(os2socktype, protocol));
}

ssize_t OS2_send(int sock, const void *buf, size_t len, int os2flags)
{
    TRACE_NATIVE("send(%d, %p, %d, %d)", sock, buf, (int)len, os2flags);
    return socket_result((ssize_t)lx_host_socket_send(sock, buf, len, mapOS2MsgFlags(os2flags)));
}

int OS2_soclose(int sock)
{
    TRACE_NATIVE("soclose(%d)", sock);
    return socket_result(lx_host_socket_close(sock));
}

int OS2_sock_errno(void)
{
    TRACE_NATIVE("sock_errno()");
    return SocketError;
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
    int ok = lx_host_socket_host(name, &host);
    ResolverError = lx_host_socket_resolver_error();
    if (!ok)
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

static LxHostSocketSet mapOS2FdSet(OS2_fd_set *source)
{
    LxHostSocketSet result = {source->fd_count, source->fd_array};
    return result;
}

int OS2_select(int limit, OS2_fd_set *read, OS2_fd_set *write, OS2_fd_set *error,
               OS2_timeval *timeout)
{
    if ((read && read->fd_count > OS2_FD_SETSIZE) || (write && write->fd_count > OS2_FD_SETSIZE) ||
        (error && error->fd_count > OS2_FD_SETSIZE))
    {
        errno = EINVAL;
        return socket_result(-1);
    }
    LxHostSocketSet r, w, e;
    if (read)
        r = mapOS2FdSet(read);
    if (write)
        w = mapOS2FdSet(write);
    if (error)
        e = mapOS2FdSet(error);
    int result = lx_host_socket_select(limit, read ? &r : NULL, write ? &w : NULL,
                                       error ? &e : NULL, timeout ? timeout->tv_sec : 0,
                                       timeout ? timeout->tv_usec : 0, timeout != NULL);
    if (result >= 0)
    {
        if (read)
            read->fd_count = (uint16_t)r.count;
        if (write)
            write->fd_count = (uint16_t)w.count;
        if (error)
            error->fd_count = (uint16_t)e.count;
    }
    return socket_result(result);
}

int OS2_os2_select(int *sockets, int reads, int writes, int errors, long timeout)
{
    return socket_result(lx_host_socket_poll_update(sockets, reads, writes, errors, (int)timeout));
}

int OS2_getsockname(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    TRACE_NATIVE("getsockname(%d, %p, %p)", sock, os2name, os2namelen);

    if (!os2name || !os2namelen || *os2namelen < 16)
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    LxHostSocketAddress bsdaddr;
    const int rc = lx_host_socket_name(sock, &bsdaddr);
    if (rc == -1)
    {
        return socket_result(-1);
    }

    mapBSDSockAddrIn(&bsdaddr, os2name);
    *os2namelen = sizeof(OS2_sockaddr_in);
    return socket_result(rc);
}

int OS2_bind(int sock, const OS2_sockaddr *os2name, int os2namelen)
{
    TRACE_NATIVE("bind(%d, %p, %d)", sock, os2name, os2namelen);
    if (!os2name || os2namelen < 16)
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    if (os2name->sa_family != OS2_AF_INET)
    {
        errno = EAFNOSUPPORT;
        return socket_result(-1);
    }
    LxHostSocketAddress bsdaddr;
    mapOS2SockAddrIn(os2name, &bsdaddr);
    return socket_result(lx_host_socket_bind(sock, &bsdaddr));
}

int OS2_listen(int sock, int backlog)
{
    TRACE_NATIVE("listen(%d, %d)", sock, backlog);
    return socket_result(lx_host_socket_listen(sock, backlog));
}

int OS2_accept(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    TRACE_NATIVE("bind(%d, %p, %p)", sock, os2name, os2namelen);
    LxHostSocketAddress bsdaddr;
    if (os2name && (!os2namelen || *os2namelen < 16))
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    const int retval = lx_host_socket_accept(sock, &bsdaddr);
    if (retval < 0)
        return socket_result(retval);

    if (os2name != NULL)
    {
        mapBSDSockAddrIn(&bsdaddr, os2name);
    }

    if (os2namelen != NULL)
    {
        *os2namelen = sizeof(OS2_sockaddr_in);
    }

    return socket_result(retval);
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
    if (os2level == 0)
        return socket_result(lx_host_socket_ip_option(sock, os2name, value, len));
    const int bsdname = mapOS2SocketOption(os2name);
    if (!bsdname)
    {
        errno = ENOPROTOOPT;
        return socket_result(-1);
    }
    else if (os2level != OS2_SOL_SOCKET)
    {
        errno = ENOPROTOOPT;
        return socket_result(-1);
    }
    return socket_result(lx_host_socket_option(sock, (LxHostSocketOption)bsdname, value, len));
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

int OS2_recvfrom(int sock, void *buffer, int length, int flags, OS2_sockaddr *address,
                 int *address_length)
{
    if (length < 0 || (!buffer && length) || (address && (!address_length || *address_length < 16)))
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    LxHostSocketAddress peer;
    int result =
        (int)lx_host_socket_recvfrom(sock, buffer, (size_t)length, mapOS2MsgFlags(flags), &peer);
    if (result >= 0 && address)
    {
        mapBSDSockAddrIn(&peer, address);
        *address_length = 16;
    }
    return socket_result(result);
}

int OS2_sendto(int sock, const void *buffer, int length, int flags, const OS2_sockaddr *address,
               int address_length)
{
    if (length < 0 || (!buffer && length) || !address || address_length < 16)
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    if (address->sa_family != OS2_AF_INET)
    {
        errno = EAFNOSUPPORT;
        return socket_result(-1);
    }
    LxHostSocketAddress peer;
    mapOS2SockAddrIn(address, &peer);
    return socket_result(
        (int)lx_host_socket_sendto(sock, buffer, (size_t)length, mapOS2MsgFlags(flags), &peer));
}

int OS2_getpeername(int sock, OS2_sockaddr *address, int *length)
{
    if (!address || !length || *length < 16)
    {
        errno = EFAULT;
        return socket_result(-1);
    }
    LxHostSocketAddress peer;
    int result = lx_host_socket_peer(sock, &peer);
    if (!result)
    {
        mapBSDSockAddrIn(&peer, address);
        *length = 16;
    }
    return socket_result(result);
}

int OS2_getsockopt(int sock, int level, int option, void *value, int *length)
{
    if (level != OS2_SOL_SOCKET)
    {
        errno = ENOPROTOOPT;
        return socket_result(-1);
    }
    int result =
        socket_result(lx_host_socket_get_option(sock, mapOS2SocketOption(option), value, length));
    if (!result && option == OS2_SO_ERROR && *length == 4)
    {
        int number;
        memcpy(&number, value, 4);
        if (number)
        {
            number += 10000;
            memcpy(value, &number, 4);
        }
    }
    return result;
}

void OS2_psock_errno(const char *message)
{
    fprintf(stderr, "%s: socket error %d\n", message ? message : "socket", OS2_sock_errno());
}

OS2_hostent *OS2_gethostbyaddr(const void *address, int length, int family)
{
    static LX_HOST_THREAD_LOCAL OS2_hostent output;
    LxHostSocketHost host;
    if (!address || length != 4 || family != 2)
    {
        errno = EINVAL;
        ResolverError = 3;
        return NULL;
    }
    uint32_t ip;
    memcpy(&ip, address, 4);
    int ok = lx_host_socket_reverse(ip, &host);
    ResolverError = lx_host_socket_resolver_error();
    if (!ok)
        return NULL;
    output.h_name = host.name;
    output.h_aliases = host.aliases;
    output.h_addrtype = host.family;
    output.h_length = host.length;
    output.h_addr_list = host.addresses;
    return &output;
}

static OS2_protoent *protocol_result(const char *name, int number)
{
    static LX_HOST_THREAD_LOCAL OS2_protoent output;
    if (!lx_host_socket_protocol(name, number, &output.p_name, &output.p_aliases, &output.p_proto))
        return NULL;
    return &output;
}

OS2_protoent *OS2_getprotobyname(const char *name)
{
    return name ? protocol_result(name, 0) : NULL;
}

OS2_protoent *OS2_getprotobynumber(int number)
{
    return protocol_result(NULL, number);
}

OS2_servent *OS2_getservbyport(int port, const char *protocol)
{
    static LX_HOST_THREAD_LOCAL OS2_servent output;
    LxHostSocketService host;
    if (!lx_host_socket_service_port(port, protocol, &host))
        return NULL;
    output.s_name = host.name;
    output.s_aliases = host.aliases;
    output.s_port = host.port;
    output.s_proto = host.protocol;
    return &output;
}

void OS2_setprotoent(int stayopen)
{
    (void)stayopen;
}

void OS2_endprotoent(void) {}

void OS2_setservent(int stayopen)
{
    (void)stayopen;
}

void OS2_endservent(void) {}

int OS2_gethostname(char *name, int length)
{
    if (!name || length <= 0)
    {
        errno = EFAULT;
        return -1;
    }
    return lx_host_socket_hostname(name, (size_t)length);
}

uint32_t OS2_gethostid(void)
{
    LxHostNetwork network;
    if (lx_host_network_snapshot(&network))
        return 0;
    uint32_t result = 0;
    for (size_t i = 0; i < network.interface_count; i++)
        if ((network.interfaces[i].flags & LX_IFF_UP) &&
            !(network.interfaces[i].flags & LX_IFF_LOOPBACK))
        {
            result = network.interfaces[i].address;
            break;
        }
    lx_host_network_destroy(&network);
    return result;
}

uint32_t OS2_htonl(uint32_t value)
{
    if (lx_host_socket_network16(1) == 1)
        return value;
    return ((value & 255u) << 24) | ((value & 65280u) << 8) | ((value >> 8) & 65280u) |
           (value >> 24);
}

uint32_t OS2_ntohl(uint32_t value)
{
    return OS2_htonl(value);
}

uint16_t OS2_ntohs(uint16_t value)
{
    return OS2_htons(value);
}

int OS2_writev(int sock, const OS2_iovec *vectors, int count)
{
    if (count < 0 || count > 1024 || (!vectors && count))
    {
        errno = EINVAL;
        return socket_result(-1);
    }
    size_t total = 0;
    for (int i = 0; i < count; i++)
    {
        if (vectors[i].length > 16u * 1024 * 1024 - total)
        {
            errno = EMSGSIZE;
            return socket_result(-1);
        }
        total += vectors[i].length;
    }
    unsigned char *data = malloc(total ? total : 1);
    if (!data)
        return socket_result(-1);
    size_t offset = 0;
    for (int i = 0; i < count; i++)
    {
        if (vectors[i].length)
            memcpy(data + offset, vectors[i].buffer, vectors[i].length);
        offset += vectors[i].length;
    }
    int result = (int)lx_host_socket_send(sock, data, total, 0);
    free(data);
    return socket_result(result);
}

/* Toolkit resolver state is two packed 512-byte records containing guest pointers. */
static LxMmaps ResolverObject;
static LxHostMutex *ResolverMutex;

static int initializeResolverData(void)
{
    if (ResolverObject.mapping)
        return 1;
    ResolverObject.mapping =
        lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!ResolverObject.mapping)
        return 0;
    ResolverObject.addr = ResolverObject.mapped = lx_execution_mapping_host(ResolverObject.mapping);
    ResolverObject.guest_base = lx_execution_mapping_address(ResolverObject.mapping);
    ResolverObject.size = 4096;
    ResolverObject.bits = 32;
    ResolverObject.alias = 0xffff;
    ResolverMutex = lx_host_mutex_create(0, 0);
    if (!ResolverMutex)
    {
        lx_execution_unmap(GLoaderState.execution, ResolverObject.mapping);
        memset(&ResolverObject, 0, sizeof ResolverObject);
    }
    return ResolverMutex != NULL;
}

int OS2_res_init(void)
{
    if (!initializeResolverData())
        return -1;
    LxHostNetwork network;
    if (lx_host_network_snapshot(&network))
        return -1;
    lx_host_mutex_lock(ResolverMutex, UINT32_MAX);
    uint8 *data = ResolverObject.addr;
    memset(data, 0, 4096);
    lx_guest_store_u32(data, 5);
    lx_guest_store_u32(data + 4, 2);
    lx_guest_store_u32(data + 8, 0x2c1);
    unsigned count = 0;
    for (size_t i = 0; i < network.dns.server_count && count < 3; i++)
    {
        uint32_t ip = lx_host_socket_parse_address(network.dns.servers[i]);
        if (ip == UINT32_MAX)
            continue;
        OS2_sockaddr_in address = {0};
        address.sin_len = 16;
        address.sin_family = 2;
        address.sin_port = lx_host_socket_network16(53);
        address.sin_addr.s_addr = ip;
        memcpy(data + 16 + count++ * 16, &address, 16);
    }
    lx_guest_store_u32(data + 12, count);
    lx_guest_store_u32(data + 356, 1); /* ndots */
    unsigned offset = 1024;
    for (size_t i = 0; i < network.dns.search_count && i < 6; i++)
    {
        size_t n = strlen(network.dns.search[i]) + 1;
        if (n > 256 || offset + n > 4096)
            continue;
        memcpy(data + offset, network.dns.search[i], n);
        lx_guest_store_u32(data + 324 + i * 4, ResolverObject.guest_base + offset);
        if (!i)
            memcpy(data + 66, network.dns.search[i], n);
        offset += (unsigned)n;
    }
    memcpy(data + 512, data, 512);
    lx_host_mutex_unlock(ResolverMutex);
    lx_host_network_destroy(&network);
    return 0;
}

#if LX_LEGACY
OS2EXPORT void lxNativeModuleDeinit(void)
{
    if (ResolverObject.mapping)
        lx_execution_unmap(GLoaderState.execution, ResolverObject.mapping);
    memset(&ResolverObject, 0, sizeof ResolverObject);
    if (ResolverMutex)
        lx_host_mutex_destroy(ResolverMutex);
    ResolverMutex = NULL;
    lxTcpipGeneratedDeinit();
}

OS2EXPORT const LxExport *lxNativeModuleInit(uint32 *count)
{
    static LxExport exports[256];
    static char uppercase[128][64];
    uint32 n = 0;
    const LxExport *generated = lxTcpipGeneratedInit(&n);
    if (!generated || n >= 128 || !initializeResolverData())
        return NULL;
    memcpy(exports, generated, n * sizeof *exports);
    exports[n++] = (LxExport){143, "_ress", (void *)(uintptr_t)ResolverObject.guest_base, NULL,
                              LX_EXPORT_ABSOLUTE};
    unsigned original = n;
    for (unsigned i = 0; i < original; i++)
    {
        if (!exports[i].name || strlen(exports[i].name) >= 64)
            continue;
        size_t j;
        for (j = 0; exports[i].name[j]; j++)
            uppercase[i][j] = (char)toupper((unsigned char)exports[i].name[j]);
        uppercase[i][j] = 0;
        if (strcmp(uppercase[i], exports[i].name))
        {
            exports[n] = exports[i];
            exports[n].ordinal = 0;
            exports[n++].name = uppercase[i];
        }
    }
    *count = n;
    return exports;
}
#endif
uint32_t OS2_inet_network(const char *name)
{
    return name ? lx_host_socket_parse_network(name) : UINT32_MAX;
}

int *OS2_tcp_h_errno1(void)
{
    return &ResolverError;
}
