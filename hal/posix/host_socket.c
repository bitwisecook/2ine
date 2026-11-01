/* Derived from original 2ine; zlib-style license. See ../../LICENSE.txt. */
/* BSD sockets and resolver storage behind platform-neutral host contracts. */
#define _DEFAULT_SOURCE 1
#include "hal/socket.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static struct sockaddr_in address_in(const LxHostSocketAddress *address)
{
    struct sockaddr_in result;
    memset(&result, 0, sizeof result);
    result.sin_family = AF_INET;
    result.sin_port = address->port;
    result.sin_addr.s_addr = address->address;
    return result;
}
static void address_out(const struct sockaddr_in *source, LxHostSocketAddress *target)
{
    target->address = source->sin_addr.s_addr;
    target->port = source->sin_port;
}
/* Restrict wildcard sockets to a per-process host IPv4 identity when selected. */
static int local_address(struct in_addr *address)
{
    const char *value = getenv("TWOINE_SOCKET_BIND_ADDRESS");
    if (!value || !*value)
        return 0;
    if (inet_pton(AF_INET, value, address) == 1)
        return 1;
    errno = EINVAL;
    return -1;
}
static int bind_sender(int fd)
{
    struct in_addr selected;
    int policy = local_address(&selected);
    if (policy <= 0)
        return policy;
    struct sockaddr_in current = {0};
    socklen_t length = sizeof current;
    if (getsockname(fd, (struct sockaddr *)&current, &length) < 0)
        return -1;
    if (current.sin_family != AF_INET || current.sin_port || current.sin_addr.s_addr)
        return 0;
    current.sin_addr = selected;
    return bind(fd, (struct sockaddr *)&current, sizeof current);
}
static int message_flags(unsigned flags)
{
    int result = 0;
#define FLAG(name)                                                                                 \
    if (flags & LX_##name)                                                                         \
    result |= name
    FLAG(MSG_OOB);
    FLAG(MSG_PEEK);
    FLAG(MSG_DONTROUTE);
    FLAG(MSG_EOR);
    FLAG(MSG_TRUNC);
    FLAG(MSG_CTRUNC);
    FLAG(MSG_WAITALL);
    FLAG(MSG_DONTWAIT);
#undef FLAG
    return result;
}
int lx_host_socket_open(int type)
{
    return socket(AF_INET, type == LX_SOCKET_STREAM ? SOCK_STREAM : SOCK_DGRAM, 0);
}
int lx_host_socket_close(int fd)
{
    return close(fd);
}
int lx_host_socket_connect(int fd, const LxHostSocketAddress *address)
{
    struct sockaddr_in a = address_in(address);
    return connect(fd, (struct sockaddr *)&a, sizeof a);
}
int lx_host_socket_bind(int fd, const LxHostSocketAddress *address)
{
    struct sockaddr_in a = address_in(address);
    struct in_addr selected;
    int policy = local_address(&selected);
    if (policy < 0)
        return -1;
    if (policy && a.sin_addr.s_addr == INADDR_ANY)
        a.sin_addr = selected;
    return bind(fd, (struct sockaddr *)&a, sizeof a);
}
int lx_host_socket_listen(int fd, int backlog)
{
    return listen(fd, backlog);
}
int lx_host_socket_accept(int fd, LxHostSocketAddress *address)
{
    struct sockaddr_in a = {0};
    socklen_t length = sizeof a;
    int r = accept(fd, (struct sockaddr *)&a, &length);
    if (address)
        address_out(&a, address);
    return r;
}
int lx_host_socket_name(int fd, LxHostSocketAddress *address)
{
    struct sockaddr_in a = {0};
    socklen_t length = sizeof a;
    int r = getsockname(fd, (struct sockaddr *)&a, &length);
    if (r < 0 || a.sin_family != AF_INET)
        return -1;
    address_out(&a, address);
    return r;
}
int lx_host_socket_shutdown(int fd, int how)
{
    return shutdown(fd, how);
}
int64_t lx_host_socket_send(int fd, const void *buffer, size_t size, unsigned flags)
{
    return send(fd, buffer, size, message_flags(flags));
}
int64_t lx_host_socket_recv(int fd, void *buffer, size_t size, unsigned flags)
{
    return recv(fd, buffer, size, message_flags(flags));
}
int lx_host_socket_error(void)
{
    return errno;
}
int64_t lx_host_socket_sendto(int fd, const void *buffer, size_t size, unsigned flags,
                              const LxHostSocketAddress *address)
{
    struct sockaddr_in a = address_in(address);
    if (bind_sender(fd) < 0)
        return -1;
    return sendto(fd, buffer, size, message_flags(flags), (struct sockaddr *)&a, sizeof a);
}
int64_t lx_host_socket_recvfrom(int fd, void *buffer, size_t size, unsigned flags,
                                LxHostSocketAddress *address)
{
    struct sockaddr_in a = {0};
    socklen_t length = sizeof a;
    int64_t result =
        recvfrom(fd, buffer, size, message_flags(flags), (struct sockaddr *)&a, &length);
    if (result >= 0 && address)
        address_out(&a, address);
    return result;
}
int lx_host_socket_nonblocking(int fd, int enabled)
{
    return ioctl(fd, FIONBIO, &enabled);
}
int lx_host_socket_available(int fd, int *bytes)
{
#ifdef SO_NREAD
    int type;
    socklen_t size = sizeof type;
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) < 0)
        return -1;
    /* Darwin FIONREAD includes the queued datagram's address record. */
    if (type == SOCK_DGRAM)
    {
        size = sizeof *bytes;
        return getsockopt(fd, SOL_SOCKET, SO_NREAD, bytes, &size);
    }
