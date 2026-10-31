/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared backing for guest address spaces, without guest ABI or OS/2 policy. */
#define _GNU_SOURCE 1
#include "host_shared.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct LxHostShared
{
    HANDLE object;
    size_t size;
};
LxHostSharedResult lx_host_shared_open(const char *name, size_t size, LxHostShared **output)
{
    *output = NULL;
    LxHostShared *s = calloc(1, sizeof *s);
    if (!s)
        return LX_HOST_SHARED_NOMEM;
    s->object = size ? CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                          (DWORD)((uint64_t)size >> 32), (DWORD)size, name)
                     : OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    DWORD error = GetLastError();
    if (!s->object || (size && error == ERROR_ALREADY_EXISTS))
    {
        if (s->object)
            CloseHandle(s->object);
        free(s);
        return error == ERROR_FILE_NOT_FOUND   ? LX_HOST_SHARED_MISSING
               : error == ERROR_ALREADY_EXISTS ? LX_HOST_SHARED_EXISTS
                                               : LX_HOST_SHARED_DENIED;
    }
    if (!size)
    {
        void *view = MapViewOfFile(s->object, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        MEMORY_BASIC_INFORMATION info;
        if (!view || !VirtualQuery(view, &info, sizeof info))
        {
            if (view)
                UnmapViewOfFile(view);
            CloseHandle(s->object);
            free(s);
            return LX_HOST_SHARED_DENIED;
        }
        size = info.RegionSize;
        UnmapViewOfFile(view);
    }
    s->size = size;
    *output = s;
    return LX_HOST_SHARED_OK;
}
void *lx_host_shared_map(LxHostShared *s, void *hint, int exact)
{
    void *view = MapViewOfFileEx(s->object, FILE_MAP_ALL_ACCESS, 0, 0, s->size, hint);
    if (exact && view != hint)
    {
        if (view)
            UnmapViewOfFile(view);
        return NULL;
    }
    return view;
}
void lx_host_shared_unmap(void *view, size_t size)
{
    (void)size;
    UnmapViewOfFile(view);
}
void lx_host_shared_close(LxHostShared *s)
{
    CloseHandle(s->object);
    free(s);
}
size_t lx_host_shared_size(const LxHostShared *s)
{
    return s->size;
}
