/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host library lifetime and symbol lookup behind an opaque handle. */
#include "host_library.h"
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
struct LxHostLibrary
{
    void *object;
};
LxHostLibrary *lx_host_library_open(const char *path, char *error, size_t size)
{
    LxHostLibrary *library = calloc(1, sizeof *library);
    if (!library)
    {
        if (error && size)
            snprintf(error, size, "Out of memory");
        return NULL;
    }
    library->object = dlopen(path, RTLD_LOCAL | RTLD_NOW);
    if (library->object)
        return library;
    const char *message = dlerror();
    if (error && size)
        snprintf(error, size, "%s", message ? message : "Host library load failed");
    free(library);
    return NULL;
}
void *lx_host_library_symbol(LxHostLibrary *library, const char *name)
{
    return dlsym(library->object, name);
}
void lx_host_library_close(LxHostLibrary *library)
{
    dlclose(library->object);
    free(library);
}
