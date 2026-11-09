/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host lookups behind gethostbyname/gethostbyaddr in TCPIP32 and TCP32DLL. */
#include "os2/mptn/common/resolver.h"
#include "os2/include/netdb.h"
#include "os2/include/sys/socket.h"
#include <string.h>

int lx_mptn_host_by_name(const char *name, LxHostSocketHost *host, int *h_error)
{
    int ok = lx_host_socket_host(name, host);
    *h_error = lx_host_socket_resolver_error();
    return ok;
}

int lx_mptn_host_by_address(const void *address, int length, int family, LxHostSocketHost *host,
                            int *h_error)
{
    uint32_t ip;
    if (!address || length != (int)sizeof ip || family != OS2_AF_INET)
    {
        *h_error = OS2_NO_RECOVERY;
        return 0;
    }
    memcpy(&ip, address, sizeof ip);
    int ok = lx_host_socket_reverse(ip, host);
    *h_error = lx_host_socket_resolver_error();
    return ok;
}
