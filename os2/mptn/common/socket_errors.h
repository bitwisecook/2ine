/* Public domain. No rights reserved. Wholly AI-authored. */
/* Socket error recording shared by TCPIP32 and SO32DLL. Each DLL keeps its own
   per-thread error (sock_errno), passed here by address. */
#ifndef TWOINE_MPTN_SOCKET_ERRORS_H
#define TWOINE_MPTN_SOCKET_ERRORS_H
#include "os2/include/nerrno.h"

/* Stores the OS/2 socket error code (SOC*) and returns -1. */
int lx_mptn_socket_error(int *error, int code);
/* Passes a non-negative host result through; for a negative one, stores the
   host error as an OS/2 socket error and returns -1. */
int lx_mptn_socket_result(int *error, int result);
#endif
