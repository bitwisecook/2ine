/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Socket handles and host identity. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_UNISTD_H_
#define _INCL_UNISTD_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Socket handles and host identity */

    OS2EXPORT int OS2API OS2_soclose(int sock);

    OS2EXPORT uint32_t OS2API OS2_gethostid(void);

    OS2EXPORT int OS2API OS2_gethostname(char *name, int length);

    OS2EXPORT int OS2API OS2_os2_select(int *socks, int noreads, int nowrites, int noexcept,
                                        long timeout);

#ifdef __cplusplus
}
#endif

#endif
