/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCPIP32 resolver and network databases: host, protocol and service lookups,
   the Toolkit _res state and the resolver error (tcp_h_errno). */
#include "os2/include/private/export.h"
#include "os2/include/sys/socket.h"
#include "os2/include/sys/select.h"
#include "os2/include/sys/time.h"
#include "os2/include/sys/uio.h"
#include "os2/include/sys/ioctl.h"
#include "os2/include/sys/sysctl.h"
#include "os2/include/sys/itypes.h"
#include "os2/include/netinet/in.h"
#include "os2/include/arpa/inet.h"
#include "os2/include/netdb.h"
#include "os2/include/resolv.h"
#include "os2/include/unistd.h"

#include "host/socket.h"
#include "host/network.h"
#include "host/sync.h"
#include "os2/mptn/common/resolver.h"
#include "os2/mptn/common/network.h"
#include "os2/mptn/tcpip32/internal.h"

static LX_HOST_THREAD_LOCAL int ResolverError;

/* Toolkit resolver state is two packed 512-byte records containing guest pointers. */
static LxMmaps ResolverObject;
static LxHostMutex *ResolverMutex;

unsigned long OS2_inet_addr(const char *name)
{
    TRACE_NATIVE("inet_addr('%s')", name);
    return lx_host_socket_parse_address(name);
}

struct OS2_hostent *OS2_gethostbyname(const char *name)
{
    static LX_HOST_THREAD_LOCAL OS2_hostent output;
    LxHostSocketHost host;
    if (!lx_mptn_host_by_name(name, &host, &ResolverError))
        return NULL;
    output.h_name = host.name;
    output.h_aliases = host.aliases;
    output.h_addrtype = host.family;
    output.h_length = host.length;
    output.h_addr_list = host.addresses;
    return &output;
}

char *OS2_inet_ntoa(struct OS2_in_addr address)
{
    return lx_host_socket_format_address(address.s_addr);
}

struct OS2_servent *OS2_getservbyname(const char *name, const char *protocol)
{
    static LX_HOST_THREAD_LOCAL OS2_servent output;
    LxHostSocketService service;
    if (!lx_host_socket_service(name, protocol, &service))
        return NULL;
    output.s_name = service.name;
    output.s_aliases = service.aliases;
    output.s_port = service.port;
    output.s_proto = service.protocol;
    return &output;
}

OS2_hostent *OS2_Rgethostbyname(const char *name)
{

    TRACE_NATIVE("Rgethostbyname('%s')", name);
    return OS2_gethostbyname(name);
}

OS2_hostent *OS2_gethostbyaddr(const void *address, int length, int family)
{
    static LX_HOST_THREAD_LOCAL OS2_hostent output;
    LxHostSocketHost host;
    if (!address || length != sizeof(OS2_in_addr) || family != OS2_AF_INET)
        errno = EINVAL;
    if (!lx_mptn_host_by_address(address, length, family, &host, &ResolverError))
        return NULL;
    output.h_name = host.name;
    output.h_aliases = host.aliases;
    output.h_addrtype = host.family;
    output.h_length = host.length;
    output.h_addr_list = host.addresses;
    return &output;
}

static OS2_protoent *protocol_result(const char *name, int number)
{
    static LX_HOST_THREAD_LOCAL OS2_protoent output;
    if (!lx_host_socket_protocol(name, number, &output.p_name, &output.p_aliases, &output.p_proto))
        return NULL;
    return &output;
}

OS2_protoent *OS2_getprotobyname(const char *name)
{
    return name ? protocol_result(name, 0) : NULL;
}

OS2_protoent *OS2_getprotobynumber(int number)
{
    return protocol_result(NULL, number);
}

OS2_servent *OS2_getservbyport(int port, const char *protocol)
{
    static LX_HOST_THREAD_LOCAL OS2_servent output;
    LxHostSocketService host;
    if (!lx_host_socket_service_port(port, protocol, &host))
        return NULL;
    output.s_name = host.name;
    output.s_aliases = host.aliases;
    output.s_port = host.port;
    output.s_proto = host.protocol;
    return &output;
}

void OS2_setprotoent(int stayopen)
{
    (void)stayopen;
}

void OS2_endprotoent(void) {}

void OS2_setservent(int stayopen)
{
    (void)stayopen;
}

void OS2_endservent(void) {}

int OS2_gethostname(char *name, int length)
{
    if (!name || length <= 0)
    {
        errno = EFAULT;
        return -1;
    }
    return lx_host_socket_hostname(name, (size_t)length);
}

uint32_t OS2_gethostid(void)
{
    return lx_os2_network_host_id();
}

/* _ress is two 512-byte __res_state records (Toolkit resolv.h, 4-byte packing);
   _res is the first, and res_init copies it to the second. */
