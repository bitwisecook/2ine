/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DEFAULT_SOURCE 1
#include "../posix/network_internal.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netpacket/packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/time.h>

int lx_host_network_platform_mac(const struct sockaddr *address, uint32_t flags, uint8_t mac[6])
{
    if (address->sa_family != AF_PACKET || (flags & LX_IFF_LOOPBACK))
        return 0;
    const struct sockaddr_ll *dl = (const struct sockaddr_ll *)address;
    if (dl->sll_halen != 6)
        return 0;
    memcpy(mac, dl->sll_addr, 6);
    return 1;
}

int lx_host_network_platform_routes(LxHostNetwork *out)
{
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return -1;
    char *buffer = malloc(65536);
    if (!buffer)
    {
        close(fd);
        errno = ENOMEM;
        return -1;
    }
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
        struct iovec iov = {buffer, 65536};
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
            if (lx_host_network_append((void **)&out->routes, &out->route_count, sizeof entry,
                                       &entry))
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

int lx_host_network_platform_dns(LxHostNetworkDns *out)
{
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
            result =
                lx_host_network_string_append(server ? &out->servers : &out->search,
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
}
