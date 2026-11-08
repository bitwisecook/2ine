/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_POSIX_NET_RAW_SOCKET_H
#define TWOINE_HOST_POSIX_NET_RAW_SOCKET_H
#include "host/socket.h"

enum
{
    ICMP_FULL = 1,
    ICMP_PAYLOAD,
    UDP_PACKET
};

int lx_diagnostic_icmp_open(int *kind);
int64_t lx_diagnostic_icmp_recv(int fd, int kind, unsigned char *packet, size_t capacity, int flags,
                                uint16_t echo_id, LxHostSocketAddress *address);
int lx_diagnostic_open(int protocol);
void lx_diagnostic_close(int fd);
int lx_diagnostic_option(int fd, int option, const void *value, int size, int *handled);
int64_t lx_diagnostic_send(int fd, const void *buffer, size_t size, int flags,
                           const LxHostSocketAddress *address, int *handled);
int64_t lx_diagnostic_recv(int fd, void *buffer, size_t size, int flags,
                           LxHostSocketAddress *address, int *handled);
int lx_diagnostic_type(int fd);
int lx_diagnostic_connect(int fd, const LxHostSocketAddress *address, int *handled);
int lx_diagnostic_peer(int fd, LxHostSocketAddress *address, int *handled);
#endif
