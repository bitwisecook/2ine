/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared by the TCPIP32 units: the per-thread socket error (sockets.c) and the
   resolver state exported as _ress (resolver.c). */
#ifndef TWOINE_TCPIP32_INTERNAL_H
#define TWOINE_TCPIP32_INTERNAL_H
#include <stdint.h>

/* Store an OS/2 socket error (SOC*) and return -1; pass a host result through. */
int lx_tcpip_socket_failure(int code);
int lx_tcpip_socket_result(int result);

/* Maps the guest _res records once; returns 0 if they cannot be mapped. */
int lx_tcpip_resolver_initialize(void);
/* The guest address of the _res records (export _ress). */
uint32_t lx_tcpip_resolver_address(void);
void lx_tcpip_resolver_terminate(void);
#endif
