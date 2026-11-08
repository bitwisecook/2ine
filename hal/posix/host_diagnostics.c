/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "hal/posix/diagnostic_socket.h"
#include "hal/host_thread.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct DiagnosticSocket
{
    int fd, kind, header;
    uint16_t echo_id;
    int connected;
    LxHostSocketAddress peer;
    struct DiagnosticSocket *next;
} DiagnosticSocket;

static DiagnosticSocket *Sockets;
static pthread_mutex_t Mutex = PTHREAD_MUTEX_INITIALIZER;

static DiagnosticSocket lookup(int fd)
{
    DiagnosticSocket result = {0};
    pthread_mutex_lock(&Mutex);
    for (DiagnosticSocket *s = Sockets; s; s = s->next)
        if (s->fd == fd)
        {
            result = *s;
            break;
        }
    pthread_mutex_unlock(&Mutex);
    return result;
}

int lx_diagnostic_type(int fd)
{
    return lookup(fd).kind ? LX_SOCKET_RAW : 0;
}

int lx_diagnostic_connect(int fd, const LxHostSocketAddress *address, int *handled)
{
    *handled = lookup(fd).kind == UDP_PACKET;
    if (!*handled)
        return 0;
    pthread_mutex_lock(&Mutex);
    for (DiagnosticSocket *p = Sockets; p; p = p->next)
        if (p->fd == fd)
        {
            p->peer = *address;
            p->connected = 1;
            break;
        }
    pthread_mutex_unlock(&Mutex);
    return 0;
}

int lx_diagnostic_peer(int fd, LxHostSocketAddress *address, int *handled)
{
    DiagnosticSocket s = lookup(fd);
    *handled = s.kind == UDP_PACKET;
    if (!*handled)
        return 0;
    if (!s.connected)
    {
        errno = ENOTCONN;
        return -1;
    }
    *address = s.peer;
    return 0;
}

