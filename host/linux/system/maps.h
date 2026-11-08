/* Public domain. No rights reserved. Wholly AI-authored. */
/* The one /proc/self/maps reader of the Linux adapter. It allocates nothing
   and uses only async-signal-safe calls, so fault handlers may use it. */
#ifndef TWOINE_HOST_LINUX_SYSTEM_MAPS_H
#define TWOINE_HOST_LINUX_SYSTEM_MAPS_H
#include <stddef.h>

/* Calls visit for each line, including its newline; a line longer than the
   reader's buffer arrives truncated. visit returns 0 to stop. Returns 0 when
   the map cannot be opened. */
int lx_linux_maps_visit(int (*visit)(const char *line, size_t length, void *context),
                        void *context);
#endif
