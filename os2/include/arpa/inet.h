/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Address conversion. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_ARPA_INET_H_
#define _INCL_ARPA_INET_H_

#include "os2/include/netinet/in.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Address conversion */

    OS2EXPORT uint32_t OS2API OS2_inet_network(const char *name);

    OS2EXPORT unsigned long OS2API OS2_inet_addr(const char *name);

    OS2EXPORT char *OS2API OS2_inet_ntoa(OS2_in_addr os2inaddr);

#ifdef __cplusplus
}
#endif

#endif