int lx_diagnostic_open(int protocol)
{
    int fd, kind;
    if (protocol == IPPROTO_ICMP)
    {
        fd = lx_diagnostic_icmp_open(&kind);
    }
    else if (protocol == IPPROTO_RAW || protocol == IPPROTO_UDP)
    {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        kind = UDP_PACKET;
    }
    else
    {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    if (fd < 0)
        return -1;
    DiagnosticSocket *s = calloc(1, sizeof *s);
    if (!s)
    {
        close(fd);
        errno = ENOMEM;
        return -1;
    }
    s->fd = fd;
    s->kind = kind;
    s->header = protocol == IPPROTO_RAW;
    pthread_mutex_lock(&Mutex);
    s->next = Sockets;
    Sockets = s;
    pthread_mutex_unlock(&Mutex);
    return fd;
}

void lx_diagnostic_close(int fd)
{
    pthread_mutex_lock(&Mutex);
    DiagnosticSocket **p = &Sockets;
    while (*p && (*p)->fd != fd)
        p = &(*p)->next;
    if (*p)
    {
        DiagnosticSocket *s = *p;
        *p = s->next;
        free(s);
    }
    pthread_mutex_unlock(&Mutex);
}

int lx_diagnostic_option(int fd, int option, const void *value, int size, int *handled)
{
    DiagnosticSocket s = lookup(fd);
    *handled = s.kind && option == 7; /* OS/2 IP_HDRINCL */
    if (!*handled)
        return 0;
    if (size != 4 || !value)
    {
        errno = EINVAL;
        return -1;
    }
    int enabled;
    memcpy(&enabled, value, 4);
    if (s.kind != UDP_PACKET && enabled)
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    pthread_mutex_lock(&Mutex);
    for (DiagnosticSocket *p = Sockets; p; p = p->next)
        if (p->fd == fd)
            p->header = enabled != 0;
    pthread_mutex_unlock(&Mutex);
    return 0;
}

static uint16_t network16(const unsigned char *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

int64_t lx_diagnostic_send(int fd, const void *buffer, size_t size, int flags,
                           const LxHostSocketAddress *address, int *handled)
{
    DiagnosticSocket s = lookup(fd);
    *handled = s.kind != 0;
    if (!*handled)
        return 0;
    if (!buffer)
    {
        errno = EFAULT;
        return -1;
    }
    int connected = address == NULL;
    LxHostSocketAddress peer;
    if (connected)
    {
        if (s.kind == UDP_PACKET)
        {
            if (!s.connected)
            {
                errno = EDESTADDRREQ;
                return -1;
            }
            peer = s.peer;
        }
        else
        {
            struct sockaddr_in a;
            socklen_t n = sizeof a;
            if (getpeername(fd, (struct sockaddr *)&a, &n))
                return -1;
            peer = (LxHostSocketAddress){a.sin_addr.s_addr, a.sin_port};
        }
        address = &peer;
    }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = address->address;
    const unsigned char *data = buffer;
    size_t skip = 0;
    if (s.kind == UDP_PACKET)
    {
        uint32_t source = 0;
        if (s.header)
        {
            if (size < 28 || (data[0] >> 4) != 4 || data[9] != IPPROTO_UDP)
            {
                errno = EINVAL;
                return -1;
            }
            skip = (data[0] & 15u) * 4;
            /* OS/2 BSD raw IP headers carry ip_off in host (x86 little-endian) order. */
            unsigned fragment = data[6] | (unsigned)data[7] << 8;
            if (skip != 20 || skip > size - 8 || (fragment & 0x3fff))
            {
                errno = EINVAL;
                return -1;
            }
            /* A host datagram supplies checksums and IP framing; preserve TTL/TOS. */
            int ttl = data[8], tos = data[1];
            if (setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof ttl) ||
                setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof tos))
                return -1;
            memcpy(&source, data + 12, 4);
            if (memcmp(data + 16, &address->address, 4))
            {
                errno = EINVAL;
                return -1;
            }
        }
        if (size - skip < 8 || network16(data + skip + 4) != size - skip)
        {
            errno = EINVAL;
            return -1;
        }
        struct sockaddr_in local = {0};
        socklen_t n = sizeof local;
        if (getsockname(fd, (struct sockaddr *)&local, &n))
            return -1;
        uint16_t port;
        memcpy(&port, data + skip, 2);
        if (!local.sin_port)
        {
            local.sin_family = AF_INET;
            local.sin_port = port;
            local.sin_addr.s_addr = source;
            if (bind(fd, (struct sockaddr *)&local, sizeof local))
                return -1;
        }
        else if (local.sin_port != port ||
                 (source && local.sin_addr.s_addr && local.sin_addr.s_addr != source))
        {
            errno = EADDRINUSE;
            return -1;
        }
        memcpy(&a.sin_port, data + skip + 2, 2);
        skip += 8;
    }
    else
    {
        if (size < 8)
        {
            errno = EINVAL;
            return -1;
        }
        pthread_mutex_lock(&Mutex);
        for (DiagnosticSocket *p = Sockets; p; p = p->next)
            if (p->fd == fd)
                memcpy(&p->echo_id, data + 4, 2);
        pthread_mutex_unlock(&Mutex);
    }
    ssize_t n = connected && s.kind != UDP_PACKET
                    ? send(fd, data + skip, size - skip, flags)
                    : sendto(fd, data + skip, size - skip, flags, (struct sockaddr *)&a, sizeof a);
    return n < 0 ? -1 : (int64_t)n + (int64_t)skip;
}

int64_t lx_diagnostic_recv(int fd, void *buffer, size_t size, int flags,
                           LxHostSocketAddress *address, int *handled)
{
    DiagnosticSocket s = lookup(fd);
    *handled = s.kind != 0;
    if (!*handled)
        return 0;
    if (s.kind == UDP_PACKET)
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    static LX_HOST_THREAD_LOCAL unsigned char packet[65536];
    ssize_t n;
    n = lx_diagnostic_icmp_recv(fd, s.kind, packet, sizeof packet, flags, s.echo_id, address);

    if (n < 0)
        return -1;
    if ((size_t)n > size)
        n = (ssize_t)size;
    if (n)
        memcpy(buffer, packet, (size_t)n);
    return n;
}
