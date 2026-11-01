/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy sockaddr layout, UDP round-trip, nonblocking errors and resolver exports. */
#include "os2libs/so32dll.h"
#include "os2libs/tcp32dll.h"
#include "hal/socket.h"
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
    LxHostSocketAddress host;
    CHECK(!lx_host_socket_name(receiver, &host));
    destination.port = host.port;
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
    puts("legacy socket UDP, errors, byte order and resolver checks passed");
    return 0;
}
