/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy sockaddr layout, UDP round-trip, nonblocking errors and resolver exports. */
#include "os2/include/stack16/sys/socket.h"
#include "os2/include/stack16/netdb.h"
#include "os2/include/netdb.h"
#include "host/socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    CHECK(sizeof(Os2LegacyAddress) == 16);
    CHECK(Os2LegacyLongSwap(0x12345678) == 0x78563412);
    CHECK(Os2LegacyShortSwap(0x1234) == 0x3412);
    CHECK(!Os2LegacySocketInit());
    CHECK(Os2LegacySocket(1, 2, 17) == -1 && Os2LegacySocketError() == 10047);
    int receiver = Os2LegacySocket(2, 2, 17), sender = Os2LegacySocket(2, 2, 0);
    CHECK(receiver >= 0 && sender >= 0);
    Os2LegacyAddress destination = {0}, source = {0};
    destination.family = 2;
    destination.address = Os2LegacyInetAddr("127.0.0.1");
    CHECK(!Os2LegacyBind(receiver, &destination, sizeof destination));
    int address_length = sizeof destination;
    CHECK(!Os2LegacyGetSocketName(receiver, &destination, &address_length));
    CHECK(address_length == sizeof destination && destination.family == 2 && destination.port);
    CHECK(Os2LegacyGetSocketName(receiver, &destination, NULL) == -1);
    CHECK(Os2LegacySocketError() == 10014);
    CHECK(!strcmp(Os2LegacyInetText(destination.address), "127.0.0.1"));
    int broadcast = 1;
    CHECK(!Os2LegacySocketOption(sender, 0xffff, 0x20, &broadcast, 4));
    CHECK(Os2LegacySocketOption(sender, 0xffff, 0x7777, &broadcast, 4) == -1);
    CHECK(Os2LegacySocketError() == 10042);
    int enabled = 1, source_length = sizeof source;
    CHECK(!Os2LegacyIoctl(receiver, 0x667e, &enabled, 4));
    char message[] = "legacy UDP round trip", output[sizeof message];
    CHECK(Os2LegacyRecvFrom(receiver, output, sizeof output, 0, &source, &source_length) == -1);
    CHECK(Os2LegacySocketError() == 10035);
    CHECK(Os2LegacySendTo(sender, message, sizeof message, 0, &destination, sizeof destination) ==
          sizeof message);
    CHECK(lx_host_socket_poll(&receiver, 1, 0, 0, 1000) == 1);
    int available = 0;
    CHECK(!Os2LegacyIoctl(receiver, 0x667f, &available, 4) && available == sizeof message);
    CHECK(Os2LegacyRecvFrom(receiver, output, sizeof output, 0, &source, &source_length) ==
          sizeof output);
    CHECK(!memcmp(message, output, sizeof message));
    CHECK(source.family == 2 && source.address == destination.address && source.port &&
          source_length == sizeof source);
    CHECK(!Os2LegacySocketClose(sender) && !Os2LegacySocketClose(receiver));
    CHECK(Os2LegacyIoctl(-1, 0x667e, &enabled, 4) == -1 && Os2LegacySocketError() == 10009);
    char name[256];
    CHECK(!Os2LegacyGetHostName(name, sizeof name) && name[0]);
    Os2LegacyHost *resolved = Os2LegacyGetHostByName("localhost");
    CHECK(resolved && resolved->family == 2 && resolved->length == 4 && resolved->addresses[0]);
    /* TCP32DLL and TCPIP32 share the host lookup: only 4-byte AF_INET addresses
       resolve; anything else fails with NO_RECOVERY in the DLL's own h_errno. */
    uint32_t loopback = 0x0100007f;
    CHECK(!Os2LegacyGetHostByAddress(&loopback, 16, 2) && *Os2LegacyResolverError() == 3);
    CHECK(!Os2LegacyGetHostByAddress(&loopback, 4, 23) && *Os2LegacyResolverError() == 3);
    CHECK(!Os2LegacyGetHostByAddress(NULL, 4, 2) && *Os2LegacyResolverError() == 3);
    CHECK(!OS2_gethostbyaddr(&loopback, 16, 2) && *OS2_tcp_h_errno1() == 3);
    CHECK(OS2_gethostbyname("localhost") && *OS2_tcp_h_errno1() != 3);
    puts("legacy socket UDP, errors, byte order and resolver checks passed");
    return 0;
}
