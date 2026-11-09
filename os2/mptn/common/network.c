/* Public domain. No rights reserved. Wholly AI-authored. */
/* Layouts checked against OS/2 Toolkit 4.5 net/if.h, net/route.h and sys/ioctlos2.h. */
#include "os2/mptn/common/network.h"
#include "os2/include/sys/ioctl.h"
#include "os2/include/sys/sysctl.h"
#include "os2/include/netinet/in.h"
#include "os2/include/net/if.h"
#include "os2/include/net/if_dl.h"
#include "os2/include/net/route.h"
#include "host/network.h"
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

/* SNMP interface types (BSD if_types.h) reported in ifi_type, iftType and sdl_type. */
enum
{
    IFT_OTHER = 0x1,
    IFT_ETHER = 0x6,
    IFT_LOOP = 0x18,
    ETHER_ADDR_LEN = 6,
    ETHER_HDR_LEN = 14
};

/* The sysctl name: CTL_NET, AF_ROUTE, a protocol, the address family (0 or AF_INET),
   the NET_RT_* operation and its argument. */
enum
{
    ROUTE_NAME_LENGTH = 6,
    ROUTE_NAME_TOP = 0,
    ROUTE_NAME_FAMILY = 1,
    ROUTE_NAME_PROTOCOL = 2,
    ROUTE_NAME_ADDRESS_FAMILY = 3,
    ROUTE_NAME_OPERATION = 4,
    ROUTE_NAME_ARGUMENT = 5
};

/* Messages carry a sockaddr_dl padded to a full 32-byte slot. */
enum
{
    LINK_ADDRESS_SIZE = 32,
    IF_MSG_SIZE = sizeof(OS2_if_msghdr) + LINK_ADDRESS_SIZE,
    IFA_MSG_SIZE = sizeof(OS2_ifa_msghdr) + 3 * sizeof(OS2_sockaddr_in),
    RT_MSG_SIZE = sizeof(OS2_rt_msghdr) + 3 * sizeof(OS2_sockaddr_in) + LINK_ADDRESS_SIZE +
                  sizeof(OS2_sockaddr_in)
};

_Static_assert(sizeof(OS2_ifreq) == 32, "ifreq is a name and one sockaddr");
_Static_assert(sizeof(OS2_ortentry) == 64, "ortentry is 64 bytes");
_Static_assert(sizeof(OS2_iftable) == 111, "iftable is 111 bytes");
_Static_assert(IF_MSG_SIZE == 164 && IFA_MSG_SIZE == 66 && RT_MSG_SIZE == 208,
               "routing messages match the Toolkit sizes");

static uint8_t interface_type(const LxHostNetworkInterface *interface)
{
    return interface->mac_length                ? IFT_ETHER
           : interface->flags & LX_IFF_LOOPBACK ? IFT_LOOP
                                                : IFT_OTHER;
}

static void word16(void *p, uint16_t value)
{
    memcpy(p, &value, sizeof value);
}

static void word32(void *p, uint32_t value)
{
    memcpy(p, &value, sizeof value);
}

/* TCP/IP 4.1 and later write sin_len and an 8-bit family; 4.0 a 16-bit family. */
static void address(void *out, uint32_t ip, int bsd)
{
    unsigned char *p = out;
    memset(p, 0, sizeof(OS2_sockaddr_in));
    if (bsd)
    {
        p[offsetof(OS2_sockaddr_in, sin_len)] = sizeof(OS2_sockaddr_in);
        p[offsetof(OS2_sockaddr_in, sin_family)] = OS2_AF_INET;
    }
    else
        word16(p, OS2_AF_INET);
    memcpy(p + offsetof(OS2_sockaddr_in, sin_addr), &ip, sizeof ip);
}

int lx_os2_network_ifconf(void *buffer, size_t capacity, size_t *used, int bsd)
{
    if (!used || (!buffer && capacity))
    {
        errno = EFAULT;
        return -1;
    }
    LxHostNetwork s;
    if (lx_host_network_snapshot(&s))
        return -1;
    size_t count = s.interface_count;
    if (count > capacity / sizeof(OS2_ifreq))
        count = capacity / sizeof(OS2_ifreq);
    for (size_t i = 0; i < count; i++)
    {
        unsigned char *p = (unsigned char *)buffer + i * sizeof(OS2_ifreq);
        memcpy(p, s.interfaces[i].name, OS2_IFNAMSIZ);
        address(p + offsetof(OS2_ifreq, ifr_ifru), s.interfaces[i].address, bsd);
    }
    *used = count * sizeof(OS2_ifreq);
    lx_host_network_destroy(&s);
    return 0;
}

