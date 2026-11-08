/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_DESKTOP_H
#define TWOINE_HOST_DESKTOP_H
#include "host/window.h"
/* Guest semantics annotate native window cycling; auxiliary views are excluded. */
void lx_host_window_set_cycle_policy(LxHostWindow *window, int eligible, int modal);
void lx_host_window_get_cycle_policy(LxHostWindow *window, int *eligible, int *modal);
/* Presents the launched program's own name and icon as a native application
   identity. Returns when the caller should keep its current executable. */
void lx_desktop_bundle_exec(const char *program, char **argv);
#endif
