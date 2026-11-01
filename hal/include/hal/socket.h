/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_SOCKET_H
#define TWOINE_HOST_SOCKET_H
#include <stddef.h>
#include <stdint.h>
typedef struct LxHostSocketAddress
{
    uint32_t address;
    uint16_t port;
} LxHostSocketAddress;
typedef struct LxHostSocketSet
{
    int count;
    const int *sockets;
} LxHostSocketSet;
typedef struct LxHostSocketHost
{
    char *name;
    char **aliases;
    int family, length;
    char **addresses;
} LxHostSocketHost;
typedef struct LxHostSocketService
{
    char *name;
    char **aliases;
    int port;
    char *protocol;
} LxHostSocketService;
enum
{
    LX_SOCKET_STREAM = 1,
    LX_SOCKET_DGRAM = 2
};
enum
{
    LX_MSG_OOB = 1,
    LX_MSG_PEEK = 2,
    LX_MSG_DONTROUTE = 4,
    LX_MSG_EOR = 8,
    LX_MSG_TRUNC = 16,
    LX_MSG_CTRUNC = 32,
    LX_MSG_WAITALL = 64,
    LX_MSG_DONTWAIT = 128
};
typedef enum LxHostSocketOption
{
    LX_SO_INVALID,
    LX_SO_DEBUG,
    LX_SO_ACCEPTCONN,
    LX_SO_REUSEADDR,
    LX_SO_KEEPALIVE,
    LX_SO_DONTROUTE,
    LX_SO_BROADCAST,
    LX_SO_LINGER,
    LX_SO_OOBINLINE,
    LX_SO_REUSEPORT,
    LX_SO_SNDBUF,
    LX_SO_RCVBUF,
    LX_SO_SNDLOWAT,
    LX_SO_RCVLOWAT,
    LX_SO_SNDTIMEO,
    LX_SO_RCVTIMEO,
    LX_SO_ERROR,
    LX_SO_TYPE
} LxHostSocketOption;
int lx_host_socket_open(int type);
int lx_host_socket_close(int socket);
int lx_host_socket_connect(int socket, const LxHostSocketAddress *address);
int lx_host_socket_bind(int socket, const LxHostSocketAddress *address);
int lx_host_socket_listen(int socket, int backlog);
int lx_host_socket_accept(int socket, LxHostSocketAddress *address);
int lx_host_socket_name(int socket, LxHostSocketAddress *address);
int lx_host_socket_shutdown(int socket, int how);
int64_t lx_host_socket_send(int socket, const void *buffer, size_t size, unsigned flags);
int64_t lx_host_socket_recv(int socket, void *buffer, size_t size, unsigned flags);
int64_t lx_host_socket_sendto(int socket, const void *buffer, size_t size, unsigned flags,
                              const LxHostSocketAddress *address);
int64_t lx_host_socket_recvfrom(int socket, void *buffer, size_t size, unsigned flags,
                                LxHostSocketAddress *address);
int lx_host_socket_nonblocking(int socket, int enabled);
int lx_host_socket_available(int socket, int *bytes);
int lx_host_socket_hostname(char *name, size_t size);
/* Stable BSD socket error number, independent of host errno numbering. */
int lx_host_socket_bsd_error(void);
int lx_host_socket_error(void);
uint32_t lx_host_socket_parse_address(const char *name);
char *lx_host_socket_format_address(uint32_t address);
uint16_t lx_host_socket_network16(uint16_t value);
/* Query results borrow libc storage and are overwritten by subsequent queries. */
int lx_host_socket_host(const char *name, LxHostSocketHost *result);
int lx_host_socket_service(const char *name, const char *protocol, LxHostSocketService *result);
int lx_host_socket_select(int limit, const LxHostSocketSet *read, const LxHostSocketSet *write,
                          const LxHostSocketSet *error, int64_t seconds, int32_t microseconds,
                          int timed);
int lx_host_socket_poll(const int *sockets, int reads, int writes, int errors, int timeout_ms);
int lx_host_socket_option(int socket, LxHostSocketOption option, const void *value, int size);
int lx_host_wall_time(int64_t *seconds, int32_t *microseconds, int32_t *minutes_west, int32_t *dst);
#endif
