/* Public domain. No rights reserved. Wholly AI-authored. */
/* Layouts checked against OS/2 Toolkit 4.5 net/if.h, net/route.h and sys/ioctlos2.h. */
#include "os2libs/network.h"
#include "hal/network.h"
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

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

static void link_address(unsigned char *p, const LxHostNetworkInterface *interface)
{
    memset(p, 0, 32);
    p[0] = 32;
    p[1] = 18; /* OS/2 AF_LINK */
    if (!interface)
        return;
    word16(p + 2, (uint16_t)interface->index);
    p[4] = interface->mac_length ? 6 : interface->flags & LX_IFF_LOOPBACK ? 24 : 1;
    p[5] = (unsigned char)strnlen(interface->name, 15);
    p[6] = interface->mac_length;
    memcpy(p + 8, interface->name, p[5]);
    memcpy(p + 8 + p[5], interface->mac, interface->mac_length);
}

/* Toolkit packed route.h/if.h messages; no host kernel pointers or ABI structs. */
int lx_os2_network_sysctl(const int *name, unsigned count, void *buffer, size_t *length)
{
    if (!name || !length)
    {
        errno = EFAULT;
        return -1;
    }
    if (count != 6 || name[0] != 4 || name[1] != 39 || name[2] || (name[3] && name[3] != 2) ||
        name[4] < 1 || name[4] > 3)
    {
        errno = ENOPROTOOPT;
        return -1;
    }
    LxHostNetwork s;
    if (lx_host_network_snapshot(&s))
        return -1;
    size_t required = 0;
    int interfaces = name[4] == 3;
    for (size_t i = 0; i < (interfaces ? s.interface_count : s.route_count); i++)
    {
        if (interfaces)
        {
            if (name[5] && s.interfaces[i].index != (unsigned)name[5])
                continue;
            int seen = 0;
            for (size_t j = 0; j < i; j++)
                if (s.interfaces[j].index == s.interfaces[i].index)
                    seen = 1;
            required += 66 + (seen ? 0 : 164);
        }
        else if (name[4] != 2 || (s.routes[i].flags & (unsigned)name[5]))
            required += 208;
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
            if (name[5] && r->index != (unsigned)name[5])
                continue;
            int seen = 0;
            for (size_t j = 0; j < i; j++)
                if (s.interfaces[j].index == r->index)
                    seen = 1;
            if (!seen)
            {
                memset(p, 0, 164);
                word16(p, 164);
                p[2] = 4;
                p[3] = 14;
                word32(p + 4, 0x10);
                word32(p + 8, r->flags);
                word16(p + 12, (uint16_t)r->index);
                p[14] = r->mac_length ? 6 : r->flags & LX_IFF_LOOPBACK ? 24 : 1;
                p[15] = r->mac_length;
                p[16] = r->mac_length ? 14 : 0;
                word32(p + 17, r->mtu);
                word16(p + 77, r->flags & LX_IFF_UP ? 1 : 2);
                memcpy(p + 87, r->name, 16);
                link_address(p + 132, r);
                p += 164;
            }
            memset(p, 0, 66);
            word16(p, 66);
            p[2] = 4;
            p[3] = 12;
            word32(p + 4, 0xa4);
            word32(p + 8, r->flags);
            word16(p + 12, (uint16_t)r->index);
            address(p + 18, r->netmask, 1);
            address(p + 34, r->address, 1);
            address(p + 50, r->flags & LX_IFF_POINTOPOINT ? r->destination : r->broadcast, 1);
            p += 66;
        }
        else
        {
            const LxHostNetworkRoute *r = &s.routes[i];
            if (name[4] == 2 && !(r->flags & (unsigned)name[5]))
                continue;
            const LxHostNetworkInterface *interface = NULL;
            for (size_t j = 0; j < s.interface_count; j++)
                if (s.interfaces[j].index == r->interface_index)
                {
                    interface = &s.interfaces[j];
                    break;
                }
            memset(p, 0, 208);
            word16(p, 208);
            p[2] = 4;
            p[3] = 4;
            word16(p + 4, (uint16_t)r->interface_index);
            word32(p + 8, r->flags);
            word32(p + 12, 0x37);
            word32(p + 36, r->netmask);
            word32(p + 64, r->metric);
            if (interface)
            {
                memcpy(p + 40, interface->name, 16);
                word32(p + 60, interface->mtu);
            }
            address(p + 112, r->destination, 1);
            address(p + 128, r->gateway, 1);
            address(p + 144, r->netmask, 1);
            link_address(p + 160, interface);
            address(p + 192, interface ? interface->address : 0, 1);
            p += 208;
        }
    }
    lx_host_network_destroy(&s);
    return 0;
}
