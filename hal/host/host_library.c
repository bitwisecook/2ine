/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host library lifetime and symbol lookup behind an opaque handle. */
#include "host_library.h"
#include <stdlib.h>
#include <stdio.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct LxHostLibrary { HMODULE object; };
LxHostLibrary *lx_host_library_open(const char *path, char *error, size_t size)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = n ? malloc((size_t) n * sizeof *wide) : NULL;
    LxHostLibrary *library = wide ? calloc(1, sizeof *library) : NULL;
    if (library && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n))
        library->object = LoadLibraryW(wide);
    free(wide);
    if (library && library->object) return library;
    free(library);
    if (error && size) snprintf(error, size, "Host library load failed (%lu)", (unsigned long) GetLastError());
    return NULL;
}
void *lx_host_library_symbol(LxHostLibrary *library, const char *name) { return (void *) GetProcAddress(library->object, name); }
void lx_host_library_close(LxHostLibrary *library) { FreeLibrary(library->object); free(library); }
#else
#include <dlfcn.h>
struct LxHostLibrary { void *object; };
LxHostLibrary *lx_host_library_open(const char *path, char *error, size_t size)
{
    LxHostLibrary *library = calloc(1, sizeof *library);
    if (!library) { if (error && size) snprintf(error, size, "Out of memory"); return NULL; }
    library->object = dlopen(path, RTLD_LOCAL | RTLD_NOW);
    if (library->object) return library;
    const char *message = dlerror();
    if (error && size) snprintf(error, size, "%s", message ? message : "Host library load failed");
    free(library); return NULL;
}
void *lx_host_library_symbol(LxHostLibrary *library, const char *name) { return dlsym(library->object, name); }
void lx_host_library_close(LxHostLibrary *library) { dlclose(library->object); free(library); }
#endif
