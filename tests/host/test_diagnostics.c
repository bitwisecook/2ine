/* Public domain. No rights reserved. Wholly AI-authored. */
/* IPv4 echo framing and raw UDP translation against real loopback sockets. */
#include "host/socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef __linux__
#include <sys/socket.h>
#endif
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (socket error %d)\n", __FILE__, __LINE__, #x,               \
                    lx_host_socket_bsd_error());                                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static uint16_t checksum(const unsigned char *p, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i += 2)
        sum += (uint32_t)p[i] << 8 | (i + 1 < n ? p[i + 1] : 0);
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}

static int receive_icmp(int fd, unsigned type, unsigned char *packet, size_t capacity)
{
    for (unsigned i = 0; i < 8; i++)
    {
        if (lx_host_socket_poll(&fd, 1, 0, 0, 2000) != 1)
            return -1;
        int n = (int)lx_host_socket_recv(fd, packet, capacity, 0);
        if (n < 20)
            return -1;
        unsigned header = (packet[0] & 15u) * 4;
        if (header >= 20 && header + 8 <= (unsigned)n && packet[header] == type)
            return n;
    }
    return -1;
}

int main(void)
{
    LxHostSocketHost host;
    uint32_t loopback = lx_host_socket_parse_address("127.0.0.1");
    CHECK(lx_host_socket_host("localhost", &host) && host.family == 2 && host.length == 4);
    CHECK(lx_host_socket_reverse(loopback, &host) && host.name && !lx_host_socket_resolver_error());
    char *protocol, **aliases;
    int number;
    CHECK(lx_host_socket_protocol("icmp", 0, &protocol, &aliases, &number) && number == 1);
    LxHostSocketService service;
    CHECK(lx_host_socket_service_port(lx_host_socket_network16(53), "udp", &service));
    CHECK(!strcmp(service.name, "domain") && !strcmp(service.protocol, "udp"));
    CHECK(lx_host_socket_open_protocol(LX_SOCKET_RAW, 6) == -1 && lx_host_socket_bsd_error() == 43);
    int icmp = lx_host_socket_open_protocol(LX_SOCKET_RAW, 1);
    CHECK(icmp >= 0);
    CHECK(!lx_host_socket_nonblocking(icmp, 1));
    unsigned char echo[] = {8, 0, 0, 0, 0x32, 0x19, 0, 1, '2', 'i', 'n', 'e', '!'};
    uint16_t sum = checksum(echo, sizeof echo);
    echo[2] = sum >> 8;
    echo[3] = sum;
    LxHostSocketAddress target = {loopback, 0};
    CHECK(!lx_host_socket_connect(icmp, &target));
    CHECK(lx_host_socket_send(icmp, echo, sizeof echo, 0) == sizeof echo);
    unsigned char packet[2048];
    int n = receive_icmp(icmp, 0, packet, sizeof packet);
    CHECK(n >= 20 + (int)sizeof echo);
    unsigned header = (packet[0] & 15u) * 4;
    CHECK(packet[9] == 1 && packet[8] > 0 && !memcmp(packet + 12, &loopback, 4));
    uint16_t length;
    memcpy(&length, packet + 2, 2);
    CHECK(length == (unsigned)n - header); /* OS/2 BSD ip_len excludes the IP header. */
    CHECK(!checksum(packet + header, (size_t)n - header));
    CHECK(!memcmp(packet + header + 4, echo + 4, sizeof echo - 4));
    CHECK(!lx_host_socket_close(icmp));
    icmp = lx_host_socket_open_protocol(LX_SOCKET_RAW, 1);
    CHECK(icmp >= 0);
    int receiver = lx_host_socket_open(LX_SOCKET_DGRAM),
        reserve = lx_host_socket_open(LX_SOCKET_DGRAM);
    CHECK(receiver >= 0 && reserve >= 0);
    target.port = 0;
    CHECK(!lx_host_socket_bind(receiver, &target) && !lx_host_socket_name(receiver, &target));
    LxHostSocketAddress source = {loopback, 0};
    CHECK(!lx_host_socket_bind(reserve, &source) && !lx_host_socket_name(reserve, &source));
    CHECK(!lx_host_socket_close(reserve));
    int raw = lx_host_socket_open_protocol(LX_SOCKET_RAW, 255);
    CHECK(raw >= 0);
    unsigned char datagram[33] = {0x45, 0, 33, 0, 0, 0, 0, 0x40, 1, 17};
    memcpy(datagram + 12, &loopback, 4);
    memcpy(datagram + 16, &loopback, 4);
    memcpy(datagram + 20, &source.port, 2);
    memcpy(datagram + 22, &target.port, 2);
    datagram[25] = 13;
    memcpy(datagram + 28, "probe", 5);
    CHECK(lx_host_socket_sendto(raw, datagram, sizeof datagram, 0, &target) == sizeof datagram);
    CHECK(lx_host_socket_poll(&receiver, 1, 0, 0, 1000) == 1);
    LxHostSocketAddress peer;
    char payload[16];
    CHECK(lx_host_socket_recvfrom(receiver, payload, sizeof payload, 0, &peer) == 5);
    CHECK(peer.port == source.port && peer.address == loopback && !memcmp(payload, "probe", 5));
    int options[2] = {0};
    int option_size = 4;
    CHECK(!lx_host_socket_get_option(raw, LX_SO_TYPE, options, &option_size) &&
          options[0] == LX_SOCKET_RAW);
    datagram[6] = 1;
    CHECK(lx_host_socket_sendto(raw, datagram, sizeof datagram, 0, &target) == -1);
    datagram[6] = 0;
    datagram[16] ^= 1;
    CHECK(lx_host_socket_sendto(raw, datagram, sizeof datagram, 0, &target) == -1);
    datagram[16] ^= 1;
    CHECK(!lx_host_socket_close(receiver));
    int errors_available = 1;
#ifdef __linux__
    socklen_t type_size = sizeof number;
    CHECK(!getsockopt(icmp, SOL_SOCKET, SO_TYPE, &number, &type_size));
    errors_available = number == SOCK_RAW;
#endif
    if (errors_available)
    {
        CHECK(lx_host_socket_sendto(raw, datagram, sizeof datagram, 0, &target) == sizeof datagram);
        n = receive_icmp(icmp, 3, packet, sizeof packet);
        CHECK(n >= 56);
        header = (packet[0] & 15u) * 4;
        CHECK(packet[header + 1] ==
              3); /* Real UDP port-unreachable, including the original probe. */
        unsigned quoted = header + 8 + (packet[header + 8] & 15u) * 4;
        CHECK(quoted + 8 <= (unsigned)n);
        CHECK(!memcmp(packet + quoted, &source.port, 2) &&
              !memcmp(packet + quoted + 2, &target.port, 2));
    }
    else
        puts(
            "Linux ping sockets support echo; receiving diagnostic ICMP errors requires CAP_NET_RAW");
    CHECK(!lx_host_socket_close(raw) && !lx_host_socket_close(icmp));
    puts("resolver, raw ICMP echo and raw UDP loopback checks passed");
    return 0;
}
