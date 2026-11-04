/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OS2_NETWORK_H
#define TWOINE_OS2_NETWORK_H
#include <stdint.h>
#include <stddef.h>
/* TCP/IP 4.0 uses a 16-bit family; 4.1+ uses length/family bytes. */
int lx_os2_network_ioctl(unsigned command, void *argument, size_t length, int bsd_address);
int lx_os2_network_ifconf(void *buffer, size_t capacity, size_t *used, int bsd_address);
int lx_os2_network_sysctl(const int *name, unsigned count, void *buffer, size_t *length);
#endif
