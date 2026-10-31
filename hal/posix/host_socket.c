/* Derived from original 2ine; zlib-style license. See ../../LICENSE.txt. */
/* BSD sockets and resolver storage behind platform-neutral host contracts. */
#define _DEFAULT_SOURCE 1
#include "hal/socket.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
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
