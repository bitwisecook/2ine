/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_TCPIP32_H_
#define _INCL_TCPIP32_H_

#include "os2types.h"
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

    OS2EXPORT int OS2API OS2_sock_init(void) OS2APIINFO(ord = 26, name = sock_init);
    OS2EXPORT int OS2API OS2_sysctl(const int *name, unsigned count, void *output, size_t *length,
                                    const void *input, size_t input_length)
        OS2APIINFO(ord = 207, name = sysctl);
    OS2EXPORT uint32_t OS2API OS2_gethostid(void) OS2APIINFO(ord = 4, name = gethostid);
    OS2EXPORT int OS2API OS2_getpeername(int sock, OS2_sockaddr *address, int *length)
        OS2APIINFO(ord = 5, name = getpeername);
    OS2EXPORT int OS2API OS2_getsockopt(int sock, int level, int option, void *value, int *length)
        OS2APIINFO(ord = 7, name = getsockopt);
    OS2EXPORT void OS2API OS2_psock_errno(const char *message)
        OS2APIINFO(ord = 30, name = psock_errno);
    OS2EXPORT int OS2API OS2_recvfrom(int sock, void *buffer, int length, int flags,
                                      OS2_sockaddr *address, int *address_length)
        OS2APIINFO(ord = 11, name = recvfrom);
    OS2EXPORT int OS2API OS2_sendto(int sock, const void *buffer, int length, int flags,
                                    const OS2_sockaddr *address, int address_length)
        OS2APIINFO(ord = 14, name = sendto);
    OS2EXPORT OS2_hostent *OS2API OS2_gethostbyaddr(const void *address, int length, int family)
        OS2APIINFO(ord = 112, name = gethostbyaddr);
    OS2EXPORT OS2_protoent *OS2API OS2_getprotobyname(const char *name)
        OS2APIINFO(ord = 121, name = getprotobyname);
    OS2EXPORT OS2_protoent *OS2API OS2_getprotobynumber(int number)
        OS2APIINFO(ord = 122, name = getprotobynumber);
    OS2EXPORT OS2_servent *OS2API OS2_getservbyport(int port, const char *protocol)
        OS2APIINFO(ord = 123, name = getservbyport);
    OS2EXPORT void OS2API OS2_setprotoent(int stayopen) OS2APIINFO(ord = 118, name = setprotoent);
    OS2EXPORT void OS2API OS2_endprotoent(void) OS2APIINFO(ord = 119, name = endprotoent);
    OS2EXPORT void OS2API OS2_setservent(int stayopen) OS2APIINFO(ord = 125, name = setservent);
    OS2EXPORT void OS2API OS2_endservent(void) OS2APIINFO(ord = 126, name = endservent);
    OS2EXPORT int OS2API OS2_res_init(void) OS2APIINFO(ord = 139, name = __res_init);
    OS2EXPORT uint32_t OS2API OS2_inet_network(const char *name)
        OS2APIINFO(ord = 109, name = inet_network);
    OS2EXPORT int *OS2API OS2_tcp_h_errno1(void) OS2APIINFO(ord = 151, name = tcp_h_errno1);
    OS2EXPORT int OS2API OS2_gethostname(char *name, int length)
        OS2APIINFO(ord = 144, name = gethostname);
    OS2EXPORT uint32_t OS2API OS2_htonl(uint32_t value) OS2APIINFO(ord = 203, name = htonl);
    OS2EXPORT uint32_t OS2API OS2_ntohl(uint32_t value) OS2APIINFO(ord = 204, name = ntohl);
    OS2EXPORT uint16_t OS2API OS2_ntohs(uint16_t value) OS2APIINFO(ord = 206, name = ntohs);
    OS2EXPORT int OS2API OS2_writev(int sock, const OS2_iovec *vectors, int count)
        OS2APIINFO(ord = 24, name = writev);
    OS2EXPORT int OS2API OS2_ioctl(int sock, uint32_t command, void *argument)
        OS2APIINFO(ord = 8, name = ioctl);
    OS2EXPORT int OS2API OS2_os2_ioctl(int sock, uint32_t command, void *argument, int length)
        OS2APIINFO(ord = 200, name = os2_ioctl);
    OS2EXPORT int OS2API OS2_recv(int sock, void *buf, size_t len, int os2flags)
        OS2APIINFO(ord = 10, name = recv);
    OS2EXPORT int OS2API OS2_connect(int sock, const OS2_sockaddr *os2addr, int addrlen)
        OS2APIINFO(ord = 3, name = connect);
    OS2EXPORT int OS2API OS2_shutdown(int sock, int kind) OS2APIINFO(ord = 25, name = shutdown);
    OS2EXPORT int OS2API OS2_socket(int family, int os2socktype, int protocol)
        OS2APIINFO(ord = 16, name = socket);
    OS2EXPORT ssize_t OS2API OS2_send(int sock, const void *buf, size_t len, int os2flags)
        OS2APIINFO(ord = 13, name = send);
    OS2EXPORT int OS2API OS2_soclose(int sock) OS2APIINFO(ord = 17, name = soclose);
    OS2EXPORT int OS2API OS2_sock_errno(void) OS2APIINFO(ord = 20, name = sock_errno);
    OS2EXPORT unsigned long OS2API OS2_inet_addr(const char *name)
        OS2APIINFO(ord = 105, name = inet_addr);
    OS2EXPORT OS2_hostent *OS2API OS2_gethostbyname(const char *name)
        OS2APIINFO(ord = 111, name = gethostbyname);
    OS2EXPORT unsigned short OS2API OS2_htons(unsigned short val)
        OS2APIINFO(ord = 205, name = htons);
    OS2EXPORT char *OS2API OS2_inet_ntoa(OS2_in_addr os2inaddr)
        OS2APIINFO(ord = 110, name = inet_ntoa);
    OS2EXPORT OS2_servent *OS2API OS2_getservbyname(const char *name, const char *proto)
        OS2APIINFO(ord = 124, name = getservbyname);
    OS2EXPORT int OS2API OS2_select(int sock, OS2_fd_set *readfds, OS2_fd_set *writefds,
                                    OS2_fd_set *errorfds, OS2_timeval *timeout)
        OS2APIINFO(ord = 32, name = select);
    OS2EXPORT int OS2API OS2_os2_select(int *socks, int noreads, int nowrites, int noexcept,
                                        long timeout) OS2APIINFO(ord = 12, name = os2_select);
    OS2EXPORT int OS2API OS2_getsockname(int sock, OS2_sockaddr *os2name, int *namelen)
        OS2APIINFO(ord = 6, name = getsockname);
    OS2EXPORT int OS2API OS2_setsockopt(int sock, int os2level, int os2name, const void *value,
                                        int len) OS2APIINFO(ord = 15, name = setsockopt);
    OS2EXPORT int OS2API OS2_bind(int sock, const OS2_sockaddr *os2name, int os2namelen)
        OS2APIINFO(ord = 2, name = bind);
    OS2EXPORT int OS2API OS2_accept(int sock, OS2_sockaddr *os2name, int *os2namelen)
        OS2APIINFO(ord = 1, name = accept);
    OS2EXPORT int OS2API OS2_Rgetsockname(int sock, OS2_sockaddr *os2name, int *namelen)
        OS2APIINFO(ord = 159, name = Rgetsockname);
    OS2EXPORT int OS2API OS2_Rbind(int sock, OS2_sockaddr *os2name, int os2namelen,
                                   OS2_sockaddr *os2remote) OS2APIINFO(ord = 157, name = Rbind);
    OS2EXPORT int OS2API OS2_Raccept(int sock, OS2_sockaddr *os2name, int *os2namelen)
        OS2APIINFO(ord = 156, name = Raccept);
    OS2EXPORT int OS2API OS2_Rconnect(int sock, const OS2_sockaddr *os2name, int os2namelen)
        OS2APIINFO(ord = 158, name = Rconnect);
    OS2EXPORT int OS2API OS2_Rlisten(int sock, int backlog) OS2APIINFO(ord = 160, name = Rlisten);
    OS2EXPORT OS2_hostent *OS2API OS2_Rgethostbyname(const char *name)
        OS2APIINFO(ord = 161, name = Rgethostbyname);
    OS2EXPORT int OS2API OS2_gettimeofday(OS2_timeval *os2tv, OS2_timezone *os2tz)
        OS2APIINFO(ord = 102, name = gettimeofday);

#ifdef __cplusplus
}
#endif

#endif