#endif
    return ioctl(fd, FIONREAD, bytes);
}
int lx_host_socket_hostname(char *name, size_t size)
{
    return gethostname(name, size);
}
int lx_host_socket_bsd_error(void)
{
    switch (errno)
    {
#define ERROR(host, bsd)                                                                           \
    case host:                                                                                     \
        return bsd
        ERROR(EPERM, 1);
        ERROR(EINTR, 4);
        ERROR(EBADF, 9);
        ERROR(EACCES, 13);
        ERROR(EFAULT, 14);
        ERROR(EINVAL, 22);
        ERROR(EMFILE, 24);
        ERROR(EPIPE, 32);
        ERROR(EAGAIN, 35);
        ERROR(EINPROGRESS, 36);
        ERROR(EALREADY, 37);
        ERROR(ENOTSOCK, 38);
        ERROR(EDESTADDRREQ, 39);
        ERROR(EMSGSIZE, 40);
        ERROR(EPROTOTYPE, 41);
        ERROR(ENOPROTOOPT, 42);
        ERROR(EPROTONOSUPPORT, 43);
        ERROR(ESOCKTNOSUPPORT, 44);
        ERROR(EOPNOTSUPP, 45);
        ERROR(EAFNOSUPPORT, 47);
        ERROR(EADDRINUSE, 48);
        ERROR(EADDRNOTAVAIL, 49);
        ERROR(ENETDOWN, 50);
        ERROR(ENETUNREACH, 51);
        ERROR(ENETRESET, 52);
        ERROR(ECONNABORTED, 53);
        ERROR(ECONNRESET, 54);
        ERROR(ENOBUFS, 55);
        ERROR(EISCONN, 56);
        ERROR(ENOTCONN, 57);
        ERROR(ETIMEDOUT, 60);
        ERROR(ECONNREFUSED, 61);
        ERROR(EHOSTUNREACH, 65);
#undef ERROR
        default:
            return 100;
    }
}
uint32_t lx_host_socket_parse_address(const char *name)
{
    return inet_addr(name);
}
char *lx_host_socket_format_address(uint32_t value)
{
    struct in_addr a;
    a.s_addr = value;
    return inet_ntoa(a);
}
uint16_t lx_host_socket_network16(uint16_t value)
{
    return htons(value);
}
int lx_host_socket_host(const char *name, LxHostSocketHost *result)
{
    struct hostent *h = gethostbyname(name);
    if (!h)
        return 0;
    result->name = h->h_name;
    result->aliases = h->h_aliases;
    result->family = h->h_addrtype;
    result->length = h->h_length;
    result->addresses = h->h_addr_list;
    return 1;
}
int lx_host_socket_service(const char *name, const char *protocol, LxHostSocketService *result)
{
    struct servent *s = getservbyname(name, protocol);
    if (!s)
        return 0;
    result->name = s->s_name;
    result->aliases = s->s_aliases;
    result->port = s->s_port;
    result->protocol = s->s_proto;
    return 1;
}
static void fd_set_in(const LxHostSocketSet *source, fd_set *target)
{
    FD_ZERO(target);
    for (int i = 0; i < source->count; i++)
        FD_SET(source->sockets[i], target);
}
int lx_host_socket_select(int limit, const LxHostSocketSet *r, const LxHostSocketSet *w,
                          const LxHostSocketSet *e, int64_t seconds, int32_t micros, int timed)
{
    fd_set a, b, c;
    if (r)
        fd_set_in(r, &a);
    if (w)
        fd_set_in(w, &b);
    if (e)
        fd_set_in(e, &c);
    struct timeval timeout = {seconds, micros};
    return select(limit, r ? &a : NULL, w ? &b : NULL, e ? &c : NULL, timed ? &timeout : NULL);
}
int lx_host_socket_poll(const int *sockets, int reads, int writes, int errors, int timeout)
{
    int count = reads + writes + errors;
    struct pollfd *fds = calloc((size_t)count, sizeof *fds);
    if (!fds && count)
        return -1;
    for (int i = 0; i < count; i++)
    {
        fds[i].fd = sockets[i];
        fds[i].events = i < reads ? POLLIN : i < reads + writes ? POLLOUT : POLLERR;
    }
    int result = poll(fds, (nfds_t)count, timeout);
    free(fds);
    return result;
}
int lx_host_socket_option(int fd, LxHostSocketOption option, const void *value, int size)
{
    int name;
    switch (option)
    {
#define OPTION(s)                                                                                  \
    case LX_##s:                                                                                   \
        name = s;                                                                                  \
        break
        OPTION(SO_DEBUG);
        OPTION(SO_ACCEPTCONN);
        OPTION(SO_REUSEADDR);
        OPTION(SO_KEEPALIVE);
        OPTION(SO_DONTROUTE);
        OPTION(SO_BROADCAST);
        OPTION(SO_LINGER);
        OPTION(SO_OOBINLINE);
        OPTION(SO_REUSEPORT);
        OPTION(SO_SNDBUF);
        OPTION(SO_RCVBUF);
        OPTION(SO_SNDLOWAT);
        OPTION(SO_RCVLOWAT);
        OPTION(SO_SNDTIMEO);
        OPTION(SO_RCVTIMEO);
        OPTION(SO_ERROR);
        OPTION(SO_TYPE);
#undef OPTION
        default:
            return -1;
    }
    return setsockopt(fd, SOL_SOCKET, name, value, (socklen_t)size);
}
int lx_host_wall_time(int64_t *seconds, int32_t *micros, int32_t *west, int32_t *dst)
{
    struct timeval t;
    struct timezone zone;
    int r = gettimeofday(&t, &zone);
    *seconds = t.tv_sec;
    *micros = (int32_t)t.tv_usec;
    *west = zone.tz_minuteswest;
    *dst = zone.tz_dsttime;
    return r;
}
