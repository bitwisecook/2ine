/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_NETWORK_INTERNAL_H
#define TWOINE_NETWORK_INTERNAL_H
#include "hal/network.h"

/* Private interface between POSIX snapshot ownership and host OS providers. */
struct sockaddr;
int lx_host_network_append(void **items, size_t *count, size_t size, const void *item);
int lx_host_network_string_append(char ***list, size_t *count, const char *value);
int lx_host_network_platform_mac(const struct sockaddr *address, uint32_t flags, uint8_t mac[6]);
int lx_host_network_platform_routes(LxHostNetwork *out);
int lx_host_network_platform_dns(LxHostNetworkDns *out);
#endif
