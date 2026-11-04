/* Public domain. No rights reserved. Wholly AI-authored. */
/* Layouts checked against OS/2 Toolkit 4.5 net/if.h, net/route.h and sys/ioctlos2.h. */
#include "network.h"
#include "hal/network.h"
#include <errno.h>
#include <string.h>
#include <limits.h>
static void word16(void *p, uint16_t value)
{
    memcpy(p, &value, 2);
}
static void word32(void *p, uint32_t value)
{
    memcpy(p, &value, 4);
}
static void address(void *out, uint32_t ip, int bsd)
{
    unsigned char *p = out;
    memset(p, 0, 16);
    if (bsd)
    {
        p[0] = 16;
        p[1] = 2;
    }
    else
        word16(p, 2);
    memcpy(p + 4, &ip, 4);
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
    if (count > capacity / 32)
        count = capacity / 32;
    for (size_t i = 0; i < count; i++)
    {
        unsigned char *p = (unsigned char *)buffer + i * 32;
        memcpy(p, s.interfaces[i].name, 16);
        address(p + 16, s.interfaces[i].address, bsd);
    }
    *used = count * 32;
    lx_host_network_destroy(&s);
    return 0;
}
int lx_os2_network_ioctl(unsigned command, void *argument, size_t length, int bsd)
{
    unsigned cmd = command & 0xffff;
    unsigned group = cmd >> 8, number = cmd & 255;
    int query = group == 'i' && (number == 13 || number == 15 || number == 17 || number == 18 ||
                                 number == 21 || number == 23 || number == 33 || number == 34 ||
                                 number == 35 || (bsd && number == 37) || number == 57);
    if (!query && !(group == 'n' && (number == 45 || number == 48 || number == 51 || number == 52)))
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    size_t required = query ? 32 : number == 52 ? 4 : number == 45 ? 4 + 512 * 64 : 2 + 42 * 111;
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
            if (!strncmp((const char *)p, s.interfaces[i].name, 16))
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
            memset(p + 16, 0, 16);
            switch (number)
            {
                case 13:
                case 33:
                    address(p + 16, entry->address, bsd);
                    break;
                case 15:
                case 34:
                    address(p + 16, entry->destination, bsd);
                    break;
                case 18:
                case 35:
                    address(p + 16, entry->broadcast, bsd);
                    break;
                case 21:
                case 37:
                    address(p + 16, entry->netmask, bsd);
                    break;
                case 17:
                    if (bsd)
                        word32(p + 16, entry->flags);
                    else
                        word16(p + 16, (uint16_t)entry->flags);
                    break;
                case 57:
                    word32(p + 16, entry->mtu);
                    break;
                case 23:
                    word32(p + 16, 0);
                    break;
            }
        }
    }
    else if (number == 52)
        word32(p, (uint32_t)s.route_count);
    else if (number == 45)
    {
        /* Host routes first, followed by network routes; no kernel pointers escape. */
        if (s.route_count > 512)
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
                    if (!!(r->flags & 4) != !pass)
                        continue;
                    unsigned char *row = p + 4 + count++ * 64;
                    address(row + 4, r->destination, bsd);
                    address(row + 20, r->gateway, bsd);
                    word16(row + 36, (uint16_t)r->flags);
                    word32(row + 48, r->metric);
                    if (!pass)
                        hosts++;
                }
            word16(p, (uint16_t)hosts);
            word16(p + 2, (uint16_t)(count - hosts));
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
            if (count == 42)
            {
                errno = ENOBUFS;
                result = -1;
                break;
            }
            unsigned char *row = p + 2 + count++ * 111;
            word16(row, (uint16_t)r->index);
            memcpy(row + 2, r->name, 16);
            word16(row + 47, r->mac_length == 6 ? 6 : (r->flags & LX_IFF_LOOPBACK) ? 24 : 1);
            word16(row + 49, r->mtu > INT16_MAX ? INT16_MAX : (uint16_t)r->mtu);
            memcpy(row + 51, r->mac, 6);
            word16(row + 57, (r->flags & LX_IFF_UP) ? 1 : 2);
        }
        word16(p, (uint16_t)count);
    }
    int saved = errno;
    lx_host_network_destroy(&s);
    errno = saved;
    return result;
}
