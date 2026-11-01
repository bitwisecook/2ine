/* Public domain. No rights reserved. Wholly AI-authored. */
/* IPv4 loopback transfer through the host socket contract. */
#include "hal/socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    int listener = lx_host_socket_open(LX_SOCKET_STREAM);
    CHECK(listener >= 0);
    LxHostSocketAddress address = {lx_host_socket_parse_address("127.0.0.1"), 0};
    CHECK(!lx_host_socket_bind(listener, &address));
    CHECK(!lx_host_socket_name(listener, &address));
    CHECK(address.port && !lx_host_socket_listen(listener, 1));
    int client = lx_host_socket_open(LX_SOCKET_STREAM);
    CHECK(client >= 0);
    CHECK(!lx_host_socket_connect(client, &address));
    int server = lx_host_socket_accept(listener, NULL);
    CHECK(server >= 0);
    char message[] = "host socket round trip", output[sizeof message];
    CHECK(lx_host_socket_send(client, message, sizeof message, 0) == sizeof message);
    CHECK(lx_host_socket_poll(&server, 1, 0, 0, 1000) == 1);
    LxHostSocketSet reads = {1, &server};
    CHECK(lx_host_socket_select(server + 1, &reads, NULL, NULL, 1, 0, 1) == 1);
    CHECK(lx_host_socket_recv(server, output, sizeof output, LX_MSG_WAITALL) == sizeof output);
    CHECK(!memcmp(message, output, sizeof message));
    CHECK(!lx_host_socket_close(client) && !lx_host_socket_close(server) &&
          !lx_host_socket_close(listener));
    int receiver = lx_host_socket_open(LX_SOCKET_DGRAM);
    int sender = lx_host_socket_open(LX_SOCKET_DGRAM);
    CHECK(receiver >= 0 && sender >= 0);
    address.port = 0;
    CHECK(!lx_host_socket_bind(receiver, &address));
    CHECK(!lx_host_socket_name(receiver, &address));
    CHECK(!lx_host_socket_nonblocking(receiver, 1));
    LxHostSocketAddress source = {0};
    CHECK(lx_host_socket_recvfrom(receiver, output, sizeof output, 0, &source) == -1);
    CHECK(lx_host_socket_bsd_error() == 35);
    CHECK(lx_host_socket_sendto(sender, message, sizeof message, 0, &address) == sizeof message);
    CHECK(lx_host_socket_poll(&receiver, 1, 0, 0, 1000) == 1);
    int available = 0;
    CHECK(!lx_host_socket_available(receiver, &available) && available == sizeof message);
    CHECK(lx_host_socket_recvfrom(receiver, output, sizeof output, 0, &source) == sizeof output);
    CHECK(source.address == address.address && source.port);
    CHECK(!memcmp(message, output, sizeof message));
    CHECK(!lx_host_socket_close(sender) && !lx_host_socket_close(receiver));
    CHECK(!setenv("TWOINE_SOCKET_BIND_ADDRESS", "127.0.0.1", 1));
    receiver = lx_host_socket_open(LX_SOCKET_DGRAM);
    sender = lx_host_socket_open(LX_SOCKET_DGRAM);
    CHECK(receiver >= 0 && sender >= 0);
    address.address = 0;
    address.port = 0;
    CHECK(!lx_host_socket_bind(receiver, &address));
    CHECK(!lx_host_socket_name(receiver, &address));
    CHECK(address.address == lx_host_socket_parse_address("127.0.0.1"));
    CHECK(lx_host_socket_sendto(sender, message, sizeof message, 0, &address) == sizeof message);
    CHECK(lx_host_socket_poll(&receiver, 1, 0, 0, 1000) == 1);
    CHECK(lx_host_socket_recvfrom(receiver, output, sizeof output, 0, &source) == sizeof output);
    CHECK(source.address == address.address && source.port);
    CHECK(!lx_host_socket_close(sender) && !lx_host_socket_close(receiver));
    CHECK(!setenv("TWOINE_SOCKET_BIND_ADDRESS", "invalid", 1));
    receiver = lx_host_socket_open(LX_SOCKET_DGRAM);
    CHECK(lx_host_socket_bind(receiver, &address) == -1);
    CHECK(lx_host_socket_bsd_error() == 22);
    CHECK(!lx_host_socket_close(receiver));
    CHECK(!unsetenv("TWOINE_SOCKET_BIND_ADDRESS"));
    puts("host socket TCP/UDP loopback, nonblocking and readiness checks passed");
    return 0;
}
