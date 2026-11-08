/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "host/posix/net/raw_socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int lx_diagnostic_icmp_open(int *kind)
{
    int fd;
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    *kind = ICMP_FULL;
    return fd;
}

int64_t lx_diagnostic_icmp_recv(int fd, int kind, unsigned char *packet, size_t capacity, int flags,
                                uint16_t echo_id, LxHostSocketAddress *address)
{
    (void)kind;
    (void)echo_id;
    struct sockaddr_in peer = {0};
    socklen_t peer_size = sizeof peer;
    ssize_t n;
    n = recvfrom(fd, packet, capacity, flags, (struct sockaddr *)&peer, &peer_size);
    if (n >= 0 && address)
        *address = (LxHostSocketAddress){peer.sin_addr.s_addr, 0};
    return n;
}