enum
{
    RES_STATE_SIZE = 512,
    RES_STATE_COUNT = 2,
    RES_OBJECT_SIZE = 4096,
    RES_RETRANS_OFFSET = 0,
    RES_RETRY_OFFSET = 4,
    RES_OPTIONS_OFFSET = 8,
    RES_NSCOUNT_OFFSET = 12,
    RES_NSADDR_LIST_OFFSET = 16,
    RES_DEFDNAME_OFFSET = 66,
    RES_DNSRCH_OFFSET = 324,
    RES_NDOTS_OFFSET = 356,
    RES_DEFDNAME_SIZE = 256,
    /* Search names are kept in the object after both records. */
    RES_STRINGS_OFFSET = 1024,
    /* Retransmit after five seconds, twice. */
    RES_RETRANS_SECONDS = 5,
    RES_RETRY_COUNT = 2,
    RES_NDOTS = 1,
    NAMESERVER_PORT = 53
};

int lx_tcpip_resolver_initialize(void)
{
    if (ResolverObject.mapping)
        return 1;
    ResolverObject.mapping = lx_execution_map(GProcessState.execution, 0, RES_OBJECT_SIZE,
                                              LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!ResolverObject.mapping)
        return 0;
    ResolverObject.addr = ResolverObject.mapped = lx_execution_mapping_host(ResolverObject.mapping);
    ResolverObject.guest_base = lx_execution_mapping_address(ResolverObject.mapping);
    ResolverObject.size = RES_OBJECT_SIZE;
    ResolverObject.bits = 32;
    /* No 16-bit alias selector. */
    ResolverObject.alias = UINT16_MAX;
    ResolverMutex = lx_host_mutex_create(0, 0);
    if (!ResolverMutex)
    {
        lx_execution_unmap(GProcessState.execution, ResolverObject.mapping);
        memset(&ResolverObject, 0, sizeof ResolverObject);
    }
    return ResolverMutex != NULL;
}

int OS2_res_init(void)
{
    if (!lx_tcpip_resolver_initialize())
        return -1;
    LxHostNetwork network;
    if (lx_host_network_snapshot(&network))
        return -1;
    lx_host_mutex_lock(ResolverMutex, UINT32_MAX);
    uint8 *data = ResolverObject.addr;
    memset(data, 0, RES_OBJECT_SIZE);
    lx_guest_store_u32(data + RES_RETRANS_OFFSET, RES_RETRANS_SECONDS);
    lx_guest_store_u32(data + RES_RETRY_OFFSET, RES_RETRY_COUNT);
    lx_guest_store_u32(data + RES_OPTIONS_OFFSET,
                       OS2_RES_INIT | OS2_RES_RECURSE | OS2_RES_DEFNAMES | OS2_RES_DNSRCH);
    unsigned count = 0;
    for (size_t i = 0; i < network.dns.server_count && count < OS2_MAXNS; i++)
    {
        uint32_t ip = lx_host_socket_parse_address(network.dns.servers[i]);
        if (ip == UINT32_MAX)
            continue;
        OS2_sockaddr_in address = {0};
        address.sin_len = sizeof address;
        address.sin_family = OS2_AF_INET;
        address.sin_port = lx_host_socket_network16(NAMESERVER_PORT);
        address.sin_addr.s_addr = ip;
        memcpy(data + RES_NSADDR_LIST_OFFSET + count++ * sizeof address, &address, sizeof address);
    }
    lx_guest_store_u32(data + RES_NSCOUNT_OFFSET, count);
    lx_guest_store_u32(data + RES_NDOTS_OFFSET, RES_NDOTS);
    unsigned offset = RES_STRINGS_OFFSET;
    for (size_t i = 0; i < network.dns.search_count && i < OS2_MAXDNSRCH; i++)
    {
        size_t n = strlen(network.dns.search[i]) + 1;
        if (n > RES_DEFDNAME_SIZE || offset + n > RES_OBJECT_SIZE)
            continue;
        memcpy(data + offset, network.dns.search[i], n);
        lx_guest_store_u32(data + RES_DNSRCH_OFFSET + i * sizeof(uint32_t),
                           ResolverObject.guest_base + offset);
        /* The first search domain is also the default domain. */
        if (!i)
            memcpy(data + RES_DEFDNAME_OFFSET, network.dns.search[i], n);
        offset += (unsigned)n;
    }
    memcpy(data + RES_STATE_SIZE, data, RES_STATE_SIZE);
    lx_host_mutex_unlock(ResolverMutex);
    lx_host_network_destroy(&network);
    return 0;
}

uint32_t OS2_inet_network(const char *name)
{
    return name ? lx_host_socket_parse_network(name) : UINT32_MAX;
}

int *OS2_tcp_h_errno1(void)
{
    return &ResolverError;
}

uint32_t lx_tcpip_resolver_address(void)
{
    return ResolverObject.guest_base;
}

void lx_tcpip_resolver_terminate(void)
{
    if (ResolverObject.mapping)
        lx_execution_unmap(GProcessState.execution, ResolverObject.mapping);
    memset(&ResolverObject, 0, sizeof ResolverObject);
    if (ResolverMutex)
        lx_host_mutex_destroy(ResolverMutex);
    ResolverMutex = NULL;
}
