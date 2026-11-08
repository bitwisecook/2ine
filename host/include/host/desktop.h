/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_DESKTOP_H
#define TWOINE_HOST_DESKTOP_H
/* Presents the launched program's own name and icon as a native application
   identity. Returns when the caller should keep its current executable. */
void lx_desktop_bundle_exec(const char *program, char **argv);
#endif
