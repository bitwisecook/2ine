/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host-specific parts of the fatal report. The register and image writers can
   run in a signal handler: they may not allocate, lock or use stdio. */
#ifndef TWOINE_HOST_POSIX_FATAL_REPORT_HOOKS_H
#define TWOINE_HOST_POSIX_FATAL_REPORT_HOOKS_H
#include <spawn.h>
#include <stddef.h>
void lx_host_fatal_write_bytes(const char *bytes, size_t size);
void lx_host_fatal_write_registers(const void *context);
void lx_host_fatal_write_images(void);
int lx_host_fatal_display_available(void);
/* Returns extra spawn flags and keeps stray descriptors out of the helper. */
short lx_host_fatal_prepare_spawn(posix_spawn_file_actions_t *actions);
#endif
