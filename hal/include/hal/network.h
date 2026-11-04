/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_NETWORK_H
#define TWOINE_HOST_NETWORK_H
#include <stddef.h>
#include <stdint.h>

/* IPv4 addresses and masks are in network byte order. Flags use BSD/OS/2 values,
   never host ABI values. Each address (including aliases) has its own entry. */
typedef struct LxHostNetworkInterface
{
    char name[16];
    uint32_t index, address, netmask, broadcast, destination, flags, mtu;
    uint8_t mac[6], mac_length;
} LxHostNetworkInterface;
typedef struct LxHostNetworkRoute
{
    uint32_t destination, netmask, gateway, interface_index, metric, flags;
} LxHostNetworkRoute;
typedef struct LxHostNetworkDns
{
    /* Text preserves IPv6 nameservers, including scope identifiers. */
    char **servers;
    size_t server_count;
    char **search;
    size_t search_count;
} LxHostNetworkDns;
typedef struct LxHostNetwork
{
    LxHostNetworkInterface *interfaces;
    size_t interface_count;
    LxHostNetworkRoute *routes;
    size_t route_count;
    LxHostNetworkDns dns;
} LxHostNetwork;
enum
{
    LX_IFF_UP = 1,
    LX_IFF_BROADCAST = 2,
    LX_IFF_LOOPBACK = 8,
    LX_IFF_POINTOPOINT = 16,
    LX_IFF_RUNNING = 64,
    LX_IFF_MULTICAST = 0x800,
    LX_IFF_ETHER = 0x4000
};
/* Fresh read-only snapshot. On failure output is zeroed; errno describes failure.
   Destroy a successful snapshot before reusing its storage. */
int lx_host_network_snapshot(LxHostNetwork *output);
void lx_host_network_destroy(LxHostNetwork *snapshot);
#endif
