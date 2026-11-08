/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_TCPIP32_H_
#define _INCL_TCPIP32_H_

#include "os2libs/os2types.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* OS2_ prefixes avoid collisions with host BSD socket symbols; guest exports use OS/2 names. */

#define OS2_MSG_OOB 0x1
#define OS2_MSG_PEEK 0x2
#define OS2_MSG_DONTROUTE 0x4
#define OS2_MSG_FULLREAD 0x8
#define OS2_MSG_EOR 0x10
#define OS2_MSG_TRUNC 0x20
#define OS2_MSG_CTRUNC 0x40
#define OS2_MSG_WAITALL 0x80
#define OS2_MSG_DONTWAIT 0x100

#define OS2_AF_INET 2

#define OS2_SOCK_STREAM 1
#define OS2_SOCK_DGRAM 2
#define OS2_SOCK_RAW 3

#define OS2_SOL_SOCKET 0xffff

#define OS2_SO_DEBUG 0x0001
#define OS2_SO_ACCEPTCONN 0x0002
#define OS2_SO_REUSEADDR 0x0004
#define OS2_SO_KEEPALIVE 0x0008
#define OS2_SO_DONTROUTE 0x0010
#define OS2_SO_BROADCAST 0x0020
#define OS2_SO_USELOOPBACK 0x0040
#define OS2_SO_LINGER 0x0080
#define OS2_SO_OOBINLINE 0x0100
#define OS2_SO_L_BROADCAST 0x0200
#define OS2_SO_RCV_SHUTDOWN 0x0400
#define OS2_SO_SND_SHUTDOWN 0x0800
#define OS2_SO_REUSEPORT 0x1000
#define OS2_SO_TTCP 0x2000
#define OS2_SO_SNDBUF 0x1001
#define OS2_SO_RCVBUF 0x1002
#define OS2_SO_SNDLOWAT 0x1003
#define OS2_SO_RCVLOWAT 0x1004
#define OS2_SO_SNDTIMEO 0x1005
#define OS2_SO_RCVTIMEO 0x1006
#define OS2_SO_ERROR 0x1007
#define OS2_SO_TYPE 0x1008
#define OS2_SO_OPTIONS 0x1010

#pragma pack(push, 1)

    typedef struct OS2_sockaddr
    {
        uint8_t sa_len;
        uint8_t sa_family;
        char sa_data[14];
    } OS2_sockaddr;

    typedef struct OS2_in_addr
    {
        uint32_t s_addr;
    } OS2_in_addr;

    typedef struct OS2_sockaddr_in
    {
        uint8_t sin_len;
        uint8_t sin_family;
        uint16_t sin_port;
        struct OS2_in_addr sin_addr;
        char sin_zero[8];
    } OS2_sockaddr_in;

    typedef struct OS2_hostent
    {
        char *h_name;       /* official name of host */
        char **h_aliases;   /* alias list */
        int h_addrtype;     /* host address type */
        int h_length;       /* length of address */
        char **h_addr_list; /* list of addresses from name server */
    } OS2_hostent;

    typedef struct OS2_servent
    {
        char *s_name;
        char **s_aliases;
        int s_port;
        char *s_proto;
    } OS2_servent;

    typedef struct OS2_protoent
    {
        char *p_name;
        char **p_aliases;
        int p_proto;
    } OS2_protoent;

    typedef struct OS2_iovec
    {
        void *buffer;
        size_t length;
    } OS2_iovec;

#define OS2_FD_SETSIZE 64

    typedef struct OS2_fd_set
    {
        uint16_t fd_count;
        uint16_t padding;
        int fd_array[OS2_FD_SETSIZE];
    } OS2_fd_set;

    typedef struct OS2_timeval
    {
        int32_t tv_sec;
        int32_t tv_usec;
    } OS2_timeval;

    typedef struct OS2_timezone
    {
        int32_t tz_minuteswest;
        int32_t tz_dsttime;
    } OS2_timezone;

