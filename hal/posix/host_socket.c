/* Derived from original 2ine; zlib-style license. See ../../LICENSE.txt. */
/* BSD sockets and resolver storage behind platform-neutral host contracts. */
#define _DEFAULT_SOURCE 1
#include "hal/socket.h"
#include "diagnostic_socket.h"
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
#include <limits.h>
#include <pthread.h>
#include "hal/host_thread.h"

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
    return lx_host_socket_open_protocol(type, 0);
}

int lx_host_socket_open_protocol(int type, int protocol)
{
    if (type == LX_SOCKET_RAW)
        return lx_diagnostic_open(protocol);
    if (type != LX_SOCKET_STREAM && type != LX_SOCKET_DGRAM)
    {
        errno = ESOCKTNOSUPPORT;
        return -1;
    }
    return socket(AF_INET, type == LX_SOCKET_STREAM ? SOCK_STREAM : SOCK_DGRAM, protocol);
}

int lx_host_socket_close(int fd)
{
    lx_diagnostic_close(fd);
    return close(fd);
}

int lx_host_socket_connect(int fd, const LxHostSocketAddress *address)
{
    int handled;
    int result = lx_diagnostic_connect(fd, address, &handled);
    if (handled)
        return result;
    struct sockaddr_in a = address_in(address);
    if (bind_sender(fd) < 0)
        return -1;
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

int lx_host_socket_peer(int fd, LxHostSocketAddress *address)
{
    int handled;
    int diagnostic = lx_diagnostic_peer(fd, address, &handled);
    if (handled)
        return diagnostic;
    struct sockaddr_in a = {0};
    socklen_t n = sizeof a;
    int result = getpeername(fd, (struct sockaddr *)&a, &n);
    if (!result)
        address_out(&a, address);
    return result;
}

int lx_host_socket_shutdown(int fd, int how)
{
    return shutdown(fd, how);
}

int64_t lx_host_socket_send(int fd, const void *buffer, size_t size, unsigned flags)
{
    if (lx_diagnostic_type(fd))
    {
        int handled;
        return lx_diagnostic_send(fd, buffer, size, message_flags(flags), NULL, &handled);
    }
    int native = message_flags(flags);
#ifdef MSG_NOSIGNAL
    native |= MSG_NOSIGNAL;
#elif defined(SO_NOSIGPIPE)
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled) < 0)
        return -1;
#endif
    return send(fd, buffer, size, native);
}

int64_t lx_host_socket_recv(int fd, void *buffer, size_t size, unsigned flags)
{
    if (lx_diagnostic_type(fd))
        return lx_host_socket_recvfrom(fd, buffer, size, flags, NULL);
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
    int handled;
    int64_t result = lx_diagnostic_send(fd, buffer, size, message_flags(flags), address, &handled);
    if (handled)
        return result;
    if (bind_sender(fd) < 0)
        return -1;
    return sendto(fd, buffer, size, message_flags(flags), (struct sockaddr *)&a, sizeof a);
}

