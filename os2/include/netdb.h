/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Host, service and protocol databases. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_NETDB_H_
#define _INCL_NETDB_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Host, service and protocol databases */

    /* h_errno values (tcp_h_errno) */
    enum
    {
        OS2_HOST_NOT_FOUND = 1,
        OS2_TRY_AGAIN = 2,
        OS2_NO_RECOVERY = 3,
        OS2_NO_DATA = 4
    };

#pragma pack(push, 1)

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

#pragma pack(pop)

    OS2EXPORT OS2_hostent *OS2API OS2_gethostbyaddr(const void *address, int length, int family);

    OS2EXPORT OS2_protoent *OS2API OS2_getprotobyname(const char *name);

    OS2EXPORT OS2_protoent *OS2API OS2_getprotobynumber(int number);

    OS2EXPORT OS2_servent *OS2API OS2_getservbyport(int port, const char *protocol);

    OS2EXPORT void OS2API OS2_setprotoent(int stayopen);

    OS2EXPORT void OS2API OS2_endprotoent(void);

    OS2EXPORT void OS2API OS2_setservent(int stayopen);

    OS2EXPORT void OS2API OS2_endservent(void);

    OS2EXPORT int *OS2API OS2_tcp_h_errno1(void);

    OS2EXPORT OS2_hostent *OS2API OS2_gethostbyname(const char *name);

    OS2EXPORT OS2_servent *OS2API OS2_getservbyname(const char *name, const char *proto);

    OS2EXPORT OS2_hostent *OS2API OS2_Rgethostbyname(const char *name);

#ifdef __cplusplus
}
#endif

#endif