#pragma pack(pop)

    OS2EXPORT int OS2API OS2_sock_init(void);
    OS2EXPORT int OS2API OS2_sysctl(const int *name, unsigned count, void *output, size_t *length,
                                    const void *input, size_t input_length);
    OS2EXPORT uint32_t OS2API OS2_gethostid(void);
    OS2EXPORT int OS2API OS2_getpeername(int sock, OS2_sockaddr *address, int *length);
    OS2EXPORT int OS2API OS2_getsockopt(int sock, int level, int option, void *value, int *length);
    OS2EXPORT void OS2API OS2_psock_errno(const char *message);
    OS2EXPORT int OS2API OS2_recvfrom(int sock, void *buffer, int length, int flags,
                                      OS2_sockaddr *address, int *address_length);
    OS2EXPORT int OS2API OS2_sendto(int sock, const void *buffer, int length, int flags,
                                    const OS2_sockaddr *address, int address_length);
    OS2EXPORT OS2_hostent *OS2API OS2_gethostbyaddr(const void *address, int length, int family);
    OS2EXPORT OS2_protoent *OS2API OS2_getprotobyname(const char *name);
    OS2EXPORT OS2_protoent *OS2API OS2_getprotobynumber(int number);
    OS2EXPORT OS2_servent *OS2API OS2_getservbyport(int port, const char *protocol);
    OS2EXPORT void OS2API OS2_setprotoent(int stayopen);
    OS2EXPORT void OS2API OS2_endprotoent(void);
    OS2EXPORT void OS2API OS2_setservent(int stayopen);
    OS2EXPORT void OS2API OS2_endservent(void);
    OS2EXPORT int OS2API OS2_res_init(void);
    OS2EXPORT uint32_t OS2API OS2_inet_network(const char *name);
    OS2EXPORT int *OS2API OS2_tcp_h_errno1(void);
    OS2EXPORT int OS2API OS2_gethostname(char *name, int length);
    OS2EXPORT uint32_t OS2API OS2_htonl(uint32_t value);
    OS2EXPORT uint32_t OS2API OS2_ntohl(uint32_t value);
    OS2EXPORT uint16_t OS2API OS2_ntohs(uint16_t value);
    OS2EXPORT int OS2API OS2_writev(int sock, const OS2_iovec *vectors, int count);
    OS2EXPORT int OS2API OS2_ioctl(int sock, uint32_t command, void *argument);
    OS2EXPORT int OS2API OS2_os2_ioctl(int sock, uint32_t command, void *argument, int length);
    OS2EXPORT int OS2API OS2_recv(int sock, void *buf, size_t len, int os2flags);
    OS2EXPORT int OS2API OS2_connect(int sock, const OS2_sockaddr *os2addr, int addrlen);
    OS2EXPORT int OS2API OS2_shutdown(int sock, int kind);
    OS2EXPORT int OS2API OS2_socket(int family, int os2socktype, int protocol);
    OS2EXPORT ssize_t OS2API OS2_send(int sock, const void *buf, size_t len, int os2flags);
    OS2EXPORT int OS2API OS2_soclose(int sock);
    OS2EXPORT int OS2API OS2_sock_errno(void);
    OS2EXPORT unsigned long OS2API OS2_inet_addr(const char *name);
    OS2EXPORT OS2_hostent *OS2API OS2_gethostbyname(const char *name);
    OS2EXPORT unsigned short OS2API OS2_htons(unsigned short val);
    OS2EXPORT char *OS2API OS2_inet_ntoa(OS2_in_addr os2inaddr);
    OS2EXPORT OS2_servent *OS2API OS2_getservbyname(const char *name, const char *proto);
    OS2EXPORT int OS2API OS2_select(int sock, OS2_fd_set *readfds, OS2_fd_set *writefds,
                                    OS2_fd_set *errorfds, OS2_timeval *timeout);
    OS2EXPORT int OS2API OS2_os2_select(int *socks, int noreads, int nowrites, int noexcept,
                                        long timeout);
    OS2EXPORT int OS2API OS2_getsockname(int sock, OS2_sockaddr *os2name, int *namelen);
    OS2EXPORT int OS2API OS2_setsockopt(int sock, int os2level, int os2name, const void *value,
                                        int len);
    OS2EXPORT int OS2API OS2_bind(int sock, const OS2_sockaddr *os2name, int os2namelen);
    OS2EXPORT int OS2API OS2_accept(int sock, OS2_sockaddr *os2name, int *os2namelen);
    OS2EXPORT int OS2API OS2_Rgetsockname(int sock, OS2_sockaddr *os2name, int *namelen);
    OS2EXPORT int OS2API OS2_Rbind(int sock, OS2_sockaddr *os2name, int os2namelen,
                                   OS2_sockaddr *os2remote);
    OS2EXPORT int OS2API OS2_Raccept(int sock, OS2_sockaddr *os2name, int *os2namelen);
    OS2EXPORT int OS2API OS2_Rconnect(int sock, const OS2_sockaddr *os2name, int os2namelen);
    OS2EXPORT int OS2API OS2_Rlisten(int sock, int backlog);
    OS2EXPORT OS2_hostent *OS2API OS2_Rgethostbyname(const char *name);
    OS2EXPORT int OS2API OS2_gettimeofday(OS2_timeval *os2tv, OS2_timezone *os2tz);

#ifdef __cplusplus
}
#endif

#endif
