/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "hal/posix/diagnostic_socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
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
int64_t lx_diagnostic_icmp_recv(int fd, int kind, unsigned char *packet, size_t capacity, int flags,
                                uint16_t echo_id, LxHostSocketAddress *address)
{
    struct sockaddr_in peer = {0};
    socklen_t peer_size = sizeof peer;
    ssize_t n;
    if (kind == ICMP_PAYLOAD)
    {
        struct iovec iov = {packet + 20, capacity - 20};
        unsigned char control[128];
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
        memset(packet, 0, 20);
        packet[0] = 0x45;
        packet[8] = 64;
        packet[9] = IPPROTO_ICMP;
        uint16_t length = (uint16_t)n;
        memcpy(packet + 2, &length, 2);
        memcpy(packet + 12, &peer.sin_addr.s_addr, 4);
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
        {
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_TTL)
            {
                int ttl;
                memcpy(&ttl, CMSG_DATA(c), sizeof ttl);
                packet[8] = (unsigned char)ttl;
            }
            if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO)
            {
                struct in_pktinfo info;
                memcpy(&info, CMSG_DATA(c), sizeof info);
                memcpy(packet + 16, &info.ipi_addr, 4);
            }
        }
        if (n >= 8 && packet[20] == 0)
        {
            memcpy(packet + 24, &echo_id, 2);
            packet[22] = packet[23] = 0;
            uint32_t sum = 0;
            for (ssize_t i = 20; i < n + 20; i += 2)
                sum += (uint32_t)packet[i] << 8 | (i + 1 < n + 20 ? packet[i + 1] : 0);
            while (sum >> 16)
                sum = (sum & 65535) + (sum >> 16);
            sum = ~sum;
            packet[22] = (unsigned char)(sum >> 8);
            packet[23] = (unsigned char)sum;
        }
        n += 20;
    }
    else
    {
        n = recvfrom(fd, packet, capacity, flags, (struct sockaddr *)&peer, &peer_size);
        if (n >= 20 && (packet[0] & 15u) * 4 <= (unsigned)n &&
            network16(packet + 2) >= (packet[0] & 15u) * 4)
        {
            uint16_t length = network16(packet + 2) - (packet[0] & 15u) * 4;
            memcpy(packet + 2, &length, 2);
            uint16_t fragment = network16(packet + 6);
            memcpy(packet + 6, &fragment, 2);
        }
    }
    if (n >= 0 && address)
        *address = (LxHostSocketAddress){peer.sin_addr.s_addr, 0};
    return n;
}
