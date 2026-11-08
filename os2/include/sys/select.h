/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Socket readiness. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_SYS_SELECT_H_
#define _INCL_SYS_SELECT_H_

#include "os2/include/sys/time.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Socket readiness */

#pragma pack(push, 1)

#define OS2_FD_SETSIZE 64

    typedef struct OS2_fd_set
    {
        uint16_t fd_count;
        uint16_t padding;
        int fd_array[OS2_FD_SETSIZE];
    } OS2_fd_set;

#pragma pack(pop)

    OS2EXPORT int OS2API OS2_select(int sock, OS2_fd_set *readfds, OS2_fd_set *writefds,
                                    OS2_fd_set *errorfds, OS2_timeval *timeout);

#ifdef __cplusplus
}
#endif

#endif
