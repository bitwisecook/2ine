/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 LX socket regression; requires free loopback UDP port 47684. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct Address
{
    unsigned short family, port;
    unsigned long address;
    unsigned char pad[8];
} Address;
typedef struct Host
{
    char *name;
    char **aliases;
    int family, length;
    char **addresses;
} Host;
int _System socket(int, int, int);
int _System bind(int, Address *, int);
int _System ioctl(int, int, void *, int);
int _System sendto(int, void *, int, int, Address *, int);
int _System recvfrom(int, void *, int, int, Address *, int *);
int _System soclose(int);
int _System sock_errno(void);
unsigned short _System bswap(unsigned short);
unsigned long _System lswap(unsigned long);
unsigned long _System inet_addr(const char *);
int _System gethostname(char *, int);
Host *_System gethostbyname(const char *);
unsigned long _System DosSleep(unsigned long);
#define CHECK(x)                                                                                   \
    if (!(x))                                                                                      \
    {                                                                                              \
        printf("failure line %d: %s\n", __LINE__, #x);                                             \
        return 1;                                                                                  \
    }
int main(void)
{
    int receiver, sender, n, len, i, on = 1;
    Address destination = {0}, source = {0};
    char output[32], message[] = "guest UDP", name[256];
    Host *host;
    CHECK(lswap(0x12345678UL) == 0x78563412UL && bswap(0x1234) == 0x3412);
    CHECK(gethostname(name, sizeof name) == 0 && name[0]);
    host = gethostbyname("localhost");
    CHECK(host && host->family == 2 && host->length == 4 && host->addresses[0]);
    CHECK(host->name && host->name[0] && host->aliases);
    receiver = socket(2, 2, 17);
    sender = socket(2, 2, 17);
    CHECK(receiver >= 0 && sender >= 0);
    destination.family = 2;
    destination.port = bswap(47684);
    destination.address = inet_addr("127.0.0.1");
    CHECK(bind(receiver, &destination, sizeof destination) == 0);
    CHECK(ioctl(receiver, 0x667e, &on, 4) == 0);
    len = sizeof source;
    CHECK(recvfrom(receiver, output, sizeof output, 0, &source, &len) == -1 &&
          sock_errno() == 10035);
    CHECK(sendto(sender, message, sizeof message, 0, &destination, sizeof destination) ==
          sizeof message);
    for (i = 0; i < 100; i++)
    {
        len = sizeof source;
        n = recvfrom(receiver, output, sizeof output, 0, &source, &len);
        if (n >= 0)
            break;
        DosSleep(1);
    }
    CHECK(n == sizeof message && !memcmp(output, message, sizeof message));
    CHECK(source.family == 2 && source.port && source.address == destination.address &&
          len == sizeof source);
    CHECK(soclose(sender) == 0 && soclose(receiver) == 0);
    puts("guest legacy socket and resolver checks passed");
    return 0;
}
