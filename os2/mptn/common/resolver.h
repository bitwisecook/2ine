/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host lookups behind gethostbyname/gethostbyaddr in TCPIP32 and TCP32DLL. The
   result points into a per-thread host resolver snapshot; each DLL copies it
   into its own hostent and keeps its own h_errno, passed here by address. */
#ifndef TWOINE_MPTN_RESOLVER_H
#define TWOINE_MPTN_RESOLVER_H
#include "host/socket.h"

/* Stores the resolver error in *h_error; returns 0 if the host is unknown. */
int lx_mptn_host_by_name(const char *name, LxHostSocketHost *host, int *h_error);
/* Only an IPv4 address (4 bytes, AF_INET) resolves; anything else fails with
   NO_RECOVERY. */
int lx_mptn_host_by_address(const void *address, int length, int family, LxHostSocketHost *host,
                            int *h_error);
#endif
