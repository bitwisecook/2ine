/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Sockets. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_SYS_SOCKET_H_
#define _INCL_SYS_SOCKET_H_

#include "os2/include/os2def.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Sockets */

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

#define OS2_AF_LINK 18

#define OS2_AF_ROUTE 39

#define OS2_NET_RT_DUMP 1

#define OS2_NET_RT_FLAGS 2

#define OS2_NET_RT_IFLIST 3

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

#pragma pack(pop)

    OS2EXPORT int OS2API OS2_sock_init(void);

    OS2EXPORT int OS2API OS2_getpeername(int sock, OS2_sockaddr *address, int *length);

    OS2EXPORT int OS2API OS2_getsockopt(int sock, int level, int option, void *value, int *length);

    OS2EXPORT int OS2API OS2_recvfrom(int sock, void *buffer, int length, int flags,
                                      OS2_sockaddr *address, int *address_length);

    OS2EXPORT int OS2API OS2_sendto(int sock, const void *buffer, int length, int flags,
                                    const OS2_sockaddr *address, int address_length);

    OS2EXPORT int OS2API OS2_recv(int sock, void *buf, size_t len, int os2flags);

    OS2EXPORT int OS2API OS2_connect(int sock, const OS2_sockaddr *os2addr, int addrlen);

    OS2EXPORT int OS2API OS2_shutdown(int sock, int kind);

    OS2EXPORT int OS2API OS2_socket(int family, int os2socktype, int protocol);

    OS2EXPORT ssize_t OS2API OS2_send(int sock, const void *buf, size_t len, int os2flags);

    OS2EXPORT int OS2API OS2_sock_errno(void);

    OS2EXPORT void OS2API OS2_psock_errno(const char *message);

    OS2EXPORT int OS2API OS2_getsockname(int sock, OS2_sockaddr *os2name, int *namelen);

    OS2EXPORT int OS2API OS2_setsockopt(int sock, int os2level, int os2name, const void *value,
                                        int len);

    OS2EXPORT int OS2API OS2_bind(int sock, const OS2_sockaddr *os2name, int os2namelen);

    OS2EXPORT int OS2API OS2_accept(int sock, OS2_sockaddr *os2name, int *os2namelen);

    OS2EXPORT int OS2API OS2_Raccept(int sock, OS2_sockaddr *os2name, int *os2namelen);

    OS2EXPORT int OS2API OS2_Rbind(int sock, OS2_sockaddr *os2name, int os2namelen,
                                   OS2_sockaddr *os2remote);

    OS2EXPORT int OS2API OS2_Rconnect(int sock, const OS2_sockaddr *os2name, int os2namelen);

    OS2EXPORT int OS2API OS2_Rgetsockname(int sock, OS2_sockaddr *os2name, int *namelen);

    OS2EXPORT int OS2API OS2_Rlisten(int sock, int backlog);

#ifdef __cplusplus
}
#endif

#endif
