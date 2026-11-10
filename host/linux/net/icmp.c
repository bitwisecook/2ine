/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "host/posix/net/raw_socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

/* IPv4 header (RFC 791) and ICMP echo (RFC 792) layout. A datagram ICMP socket returns only
   the ICMP message, so the receive path rebuilds the IPv4 header a raw socket would return. */
enum
{
    IPV4_HEADER_BYTES = 20,
    IPV4_VERSION_IHL = 0x45, /* version 4, five-word header */
    IPV4_IHL_MASK = 15u,
    IPV4_TOTAL_LENGTH = 2,
    IPV4_FRAGMENT = 6,
    IPV4_TTL = 8,
    IPV4_PROTOCOL = 9,
    IPV4_SOURCE = 12,
    IPV4_DESTINATION = 16,
    IPV4_DEFAULT_TTL = 64,
    ECHO_HEADER_BYTES = 8,
    ECHO_REPLY_TYPE = 0,
    ECHO_CHECKSUM = 2,
    ECHO_IDENTIFIER = 4,
    CONTROL_BYTES = 128
};

int lx_diagnostic_icmp_open(int *kind)
{
    int fd;
    fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    *kind = ICMP_FULL;
    if (fd < 0 && (errno == EPERM || errno == EACCES))
    {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        *kind = ICMP_PAYLOAD;
        if (fd >= 0)
        {
            int on = 1;
            setsockopt(fd, IPPROTO_IP, IP_RECVTTL, &on, sizeof on);
            setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &on, sizeof on);
        }
    }
    return fd;
}

static uint16_t network16(const unsigned char *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static unsigned ip_header_bytes(const unsigned char *packet)
{
    return (packet[0] & IPV4_IHL_MASK) * 4;
}

int64_t lx_diagnostic_icmp_recv(int fd, int kind, unsigned char *packet, size_t capacity, int flags,
                                uint16_t echo_id, LxHostSocketAddress *address)
{
    struct sockaddr_in peer = {0};
    socklen_t peer_size = sizeof peer;
    ssize_t n;
    if (kind == ICMP_PAYLOAD)
    {
        struct iovec iov = {packet + IPV4_HEADER_BYTES, capacity - IPV4_HEADER_BYTES};
        unsigned char control[CONTROL_BYTES];
        struct msghdr msg = {0};
        msg.msg_name = &peer;
        msg.msg_namelen = sizeof peer;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof control;
        n = recvmsg(fd, &msg, flags);
        if (n < 0)
            return -1;
        memset(packet, 0, IPV4_HEADER_BYTES);
        packet[0] = IPV4_VERSION_IHL;
        packet[IPV4_TTL] = IPV4_DEFAULT_TTL;
        packet[IPV4_PROTOCOL] = IPPROTO_ICMP;
        uint16_t length = (uint16_t)n;
        memcpy(packet + IPV4_TOTAL_LENGTH, &length, sizeof length);
        memcpy(packet + IPV4_SOURCE, &peer.sin_addr.s_addr, sizeof peer.sin_addr.s_addr);
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
        {
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_TTL)
            {
                int ttl;
                memcpy(&ttl, CMSG_DATA(c), sizeof ttl);
                packet[IPV4_TTL] = (unsigned char)ttl;
            }
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO)
            {
                struct in_pktinfo info;
                memcpy(&info, CMSG_DATA(c), sizeof info);
                memcpy(packet + IPV4_DESTINATION, &info.ipi_addr, sizeof info.ipi_addr);
            }
        }
        /* The kernel rewrites the echo identifier; restore the caller's and re-checksum. */
        unsigned char *icmp = packet + IPV4_HEADER_BYTES;
        if (n >= ECHO_HEADER_BYTES && icmp[0] == ECHO_REPLY_TYPE)
        {
            memcpy(icmp + ECHO_IDENTIFIER, &echo_id, sizeof echo_id);
            icmp[ECHO_CHECKSUM] = icmp[ECHO_CHECKSUM + 1] = 0;
            uint32_t sum = 0;
            for (ssize_t i = IPV4_HEADER_BYTES; i < n + IPV4_HEADER_BYTES; i += 2)
                sum +=
                    (uint32_t)packet[i] << 8 | (i + 1 < n + IPV4_HEADER_BYTES ? packet[i + 1] : 0);
            while (sum >> 16)
                sum = (sum & 65535) + (sum >> 16);
            sum = ~sum;
            icmp[ECHO_CHECKSUM] = (unsigned char)(sum >> 8);
            icmp[ECHO_CHECKSUM + 1] = (unsigned char)sum;
        }
        n += IPV4_HEADER_BYTES;
    }
    else
    {
        n = recvfrom(fd, packet, capacity, flags, (struct sockaddr *)&peer, &peer_size);
        /* BSD raw sockets, which OS/2 programs expect, report the IP length fields in host
           order and the total length without the header. */
        if (n >= IPV4_HEADER_BYTES && ip_header_bytes(packet) <= (unsigned)n &&
            network16(packet + IPV4_TOTAL_LENGTH) >= ip_header_bytes(packet))
        {
            uint16_t length = network16(packet + IPV4_TOTAL_LENGTH) - ip_header_bytes(packet);
            memcpy(packet + IPV4_TOTAL_LENGTH, &length, sizeof length);
            uint16_t fragment = network16(packet + IPV4_FRAGMENT);
            memcpy(packet + IPV4_FRAGMENT, &fragment, sizeof fragment);
        }
    }
    if (n >= 0 && address)
        *address = (LxHostSocketAddress){peer.sin_addr.s_addr, 0};
    return n;
}
