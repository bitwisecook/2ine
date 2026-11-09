/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Internet addresses. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_NETINET_IN_H_
#define _INCL_NETINET_IN_H_

#include "os2/include/sys/socket.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Internet addresses */

#define OS2_IPPROTO_IP 0

#define OS2_IPPROTO_TCP 6

#define OS2_IPPROTO_UDP 17

#pragma pack(push, 1)

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

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