int lx_os2_network_ioctl(unsigned command, void *argument, size_t length, int bsd)
{
    const unsigned cmd = command & UINT16_MAX;
    /* TCP/IP 4.0's 16-bit stack has no SIOCGIFNETMASK. */
    int query = cmd == OS2_OSIOCGIFADDR || cmd == OS2_OSIOCGIFDSTADDR || cmd == OS2_SIOCGIFFLAGS ||
                cmd == OS2_OSIOCGIFBRDADDR || cmd == OS2_OSIOCGIFNETMASK ||
                cmd == OS2_SIOCGIFMETRIC || cmd == OS2_SIOCGIFADDR || cmd == OS2_SIOCGIFDSTADDR ||
                cmd == OS2_SIOCGIFBRDADDR || (bsd && cmd == OS2_SIOCGIFNETMASK) ||
                cmd == OS2_SIOCGIFMTU;
    if (!query && !(cmd == OS2_SIOSTATRT || cmd == OS2_SIOSTATIF || cmd == OS2_SIOSTATIF42 ||
                    cmd == OS2_SIOSTATCNTRT))
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    size_t required = query                     ? sizeof(OS2_ifreq)
                      : cmd == OS2_SIOSTATCNTRT ? sizeof(int32_t)
                      : cmd == OS2_SIOSTATRT    ? sizeof(OS2_rtentries)
                                                : sizeof(OS2_ifmib);
    if (!argument || length < required)
    {
        errno = EFAULT;
        return -1;
    }
    LxHostNetwork s;
    if (lx_host_network_snapshot(&s))
        return -1;
    unsigned char *p = argument;
    int result = 0;
    if (query)
    {
        const LxHostNetworkInterface *entry = NULL;
        for (size_t i = 0; i < s.interface_count; i++)
            if (!strncmp((const char *)p, s.interfaces[i].name, OS2_IFNAMSIZ))
            {
                entry = &s.interfaces[i];
                break;
            }
        if (!entry)
        {
            errno = ENXIO;
            result = -1;
        }
        else
        {
            unsigned char *value = p + offsetof(OS2_ifreq, ifr_ifru);
            memset(value, 0, sizeof(OS2_sockaddr));
            switch (cmd)
            {
                case OS2_OSIOCGIFADDR:
                case OS2_SIOCGIFADDR:
                    address(value, entry->address, bsd);
                    break;
                case OS2_OSIOCGIFDSTADDR:
                case OS2_SIOCGIFDSTADDR:
                    address(value, entry->destination, bsd);
                    break;
                case OS2_OSIOCGIFBRDADDR:
                case OS2_SIOCGIFBRDADDR:
                    address(value, entry->broadcast, bsd);
                    break;
                case OS2_OSIOCGIFNETMASK:
                case OS2_SIOCGIFNETMASK:
                    address(value, entry->netmask, bsd);
                    break;
                case OS2_SIOCGIFFLAGS:
                    if (bsd)
                        word32(value, entry->flags);
                    else
                        word16(value, (uint16_t)entry->flags);
                    break;
                case OS2_SIOCGIFMTU:
                    word32(value, entry->mtu);
                    break;
                case OS2_SIOCGIFMETRIC:
                    word32(value, 0);
                    break;
            }
        }
    }
    else if (cmd == OS2_SIOSTATCNTRT)
        word32(p, (uint32_t)s.route_count);
    else if (cmd == OS2_SIOSTATRT)
    {
        /* Host routes first, followed by network routes; no kernel pointers escape. */
        if (s.route_count > OS2_RTENTRY_COUNT)
        {
            errno = ENOBUFS;
            result = -1;
        }
        else
        {
            memset(p, 0, required);
            unsigned count = 0, hosts = 0;
            for (unsigned pass = 0; pass < 2; pass++)
                for (size_t i = 0; i < s.route_count; i++)
                {
                    const LxHostNetworkRoute *r = &s.routes[i];
                    if (!!(r->flags & OS2_RTF_HOST) != !pass)
                        continue;
                    unsigned char *row =
                        p + offsetof(OS2_rtentries, rttable) + count++ * sizeof(OS2_ortentry);
                    address(row + offsetof(OS2_ortentry, rt_dst), r->destination, bsd);
                    address(row + offsetof(OS2_ortentry, rt_gateway), r->gateway, bsd);
                    word16(row + offsetof(OS2_ortentry, rt_flags), (uint16_t)r->flags);
                    word32(row + offsetof(OS2_ortentry, metric1), r->metric);
                    if (!pass)
                        hosts++;
                }
            word16(p + offsetof(OS2_rtentries, hostcount), (uint16_t)hosts);
            word16(p + offsetof(OS2_rtentries, netcount), (uint16_t)(count - hosts));
        }
    }
    else
    {
        /* ifmib has one row per interface, not one per address alias. */
        memset(p, 0, required);
        unsigned count = 0;
        for (size_t i = 0; i < s.interface_count; i++)
        {
            const LxHostNetworkInterface *r = &s.interfaces[i];
            int seen = 0;
            for (size_t j = 0; j < i; j++)
                if (s.interfaces[j].index == r->index)
                    seen = 1;
            if (seen)
                continue;
            if (count == OS2_IFMIB_ENTRIES)
            {
                errno = ENOBUFS;
                result = -1;
                break;
            }
            unsigned char *row = p + offsetof(OS2_ifmib, iftable) + count++ * sizeof(OS2_iftable);
            word16(row + offsetof(OS2_iftable, iftIndex), (uint16_t)r->index);
            memcpy(row + offsetof(OS2_iftable, iftDescr), r->name, OS2_IFNAMSIZ);
            word16(row + offsetof(OS2_iftable, iftType), r->mac_length == ETHER_ADDR_LEN ? IFT_ETHER
                                                         : (r->flags & LX_IFF_LOOPBACK)
                                                             ? IFT_LOOP
                                                             : IFT_OTHER);
            word16(row + offsetof(OS2_iftable, iftMtu),
                   r->mtu > INT16_MAX ? INT16_MAX : (uint16_t)r->mtu);
            memcpy(row + offsetof(OS2_iftable, iftPhysAddr), r->mac, ETHER_ADDR_LEN);
            word16(row + offsetof(OS2_iftable, iftOperStatus),
                   (r->flags & LX_IFF_UP) ? OS2_OPERSTATUS_UP : OS2_OPERSTATUS_DOWN);
        }
        word16(p + offsetof(OS2_ifmib, ifNumber), (uint16_t)count);
    }
    int saved = errno;
    lx_host_network_destroy(&s);
    errno = saved;
    return result;
}

