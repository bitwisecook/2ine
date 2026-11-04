/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "hal/network.h"
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
#ifdef __APPLE__
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/sysctl.h>
#include <SystemConfiguration/SystemConfiguration.h>
#else
#include <netpacket/packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/time.h>
#endif

static int append(void **items, size_t *count, size_t size, const void *item)
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
#ifdef __APPLE__
            if (link->ifa_addr->sa_family != AF_LINK)
                continue;
            const struct sockaddr_dl *dl = (const struct sockaddr_dl *)link->ifa_addr;
            if (dl->sdl_alen != 6)
                continue;
            memcpy(entry.mac, LLADDR(dl), 6);
#else
            if (link->ifa_addr->sa_family != AF_PACKET)
                continue;
            const struct sockaddr_ll *dl = (const struct sockaddr_ll *)link->ifa_addr;
            if (dl->sll_halen != 6 || (entry.flags & LX_IFF_LOOPBACK))
                continue;
            memcpy(entry.mac, dl->sll_addr, 6);
#endif
            entry.mac_length = 6;
            entry.flags |= LX_IFF_ETHER;
            break;
        }
        result = append((void **)&out->interfaces, &out->interface_count, sizeof entry, &entry);
    }
    int saved = errno;
    if (fd >= 0)
        close(fd);
    freeifaddrs(list);
    errno = saved;
    return result;
}
#ifdef __APPLE__
static int routes(LxHostNetwork *out)
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
                entry.destination = ipv4(sa);
                has_destination = 1;
            }
            if (i == RTAX_GATEWAY && sa->sa_family == AF_INET && sa->sa_len >= 8)
                entry.gateway = ipv4(sa);
            /* BSD masks can have family zero and omit trailing zero bytes. */
            if (i == RTAX_NETMASK && sa->sa_len > 4)
                memcpy(&entry.netmask, a + 4, sa->sa_len - 4 < 4 ? sa->sa_len - 4 : 4);
            a += n;
        }
        if (has_destination &&
            append((void **)&out->routes, &out->route_count, sizeof entry, &entry))
        {
            result = -1;
            break;
        }
        offset += h->rtm_msglen;
    }
    free(buffer);
    return result;
}
#else
static int routes(LxHostNetwork *out)
{
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return -1;
    struct timeval timeout = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    struct
    {
        struct nlmsghdr h;
        struct rtmsg r;
    } request = {0};
    request.h.nlmsg_len = sizeof request;
    request.h.nlmsg_type = RTM_GETROUTE;
    request.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.h.nlmsg_seq = 1;
    request.r.rtm_family = AF_INET;
    struct sockaddr_nl kernel = {0};
    kernel.nl_family = AF_NETLINK;
    int result = -1;
    if (sendto(fd, &request, sizeof request, 0, (struct sockaddr *)&kernel, sizeof kernel) < 0)
        goto done;
    for (;;)
    {
        char buffer[65536];
        struct iovec iov = {buffer, sizeof buffer};
        struct msghdr message = {0};
        message.msg_iov = &iov;
        message.msg_iovlen = 1;
        ssize_t n = recvmsg(fd, &message, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            goto done;
        }
        if (!n || (message.msg_flags & MSG_TRUNC))
        {
            errno = ENOBUFS;
            goto done;
        }
        for (struct nlmsghdr *h = (struct nlmsghdr *)buffer; NLMSG_OK(h, n); h = NLMSG_NEXT(h, n))
        {
            if (h->nlmsg_flags & NLM_F_DUMP_INTR)
            {
                errno = EAGAIN;
                goto done;
            }
            if (h->nlmsg_type == NLMSG_DONE)
            {
                result = 0;
                goto done;
            }
            if (h->nlmsg_type == NLMSG_ERROR)
            {
                errno = EIO;
                goto done;
            }
            if (h->nlmsg_type != RTM_NEWROUTE || h->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtmsg)))
                continue;
            const struct rtmsg *r = NLMSG_DATA(h);
            if (r->rtm_family != AF_INET || r->rtm_type != RTN_UNICAST || r->rtm_dst_len > 32)
                continue;
            LxHostNetworkRoute entry = {0};
            entry.netmask = htonl(r->rtm_dst_len ? UINT32_MAX << (32 - r->rtm_dst_len) : 0);
            entry.flags = 1 | (r->rtm_dst_len == 32 ? 4 : 0);
            int bytes = RTM_PAYLOAD(h);
            for (struct rtattr *a = RTM_RTA(r); RTA_OK(a, bytes); a = RTA_NEXT(a, bytes))
            {
                if (RTA_PAYLOAD(a) < 4)
                    continue;
                switch (a->rta_type)
                {
                    case RTA_DST:
                        memcpy(&entry.destination, RTA_DATA(a), 4);
                        break;
                    case RTA_GATEWAY:
                        memcpy(&entry.gateway, RTA_DATA(a), 4);
                        entry.flags |= 2;
                        break;
                    case RTA_OIF:
                        memcpy(&entry.interface_index, RTA_DATA(a), 4);
                        break;
                    case RTA_PRIORITY:
                        memcpy(&entry.metric, RTA_DATA(a), 4);
                        break;
                }
            }
            if (append((void **)&out->routes, &out->route_count, sizeof entry, &entry))
                goto done;
        }
    }
done:
    {
        int saved = errno;
        close(fd);
        errno = saved;
    }
    return result;
}
#endif
static int string_append(char ***list, size_t *count, const char *value)
{
    for (size_t i = 0; i < *count; i++)
        if (!strcmp((*list)[i], value))
            return 0;
    char *copy = strdup(value);
    if (!copy)
        return -1;
    if (append((void **)list, count, sizeof copy, &copy))
    {
        free(copy);
        return -1;
    }
    return 0;
}
static int dns(LxHostNetworkDns *out)
{
#ifdef __APPLE__
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
                result = string_append(k ? &out->search : &out->servers,
                                       k ? &out->search_count : &out->server_count, text);
            if (result)
                break;
        }
    }
    CFRelease(config);
    return result;
#else
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f)
        return errno == ENOENT ? 0 : -1;
    char *line = NULL;
    size_t size = 0;
    int result = 0;
    while (!result && getline(&line, &size, f) >= 0)
    {
        char *comment = strpbrk(line, "#;");
        if (comment)
            *comment = 0;
        char *state, *key = strtok_r(line, " \t\r\n", &state);
        if (!key)
            continue;
        int server = !strcmp(key, "nameserver");
        if (!server && strcmp(key, "search") && strcmp(key, "domain"))
            continue;
        for (char *s = strtok_r(NULL, " \t\r\n", &state); s; s = strtok_r(NULL, " \t\r\n", &state))
        {
            result = string_append(server ? &out->servers : &out->search,
                                   server ? &out->server_count : &out->search_count, s);
            if (result || server)
                break;
        }
    }
    if (ferror(f))
    {
        errno = EIO;
        result = -1;
    }
    free(line);
    fclose(f);
    return result;
#endif
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
    if (interfaces(out) || routes(out) || dns(&out->dns))
    {
        int saved = errno;
        lx_host_network_destroy(out);
        errno = saved;
        return -1;
    }
    return 0;
}
