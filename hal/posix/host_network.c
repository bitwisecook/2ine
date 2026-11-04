/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "network_internal.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int lx_host_network_append(void **items, size_t *count, size_t size, const void *item)
{
    if (*count >= SIZE_MAX / size - 1)
    {
        errno = ENOMEM;
        return -1;
    }
    void *p = realloc(*items, (*count + 1) * size);
    if (!p)
        return -1;
    *items = p;
    memcpy((char *)p + *count * size, item, size);
    ++*count;
    return 0;
}
static uint32_t ipv4(const struct sockaddr *a)
{
    return a && a->sa_family == AF_INET ? ((const struct sockaddr_in *)a)->sin_addr.s_addr : 0;
}
static int interfaces(LxHostNetwork *out)
{
    struct ifaddrs *list;
    if (getifaddrs(&list))
        return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0), result = 0;
    for (struct ifaddrs *p = list; p && !result; p = p->ifa_next)
    {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
            continue;
        LxHostNetworkInterface entry = {0};
        snprintf(entry.name, sizeof entry.name, "%s", p->ifa_name);
        entry.index = if_nametoindex(p->ifa_name);
        entry.address = ipv4(p->ifa_addr);
        entry.netmask = ipv4(p->ifa_netmask);
#define FLAG(host, guest)                                                                          \
    if (p->ifa_flags & host)                                                                       \
    entry.flags |= guest
        FLAG(IFF_UP, LX_IFF_UP);
        FLAG(IFF_RUNNING, LX_IFF_RUNNING);
        FLAG(IFF_LOOPBACK, LX_IFF_LOOPBACK);
        FLAG(IFF_BROADCAST, LX_IFF_BROADCAST);
        FLAG(IFF_POINTOPOINT, LX_IFF_POINTOPOINT);
        FLAG(IFF_MULTICAST, LX_IFF_MULTICAST);
#undef FLAG
        if (entry.flags & LX_IFF_BROADCAST)
            entry.broadcast = ipv4(p->ifa_broadaddr);
        if (entry.flags & LX_IFF_POINTOPOINT)
            entry.destination = ipv4(p->ifa_dstaddr);
        struct ifreq req = {0};
        snprintf(req.ifr_name, sizeof req.ifr_name, "%s", p->ifa_name);
        if (fd >= 0 && !ioctl(fd, SIOCGIFMTU, &req))
            entry.mtu = req.ifr_mtu;
        for (struct ifaddrs *link = list; link; link = link->ifa_next)
        {
            if (!link->ifa_addr || strcmp(link->ifa_name, p->ifa_name))
                continue;
            if (!lx_host_network_platform_mac(link->ifa_addr, entry.flags, entry.mac))
                continue;
            entry.mac_length = 6;
            entry.flags |= LX_IFF_ETHER;
            break;
        }
        result = lx_host_network_append((void **)&out->interfaces, &out->interface_count,
                                        sizeof entry, &entry);
    }
    int saved = errno;
    if (fd >= 0)
        close(fd);
    freeifaddrs(list);
    errno = saved;
    return result;
}
int lx_host_network_string_append(char ***list, size_t *count, const char *value)
{
    for (size_t i = 0; i < *count; i++)
        if (!strcmp((*list)[i], value))
            return 0;
    char *copy = strdup(value);
    if (!copy)
        return -1;
    if (lx_host_network_append((void **)list, count, sizeof copy, &copy))
    {
        free(copy);
        return -1;
    }
    return 0;
}
void lx_host_network_destroy(LxHostNetwork *s)
{
    if (!s)
        return;
    free(s->interfaces);
    free(s->routes);
    for (size_t i = 0; i < s->dns.server_count; i++)
        free(s->dns.servers[i]);
    for (size_t i = 0; i < s->dns.search_count; i++)
        free(s->dns.search[i]);
    free(s->dns.servers);
    free(s->dns.search);
    memset(s, 0, sizeof *s);
}
int lx_host_network_snapshot(LxHostNetwork *out)
{
    if (!out)
    {
        errno = EINVAL;
        return -1;
    }
    memset(out, 0, sizeof *out);
    if (interfaces(out) || lx_host_network_platform_routes(out) ||
        lx_host_network_platform_dns(&out->dns))
    {
        int saved = errno;
        lx_host_network_destroy(out);
        errno = saved;
        return -1;
    }
    return 0;
}
