/* Host dynamic libraries. These are host API modules, never guest DLL images. */
#ifndef TWOINE_HOST_LIBRARY_H
#define TWOINE_HOST_LIBRARY_H
#include <stddef.h>
typedef struct LxHostLibrary LxHostLibrary;
LxHostLibrary *lx_host_library_open(const char *path, char *error, size_t error_size);
void *lx_host_library_symbol(LxHostLibrary *library, const char *name);
void lx_host_library_close(LxHostLibrary *library);
#endif