int64_t lx_host_socket_recvfrom(int fd, void *buffer, size_t size, unsigned flags,
                                LxHostSocketAddress *address)
{
    int handled;
    int64_t diagnostic =
        lx_diagnostic_recv(fd, buffer, size, message_flags(flags), address, &handled);
    if (handled)
        return diagnostic;
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

static int bsd_error(int number)
{
    switch (number)
    {
        case 0:
            return 0;
#define ERROR(host, bsd)                                                                           \
    case host:                                                                                     \
        return bsd
            ERROR(EPERM, 1);
            ERROR(ENOENT, 2);
            ERROR(EINTR, 4);
            ERROR(ENXIO, 6);
            ERROR(EBADF, 9);
            ERROR(ENOMEM, 12);
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

int lx_host_socket_bsd_error(void)
{
    return bsd_error(errno);
}

uint32_t lx_host_socket_parse_address(const char *name)
{
    return inet_addr(name);
}

uint32_t lx_host_socket_parse_network(const char *name)
{
    return inet_network(name);
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

/* libc database functions may share process storage. Copy it while holding the lock. */
static pthread_mutex_t DatabaseMutex = PTHREAD_MUTEX_INITIALIZER;
static LX_HOST_THREAD_LOCAL int ResolverError;

typedef struct DatabaseStorage
{
    char data[65536];
    size_t used;
    char *aliases[257], *addresses[257];
} DatabaseStorage;

static char *database_copy(DatabaseStorage *storage, const void *data, size_t size)
{
    if (size > sizeof storage->data - storage->used)
        return NULL;
    char *result = storage->data + storage->used;
    memcpy(result, data, size);
    storage->used += size;
    return result;
}

static int database_aliases(DatabaseStorage *storage, char **aliases)
{
    unsigned count = 0;
    for (; aliases && aliases[count]; count++)
    {
        if (count == 256 || !(storage->aliases[count] = database_copy(storage, aliases[count],
                                                                      strlen(aliases[count]) + 1)))
            return 0;
    }
    storage->aliases[count] = NULL;
    return 1;
}

static int copy_host(struct hostent *host, LxHostSocketHost *result)
{
    static LX_HOST_THREAD_LOCAL DatabaseStorage storage;
    if (!host)
    {
        ResolverError = h_errno > 0 ? h_errno : 3;
        return 0;
    }
    storage.used = 0;
    ResolverError = 3;
    if (host->h_addrtype != AF_INET || host->h_length != 4)
        return 0;
    char *name = database_copy(&storage, host->h_name, strlen(host->h_name) + 1);
    if (!name || !database_aliases(&storage, host->h_aliases))
        return 0;
    unsigned count = 0;
    for (; host->h_addr_list[count]; count++)
    {
        if (count == 256 ||
            !(storage.addresses[count] = database_copy(&storage, host->h_addr_list[count], 4)))
            return 0;
    }
    storage.addresses[count] = NULL;
    *result = (LxHostSocketHost){name, storage.aliases, 2, 4, storage.addresses};
    ResolverError = 0;
    return 1;
}

int lx_host_socket_host(const char *name, LxHostSocketHost *result)
{
    if (!name || !result)
    {
        ResolverError = 3;
        errno = EFAULT;
        return 0;
    }
    pthread_mutex_lock(&DatabaseMutex);
    int ok = copy_host(gethostbyname(name), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

int lx_host_socket_reverse(uint32_t address, LxHostSocketHost *result)
{
    if (!result)
    {
        ResolverError = 3;
        errno = EFAULT;
        return 0;
    }
    pthread_mutex_lock(&DatabaseMutex);
    int ok = copy_host(gethostbyaddr(&address, 4, AF_INET), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

int lx_host_socket_resolver_error(void)
{
    return ResolverError;
}

int lx_host_socket_protocol(const char *name, int number, char **official, char ***aliases,
                            int *protocol)
{
    static LX_HOST_THREAD_LOCAL DatabaseStorage storage;
    pthread_mutex_lock(&DatabaseMutex);
    struct protoent *p = name ? getprotobyname(name) : getprotobynumber(number);
    storage.used = 0;
    int ok = p && (*official = database_copy(&storage, p->p_name, strlen(p->p_name) + 1)) &&
             database_aliases(&storage, p->p_aliases);
    if (ok)
    {
        *aliases = storage.aliases;
        *protocol = p->p_proto;
    }
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

static int copy_service(struct servent *service, LxHostSocketService *result)
{
    static LX_HOST_THREAD_LOCAL DatabaseStorage storage;
    storage.used = 0;
    if (!service)
        return 0;
    char *name = database_copy(&storage, service->s_name, strlen(service->s_name) + 1);
    char *protocol = database_copy(&storage, service->s_proto, strlen(service->s_proto) + 1);
    if (!name || !protocol || !database_aliases(&storage, service->s_aliases))
        return 0;
    *result = (LxHostSocketService){name, storage.aliases, service->s_port, protocol};
    return 1;
}

int lx_host_socket_service_port(int port, const char *protocol, LxHostSocketService *result)
{
    pthread_mutex_lock(&DatabaseMutex);
    int ok = copy_service(getservbyport(port, protocol), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

int lx_host_socket_service(const char *name, const char *protocol, LxHostSocketService *result)
{
    pthread_mutex_lock(&DatabaseMutex);
    int ok = name && copy_service(getservbyname(name, protocol), result);
    pthread_mutex_unlock(&DatabaseMutex);
    return ok;
}

int lx_host_socket_select(int limit, LxHostSocketSet *r, LxHostSocketSet *w, LxHostSocketSet *e,
                          int64_t seconds, int32_t micros, int timed)
{
    (void)limit; /* OS/2 descriptor arrays do not use the BSD bit-set upper bound. */
    LxHostSocketSet *sets[] = {r, w, e};
    int counts[3] = {0}, sockets[3072], offset = 0;
    if (timed && (seconds < 0 || micros < 0 || micros >= 1000000))
    {
        errno = EINVAL;
        return -1;
    }
    for (unsigned i = 0; i < 3; i++)
    {
        if (!sets[i])
            continue;
        counts[i] = sets[i]->count;
        if (counts[i] < 0 || counts[i] > 1024 || (counts[i] && !sets[i]->sockets))
        {
            errno = EINVAL;
            return -1;
        }
        for (int j = 0; j < counts[i]; j++)
        {
            int fd = sets[i]->sockets[j];
            if (fd < 0)
            {
                errno = EBADF;
                return -1;
            }
            sockets[offset++] = fd;
        }
    }
    int timeout = !timed                              ? -1
                  : seconds > (INT_MAX - 1000) / 1000 ? INT_MAX
                                                      : (int)seconds * 1000 + (micros + 999) / 1000;
    int result = lx_host_socket_poll_update(sockets, counts[0], counts[1], counts[2], timeout);
    if (result < 0)
        return result;
    offset = 0;
    for (unsigned i = 0; i < 3; i++)
    {
        if (!sets[i])
            continue;
        int used = 0;
        for (int j = 0; j < counts[i]; j++, offset++)
            if (sockets[offset] >= 0)
                sets[i]->sockets[used++] = sockets[offset];
        sets[i]->count = used;
    }
    return result;
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

int lx_host_socket_poll_update(int *sockets, int reads, int writes, int errors, int timeout)
{
    if (reads < 0 || writes < 0 || errors < 0 || reads > 1024 || writes > 1024 || errors > 1024)
    {
        errno = EINVAL;
        return -1;
    }
    int count = reads + writes + errors;
    if (!sockets && count)
    {
        errno = EFAULT;
        return -1;
    }
    struct pollfd *p = calloc(count ? (size_t)count : 1, sizeof *p);
    if (!p)
        return -1;
    for (int i = 0; i < count; i++)
    {
        p[i].fd = sockets[i];
        p[i].events = i < reads ? POLLIN : i < reads + writes ? POLLOUT : POLLPRI;
    }
    int result = poll(p, (nfds_t)count, timeout);
    if (result >= 0)
        for (int i = 0; i < count; i++)
            if (p[i].revents & POLLNVAL)
            {
                errno = EBADF;
                result = -1;
                break;
            }
    if (result >= 0)
    {
        result = 0;
        for (int i = 0; i < count; i++)
            if (!(p[i].revents & (p[i].events | (i < reads + writes ? POLLHUP | POLLERR : 0))))
                sockets[i] = -1;
            else
                result++;
    }
    free(p);
    return result;
}

int lx_host_socket_ip_option(int fd, int option, const void *value, int size)
{
    int handled;
    int result = lx_diagnostic_option(fd, option, value, size, &handled);
    if (handled)
        return result;
    int host;
    switch (option)
    {
        case 1:
            host = IP_OPTIONS;
            break;
        case 2:
            host = IP_MULTICAST_IF;
            break;
        case 3:
            host = IP_MULTICAST_TTL;
            break;
        case 4:
            host = IP_MULTICAST_LOOP;
            break;
        case 5:
            host = IP_ADD_MEMBERSHIP;
            break;
        case 6:
            host = IP_DROP_MEMBERSHIP;
            break;
        case 7:
            host = IP_HDRINCL;
            break;
        case 8:
            host = IP_TOS;
            break;
        case 9:
            host = IP_TTL;
            break;
        default:
            errno = ENOPROTOOPT;
            return -1;
    }
    if (size < 0 || (!value && size))
    {
        errno = EINVAL;
        return -1;
    }
    return setsockopt(fd, IPPROTO_IP, host, value, (socklen_t)size);
}

int lx_host_socket_get_option(int fd, LxHostSocketOption option, void *value, int *size)
{
    if (!value || !size || *size < 4)
    {
        errno = EFAULT;
        return -1;
    }
    int host;
    if (option == LX_SO_SNDTIMEO || option == LX_SO_RCVTIMEO)
    {
        if (*size < 8)
        {
            errno = EINVAL;
            return -1;
        }
        struct timeval timeout;
        socklen_t n = sizeof timeout;
        if (getsockopt(fd, SOL_SOCKET, option == LX_SO_SNDTIMEO ? SO_SNDTIMEO : SO_RCVTIMEO,
                       &timeout, &n))
            return -1;
        int32_t words[2] = {(int32_t)timeout.tv_sec, (int32_t)timeout.tv_usec};
        memcpy(value, words, 8);
        *size = 8;
        return 0;
    }
    switch (option)
    {
#define GET(s)                                                                                     \
    case LX_##s:                                                                                   \
        host = s;                                                                                  \
        break
        GET(SO_DEBUG);
        GET(SO_ACCEPTCONN);
        GET(SO_REUSEADDR);
        GET(SO_KEEPALIVE);
        GET(SO_DONTROUTE);
        GET(SO_BROADCAST);
        GET(SO_LINGER);
        GET(SO_OOBINLINE);
        GET(SO_REUSEPORT);
        GET(SO_SNDBUF);
        GET(SO_RCVBUF);
        GET(SO_SNDLOWAT);
        GET(SO_RCVLOWAT);
        GET(SO_ERROR);
        GET(SO_TYPE);
#undef GET
        default:
            errno = ENOPROTOOPT;
            return -1;
    }
    socklen_t n = (socklen_t)*size;
    int result = getsockopt(fd, SOL_SOCKET, host, value, &n);
    if (!result)
    {
        *size = (int)n;
        int number;
        if (option == LX_SO_TYPE && lx_diagnostic_type(fd))
        {
            number = LX_SOCKET_RAW;
            memcpy(value, &number, 4);
        }
        if (option == LX_SO_ERROR && n == 4)
        {
            memcpy(&number, value, 4);
            number = bsd_error(number);
            memcpy(value, &number, 4);
        }
    }
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
            errno = ENOPROTOOPT;
            return -1;
    }
    if (option == LX_SO_SNDTIMEO || option == LX_SO_RCVTIMEO)
    {
        if (size != 8 || !value)
        {
            errno = EINVAL;
            return -1;
        }
        int32_t words[2];
        memcpy(words, value, 8);
        if (words[0] < 0 || words[1] < 0 || words[1] >= 1000000)
        {
            errno = EINVAL;
            return -1;
        }
        struct timeval timeout = {words[0], words[1]};
        return setsockopt(fd, SOL_SOCKET, name, &timeout, sizeof timeout);
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
