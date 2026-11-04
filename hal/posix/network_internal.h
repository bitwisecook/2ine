/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_NETWORK_INTERNAL_H
#define TWOINE_NETWORK_INTERNAL_H
#include "hal/network.h"
struct sockaddr;
struct ifaddrs;
int lx_network_append(void **items, size_t *count, size_t size, const void *item);
uint32_t lx_network_ipv4(const struct sockaddr *address);
int lx_network_string_append(char ***list, size_t *count, const char *value);
int lx_network_mac(const struct ifaddrs *link, LxHostNetworkInterface *entry);
int lx_network_routes(LxHostNetwork *output);
int lx_network_dns(LxHostNetworkDns *output);
#endif
