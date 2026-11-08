/* Public domain. No rights reserved. Wholly AI-authored. */
#include "hal/network.h"
#include "os2libs/network.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s (errno %d)\n", __LINE__, #x, errno);                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned short word16(const void *p)
{
    unsigned short v;
    memcpy(&v, p, 2);
    return v;
}

static unsigned word32(const void *p)
{
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

int main(void)
{
    LxHostNetwork snapshot;
    CHECK(lx_host_network_snapshot(&snapshot) == 0);
    CHECK(snapshot.interface_count > 0);
    struct ifaddrs *addresses;
    CHECK(getifaddrs(&addresses) == 0);
    size_t count = 0;
    for (struct ifaddrs *p = addresses; p; p = p->ifa_next)
    {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
            continue;
        int found = 0;
        for (size_t i = 0; i < snapshot.interface_count; i++)
            if (!strcmp(snapshot.interfaces[i].name, p->ifa_name) &&
                snapshot.interfaces[i].address ==
                    ((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr)
                found = 1;
        CHECK(found);
        count++;
    }
    CHECK(count == snapshot.interface_count);
    freeifaddrs(addresses);
    unsigned char entries[4096], mib[2 + 42 * 111], routes[4 + 512 * 64], request[32];
    size_t used = 0;
    memset(entries, 0xaa, sizeof entries);
    CHECK(!lx_os2_network_ifconf(entries, 31, &used, 0) && used == 0 && entries[0] == 0xaa);
    CHECK(!lx_os2_network_ifconf(entries, sizeof entries, &used, 0));
    CHECK(used == count * 32 && word16(entries + 16) == 2);
    CHECK(!lx_os2_network_ifconf(entries, sizeof entries, &used, 1));
    CHECK(entries[16] == 16 && entries[17] == 2);
    memcpy(request, entries, 32);
    CHECK(!lx_os2_network_ioctl(0xc0206921, request, sizeof request, 1));
    CHECK(word32(request + 20) == snapshot.interfaces[0].address);
    CHECK(lx_os2_network_ioctl(0x6921, request, 31, 1) == -1 && errno == EFAULT);
    memset(request, 'x', 16);
    CHECK(lx_os2_network_ioctl(0x6921, request, 32, 1) == -1 && errno == ENXIO);
    CHECK(lx_os2_network_ioctl(0x690c, request, 32, 1) == -1 && errno == EOPNOTSUPP);
    CHECK(!lx_os2_network_ioctl(0x6e30, mib, sizeof mib, 0));
    CHECK(word16(mib) > 0 && word16(mib) <= count);
    for (size_t i = 0; i < word16(mib); i++)
    {
        const unsigned char *row = mib + 2 + i * 111;
        int found = 0;
        for (size_t j = 0; j < snapshot.interface_count; j++)
            if (word16(row) == snapshot.interfaces[j].index)
            {
                CHECK(!memcmp(row + 51, snapshot.interfaces[j].mac, 6));
                found = 1;
                break;
            }
        CHECK(found);
    }
    CHECK(!lx_os2_network_ioctl(0x6e2d, routes, sizeof routes, 0));
    CHECK(word16(routes) + word16(routes + 2) == snapshot.route_count);
    int query[] = {4, 39, 0, 2, 1, 0};
    size_t needed = 0;
    CHECK(!lx_os2_network_sysctl(query, 6, NULL, &needed));
    CHECK(needed == snapshot.route_count * 208);
    size_t short_size = 1;
    unsigned char canary = 0xa5;
    if (needed > 1)
    {
        CHECK(lx_os2_network_sysctl(query, 6, &canary, &short_size) == -1 && errno == ENOMEM);
        CHECK(short_size == needed && canary == 0xa5);
    }
    unsigned char *messages = malloc(needed ? needed : 1);
    CHECK(messages && !lx_os2_network_sysctl(query, 6, messages, &needed));
    for (size_t offset = 0; offset < needed; offset += 208)
    {
        CHECK(word16(messages + offset) == 208 && messages[offset + 2] == 4 &&
              messages[offset + 3] == 4);
        CHECK(word32(messages + offset + 12) == 0x37);
        CHECK(messages[offset + 112] == 16 && messages[offset + 113] == 2);
        CHECK(messages[offset + 160] == 32 && messages[offset + 161] == 18);
    }
    free(messages);
    query[4] = 3;
    needed = 0;
    CHECK(!lx_os2_network_sysctl(query, 6, NULL, &needed));
    messages = malloc(needed ? needed : 1);
    CHECK(messages && !lx_os2_network_sysctl(query, 6, messages, &needed));
    unsigned address_count = 0, interface_count = 0;
    for (size_t offset = 0; offset < needed;)
    {
        unsigned length = word16(messages + offset);
        CHECK(length && length <= needed - offset && messages[offset + 2] == 4);
        if (messages[offset + 3] == 14)
        {
            CHECK(length == 164 && messages[offset + 133] == 18);
            interface_count++;
        }
        else
        {
            CHECK(messages[offset + 3] == 12 && length == 66 && messages[offset + 35] == 2);
            address_count++;
        }
        offset += length;
    }
    CHECK(address_count == snapshot.interface_count && interface_count > 0);
    free(messages);
    query[1] = 2;
    CHECK(lx_os2_network_sysctl(query, 6, NULL, &needed) == -1 && errno == ENOPROTOOPT);
    for (size_t i = 0; i < snapshot.dns.server_count; i++)
        CHECK(snapshot.dns.servers[i][0]);
    lx_host_network_destroy(&snapshot);
    CHECK(!snapshot.interfaces && !snapshot.routes && !snapshot.dns.servers);
    lx_host_network_destroy(&snapshot);
    puts("host network identity and OS/2 interface/MAC/route layouts passed");
    return 0;
}