static void link_address(unsigned char *p, const LxHostNetworkInterface *interface)
{
    memset(p, 0, LINK_ADDRESS_SIZE);
    p[offsetof(OS2_sockaddr_dl, sdl_len)] = LINK_ADDRESS_SIZE;
    p[offsetof(OS2_sockaddr_dl, sdl_family)] = OS2_AF_LINK;
    if (!interface)
        return;
    word16(p + offsetof(OS2_sockaddr_dl, sdl_index), (uint16_t)interface->index);
    p[offsetof(OS2_sockaddr_dl, sdl_type)] = interface_type(interface);
    const unsigned char name_length = (unsigned char)strnlen(interface->name, OS2_IFNAMSIZ - 1);
    p[offsetof(OS2_sockaddr_dl, sdl_nlen)] = name_length;
    p[offsetof(OS2_sockaddr_dl, sdl_alen)] = interface->mac_length;
    /* sdl_data holds the name and then the link-level address. */
    memcpy(p + offsetof(OS2_sockaddr_dl, sdl_data), interface->name, name_length);
    memcpy(p + offsetof(OS2_sockaddr_dl, sdl_data) + name_length, interface->mac,
           interface->mac_length);
}

/* Toolkit packed route.h/if.h messages; no host kernel pointers or ABI structs. */
int lx_os2_network_sysctl(const int *name, unsigned count, void *buffer, size_t *length)
{
    if (!name || !length)
    {
        errno = EFAULT;
        return -1;
    }
    if (count != ROUTE_NAME_LENGTH || name[ROUTE_NAME_TOP] != OS2_CTL_NET ||
        name[ROUTE_NAME_FAMILY] != OS2_AF_ROUTE || name[ROUTE_NAME_PROTOCOL] ||
        (name[ROUTE_NAME_ADDRESS_FAMILY] && name[ROUTE_NAME_ADDRESS_FAMILY] != OS2_AF_INET) ||
        name[ROUTE_NAME_OPERATION] < OS2_NET_RT_DUMP ||
        name[ROUTE_NAME_OPERATION] > OS2_NET_RT_IFLIST)
    {
        errno = ENOPROTOOPT;
        return -1;
    }
    LxHostNetwork s;
    if (lx_host_network_snapshot(&s))
        return -1;
    size_t required = 0;
    const int operation = name[ROUTE_NAME_OPERATION];
    const unsigned argument = (unsigned)name[ROUTE_NAME_ARGUMENT];
    int interfaces = operation == OS2_NET_RT_IFLIST;
    for (size_t i = 0; i < (interfaces ? s.interface_count : s.route_count); i++)
    {
        if (interfaces)
        {
            if (argument && s.interfaces[i].index != argument)
                continue;
            int seen = 0;
            for (size_t j = 0; j < i; j++)
                if (s.interfaces[j].index == s.interfaces[i].index)
                    seen = 1;
            /* One RTM_IFINFO per interface, then one RTM_NEWADDR per address. */
            required += IFA_MSG_SIZE + (seen ? 0 : IF_MSG_SIZE);
        }
        else if (operation != OS2_NET_RT_FLAGS || (s.routes[i].flags & argument))
            required += RT_MSG_SIZE;
    }
    size_t capacity = *length;
    *length = required;
    if (!buffer || capacity < required)
    {
        lx_host_network_destroy(&s);
        if (buffer)
        {
            errno = ENOMEM;
            return -1;
        }
        return 0;
    }
    unsigned char *p = buffer;
    for (size_t i = 0; i < (interfaces ? s.interface_count : s.route_count); i++)
    {
        if (interfaces)
        {
            const LxHostNetworkInterface *r = &s.interfaces[i];
            if (argument && r->index != argument)
                continue;
            int seen = 0;
            for (size_t j = 0; j < i; j++)
                if (s.interfaces[j].index == r->index)
                    seen = 1;
            if (!seen)
            {
                const size_t data = offsetof(OS2_if_msghdr, ifm_data);
                memset(p, 0, IF_MSG_SIZE);
                word16(p + offsetof(OS2_if_msghdr, ifm_msglen), IF_MSG_SIZE);
                p[offsetof(OS2_if_msghdr, ifm_version)] = OS2_RTM_VERSION;
                p[offsetof(OS2_if_msghdr, ifm_type)] = OS2_RTM_IFINFO;
                word32(p + offsetof(OS2_if_msghdr, ifm_addrs), OS2_RTA_IFP);
                word32(p + offsetof(OS2_if_msghdr, ifm_flags), r->flags);
                word16(p + offsetof(OS2_if_msghdr, ifm_index), (uint16_t)r->index);
                p[data + offsetof(OS2_if_data, ifi_type)] = interface_type(r);
                p[data + offsetof(OS2_if_data, ifi_addrlen)] = r->mac_length;
                p[data + offsetof(OS2_if_data, ifi_hdrlen)] = r->mac_length ? ETHER_HDR_LEN : 0;
                word32(p + data + offsetof(OS2_if_data, ifi_mtu), r->mtu);
                word16(p + data + offsetof(OS2_if_data, ifi_OperStatus),
                       r->flags & LX_IFF_UP ? OS2_OPERSTATUS_UP : OS2_OPERSTATUS_DOWN);
                memcpy(p + data + offsetof(OS2_if_data, ifi_descr), r->name, OS2_IFNAMSIZ);
                link_address(p + sizeof(OS2_if_msghdr), r);
                p += IF_MSG_SIZE;
            }
            unsigned char *addresses = p + sizeof(OS2_ifa_msghdr);
            memset(p, 0, IFA_MSG_SIZE);
            word16(p + offsetof(OS2_ifa_msghdr, ifam_msglen), IFA_MSG_SIZE);
            p[offsetof(OS2_ifa_msghdr, ifam_version)] = OS2_RTM_VERSION;
            p[offsetof(OS2_ifa_msghdr, ifam_type)] = OS2_RTM_NEWADDR;
            word32(p + offsetof(OS2_ifa_msghdr, ifam_addrs),
                   OS2_RTA_NETMASK | OS2_RTA_IFA | OS2_RTA_BRD);
            word32(p + offsetof(OS2_ifa_msghdr, ifam_flags), r->flags);
            word16(p + offsetof(OS2_ifa_msghdr, ifam_index), (uint16_t)r->index);
            address(addresses, r->netmask, 1);
            address(addresses + sizeof(OS2_sockaddr_in), r->address, 1);
            address(addresses + 2 * sizeof(OS2_sockaddr_in),
                    r->flags & LX_IFF_POINTOPOINT ? r->destination : r->broadcast, 1);
            p += IFA_MSG_SIZE;
        }
        else
        {
            const LxHostNetworkRoute *r = &s.routes[i];
            if (operation == OS2_NET_RT_FLAGS && !(r->flags & argument))
                continue;
            const LxHostNetworkInterface *interface = NULL;
            for (size_t j = 0; j < s.interface_count; j++)
                if (s.interfaces[j].index == r->interface_index)
                {
                    interface = &s.interfaces[j];
                    break;
                }
            const size_t metrics = offsetof(OS2_rt_msghdr, rtm_rmx);
            /* Addresses follow in RTA_* order: DST, GATEWAY, NETMASK, IFP, IFA. */
            unsigned char *addresses = p + sizeof(OS2_rt_msghdr);
            memset(p, 0, RT_MSG_SIZE);
            word16(p + offsetof(OS2_rt_msghdr, rtm_msglen), RT_MSG_SIZE);
            p[offsetof(OS2_rt_msghdr, rtm_version)] = OS2_RTM_VERSION;
            p[offsetof(OS2_rt_msghdr, rtm_type)] = OS2_RTM_GET;
            word16(p + offsetof(OS2_rt_msghdr, rtm_index), (uint16_t)r->interface_index);
            word32(p + offsetof(OS2_rt_msghdr, rtm_flags), r->flags);
            word32(p + offsetof(OS2_rt_msghdr, rtm_addrs),
                   OS2_RTA_DST | OS2_RTA_GATEWAY | OS2_RTA_NETMASK | OS2_RTA_IFP | OS2_RTA_IFA);
            word32(p + offsetof(OS2_rt_msghdr, rtm_netmask), r->netmask);
            word32(p + metrics + offsetof(OS2_rt_metrics, rmx_hopcount), r->metric);
            if (interface)
            {
                memcpy(p + offsetof(OS2_rt_msghdr, rtm_name), interface->name, OS2_IFNAMSIZ);
                word32(p + metrics + offsetof(OS2_rt_metrics, rmx_mtu), interface->mtu);
            }
            address(addresses, r->destination, 1);
            address(addresses + sizeof(OS2_sockaddr_in), r->gateway, 1);
            address(addresses + 2 * sizeof(OS2_sockaddr_in), r->netmask, 1);
            link_address(addresses + 3 * sizeof(OS2_sockaddr_in), interface);
            address(addresses + 3 * sizeof(OS2_sockaddr_in) + LINK_ADDRESS_SIZE,
                    interface ? interface->address : 0, 1);
            p += RT_MSG_SIZE;
        }
    }
    lx_host_network_destroy(&s);
    return 0;
}

/* The address of the first interface that is up and not loopback, or 0. */
uint32_t lx_os2_network_host_id(void)
{
    LxHostNetwork network;
    if (lx_host_network_snapshot(&network))
        return 0;
    uint32_t address = 0;
    for (size_t i = 0; i < network.interface_count; i++)
        if ((network.interfaces[i].flags & LX_IFF_UP) &&
            !(network.interfaces[i].flags & LX_IFF_LOOPBACK))
        {
            address = network.interfaces[i].address;
            break;
        }
    lx_host_network_destroy(&network);
    return address;
}
