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
    puts("host socket loopback and readiness checks passed");
    return 0;
}
