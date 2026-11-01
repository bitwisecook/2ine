/* Public domain. No rights reserved. Wholly AI-authored. */
/* Win32 find handles own their directory cursor independently of file handles. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_fs.h"
#include "hal/host_sync.h"
typedef struct FileSearch
{
    LxHostDirectory *directory;
    LxHostMonitor *monitor;
    char *path, *pattern;
} FileSearch;
static ULONG find_error(LxHostFsError error)
{
    switch (error)
    {
        case LX_HOST_FS_MISSING:
            return 3;
        case LX_HOST_FS_NOT_DIRECTORY:
            return 267;
        case LX_HOST_FS_DENIED:
            return 5;
        case LX_HOST_FS_NO_MEMORY:
            return 8;
        case LX_HOST_FS_TOO_MANY_FILES:
            return 4;
        case LX_HOST_FS_NAME_LONG:
            return 206;
        default:
            return 31;
    }
}
static unsigned folded(unsigned char value)
{
    return value >= 'a' && value <= 'z' ? value - 'a' + 'A' : value;
}
static BOOL match_name(const char *name, const char *pattern)
{
    const char *star = NULL, *retry = NULL;
    if (!strcmp(pattern, "*.*"))
        pattern = "*";
    while (*name)
    {
        if (*pattern == '?' ||
            (*pattern && folded((unsigned char)*pattern) == folded((unsigned char)*name)))
        {
            name++;
            pattern++;
        }
        else if (*pattern == '*')
        {
            star = pattern++;
            retry = name;
        }
        else if (star)
        {
            pattern = star + 1;
            name = ++retry;
        }
        else
            return FALSE;
    }
    while (*pattern == '*')
        pattern++;
    return !*pattern;
}
static void search_destroy(void *data)
{
    FileSearch *search = data;
    if (search->directory)
        lx_host_directory_close(search->directory);
    if (search->monitor)
        lx_host_monitor_destroy(search->monitor);
    free(search->path);
    free(search);
}
static void find_time(uint8 *output, int64_t seconds)
{
    /* Unix seconds are UTC; FILETIME counts 100-ns intervals from 1601. */
    uint64_t ticks = seconds < -INT64_C(11644473600) || seconds > INT64_C(1833029933770)
                         ? 0
                         : (uint64_t)(seconds + INT64_C(11644473600)) * UINT64_C(10000000);
    lx_guest_store_u32(output, (uint32)ticks);
    lx_guest_store_u32(output + 4, (uint32)(ticks >> 32));
}
static BOOL search_next(FileSearch *search, PVOID output, BOOL first)
{
    const char *name;
    LxHostFileInfo info;
    LxHostFsError error = LX_HOST_FS_OK;
    int status;
    while ((status = lx_host_directory_read(search->directory, &name, &info, &error)) == 1)
    {
        if (!strcmp(name, ".") || !strcmp(name, "..") || !match_name(name, search->pattern) ||
            (info.kind != LX_HOST_FILE_REGULAR && info.kind != LX_HOST_FILE_DIRECTORY))
            continue;
        size_t length = strlen(name);
        if (length >= 260)
        {
            open32SetError(206);
            return FALSE;
        }
        uint8 wire[320] = {0};
        ULONG attributes =
            (info.kind == LX_HOST_FILE_DIRECTORY ? 0x10 : 0) | (info.readonly ? 1 : 0);
        lx_guest_store_u32(wire, attributes ? attributes : 0x80);
        find_time(wire + 12, info.accessed);
        find_time(wire + 20, info.modified);
        lx_guest_store_u32(wire + 28, (uint32)(info.size >> 32));
        lx_guest_store_u32(wire + 32, (uint32)info.size);
        memcpy(wire + 44, name, length + 1);
        memcpy(output, wire, sizeof wire);
        return TRUE;
    }
    open32SetError(status < 0 ? find_error(error) : first ? 2 : 18);
    return FALSE;
}
ULONG FindFirstFile(PSZ name, PVOID output)
{
    TRACE_NATIVE("FindFirstFile('%s', %p)", name, output);
    if (!name || !*name || !output)
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    size_t length = strlen(name);
    if (name[length - 1] == '\\' || name[length - 1] == '/')
    {
        open32SetError(3);
        return UINT32_MAX;
    }
    FileSearch *search = calloc(1, sizeof *search);
    if (!search)
    {
        open32SetError(8);
        return UINT32_MAX;
    }
    ULONG error = 0;
    search->path = GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(name, &error) : NULL;
    if (!search->path)
    {
        search_destroy(search);
        open32SetError(error ? error : 87);
        return UINT32_MAX;
    }
    search->pattern = strrchr(search->path, '/');
    if (!search->pattern)
    {
        search_destroy(search);
        open32SetError(87);
        return UINT32_MAX;
    }
    *search->pattern++ = 0;
    LxHostFsError host_error;
    search->directory = lx_host_directory_open(*search->path ? search->path : "/", &host_error);
    search->monitor = lx_host_monitor_create();
    if (!search->directory || !search->monitor)
    {
        open32SetError(search->directory ? 8 : find_error(host_error));
        search_destroy(search);
        return UINT32_MAX;
    }
    if (!search_next(search, output, TRUE))
    {
        search_destroy(search);
        return UINT32_MAX;
    }
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_FIND, search, search_destroy);
    if (!handle)
    {
        search_destroy(search);
        open32SetError(8);
        return UINT32_MAX;
    }
    return handle;
}
BOOL FindNextFile(ULONG handle, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!output || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_FIND, &borrow))
    {
        open32SetError(output ? 6 : 87);
        return FALSE;
    }
    FileSearch *search = borrow.resource;
    lx_host_monitor_lock(search->monitor);
    BOOL result = search_next(search, output, FALSE);
    lx_host_monitor_unlock(search->monitor);
    lx_handle_release(&borrow);
    return result;
}
BOOL FindClose(ULONG handle)
{
    if (lx_handle_close(handle, LX_HANDLE_OPEN32_FIND))
        return TRUE;
    open32SetError(6);
    return FALSE;
}
