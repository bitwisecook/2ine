/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "hal/posix/network_internal.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/sysctl.h>
#include <SystemConfiguration/SystemConfiguration.h>
int lx_network_mac(const struct ifaddrs *link, LxHostNetworkInterface *entry)
{
    if (link->ifa_addr->sa_family != AF_LINK)
        return 0;
    const struct sockaddr_dl *dl = (const struct sockaddr_dl *)link->ifa_addr;
    if (dl->sdl_alen != 6)
        return 0;
    memcpy(entry->mac, LLADDR(dl), 6);
    return 1;
}

int lx_network_routes(LxHostNetwork *out)
{
    int mib[] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0};
    size_t size = 0;
    if (sysctl(mib, 6, NULL, &size, NULL, 0))
        return -1;
    char *buffer = malloc(size ? size : 1);
    if (!buffer)
        return -1;
    if (sysctl(mib, 6, buffer, &size, NULL, 0))
    {
        free(buffer);
        return -1;
    }
    int result = 0;
    for (size_t offset = 0; offset < size;)
    {
        if (size - offset < sizeof(struct rt_msghdr))
            break;
        const struct rt_msghdr *h = (const struct rt_msghdr *)(buffer + offset);
        if (h->rtm_msglen < sizeof *h || h->rtm_msglen > size - offset)
            break;
        LxHostNetworkRoute entry = {0};
        entry.interface_index = h->rtm_index;
        entry.flags = h->rtm_flags & (RTF_UP | RTF_GATEWAY | RTF_HOST);
        if (h->rtm_flags & RTF_HOST)
            entry.netmask = UINT32_MAX;
        const char *a = (const char *)(h + 1), *end = (const char *)h + h->rtm_msglen;
        int has_destination = 0;
        for (int i = 0; i < RTAX_MAX && a + 2 <= end; i++)
        {
            if (!(h->rtm_addrs & (1 << i)))
                continue;
            const struct sockaddr *sa = (const struct sockaddr *)a;
            size_t n = sa->sa_len ? (sa->sa_len + 3u) & ~3u : 4;
            if (a + n > end)
                break;
            if (i == RTAX_DST && sa->sa_family == AF_INET && sa->sa_len >= 8)
            {
                entry.destination = lx_network_ipv4(sa);
                has_destination = 1;
            }
            if (i == RTAX_GATEWAY && sa->sa_family == AF_INET && sa->sa_len >= 8)
                entry.gateway = lx_network_ipv4(sa);
            /* BSD masks can have family zero and omit trailing zero bytes. */
            if (i == RTAX_NETMASK && sa->sa_len > 4)
                memcpy(&entry.netmask, a + 4, sa->sa_len - 4 < 4 ? sa->sa_len - 4 : 4);
            a += n;
        }
        if (has_destination &&
            lx_network_append((void **)&out->routes, &out->route_count, sizeof entry, &entry))
        {
            result = -1;
            break;
        }
        offset += h->rtm_msglen;
    }
    free(buffer);
    return result;
}

int lx_network_dns(LxHostNetworkDns *out)
{
    CFDictionaryRef config = SCDynamicStoreCopyValue(NULL, CFSTR("State:/Network/Global/DNS"));
    if (!config)
        return 0;
    const CFStringRef keys[] = {CFSTR("ServerAddresses"), CFSTR("SearchDomains")};
    int result = 0;
    for (unsigned k = 0; k < 2 && !result; k++)
    {
        CFArrayRef values = CFDictionaryGetValue(config, keys[k]);
        if (!values || CFGetTypeID(values) != CFArrayGetTypeID())
            continue;
        for (CFIndex i = 0; i < CFArrayGetCount(values); i++)
        {
            CFStringRef s = CFArrayGetValueAtIndex(values, i);
            char text[1024];
            if (CFGetTypeID(s) == CFStringGetTypeID() &&
                CFStringGetCString(s, text, sizeof text, kCFStringEncodingUTF8))
                result =
                    lx_network_string_append(k ? &out->search : &out->servers,
                                             k ? &out->search_count : &out->server_count, text);
            if (result)
                break;
        }
    }
    CFRelease(config);
    return result;
}
