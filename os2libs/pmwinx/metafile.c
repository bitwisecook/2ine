/* Public domain. No rights reserved. Wholly AI-authored. */
/* Memory WMFs own a validated copy, independently of the caller's buffer. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "guest/handles.h"
#include "hal/host_sync.h"

typedef struct Open32Metafile
{
    struct Open32Metafile *next;
    ULONG handle, size;
    UCHAR data[];
} Open32Metafile;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static Open32Metafile *files;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
}
static void destroy_file(void *resource)
{
    Open32Metafile *file = resource;
    lx_host_monitor_lock(monitor);
    Open32Metafile **slot = &files;
    while (*slot && *slot != file)
        slot = &(*slot)->next;
    if (*slot)
        *slot = file->next;
    lx_host_monitor_unlock(monitor);
    free(file);
}
static BOOL valid_wmf(const UCHAR *data, ULONG size)
{
    if (!data || size < 24 || size > 64u * 1024 * 1024 || (size & 1))
        return FALSE;
    USHORT type = lx_guest_load_u16(data), version = lx_guest_load_u16(data + 4);
    if ((type != 1 && type != 2) || lx_guest_load_u16(data + 2) != 9 ||
        (version != 0x100 && version != 0x300) ||
        lx_guest_load_u32(data + 6) != size / 2 || lx_guest_load_u16(data + 16))
        return FALSE;
    ULONG maximum = lx_guest_load_u32(data + 12);
    if (maximum < 3 || maximum > (size - 18) / 2)
        return FALSE;
    for (ULONG offset = 18; offset <= size - 6;)
    {
        ULONG words = lx_guest_load_u32(data + offset);
        USHORT function = lx_guest_load_u16(data + offset + 4);
        if (words < 3 || words > maximum || words > (size - offset) / 2)
            return FALSE;
        if (!function)
            return words == 3 && offset + 6 == size;
        offset += words * 2;
    }
    return FALSE;
}
ULONG SetMetaFileBitsEx(ULONG size, PVOID data)
{
    TRACE_NATIVE("SetMetaFileBitsEx(%u, %p)", size, data);
    if (!valid_wmf(data, size))
    {
        open32SetError(13);
        return 0;
    }
    lx_host_once(&once, initialize);
    Open32Metafile *file = monitor ? malloc(sizeof *file + size) : NULL;
    if (!file)
    {
        open32SetError(8);
        return 0;
    }
    file->size = size;
    memcpy(file->data, data, size);
    lx_host_monitor_lock(monitor);
    file->handle = lx_handle_create(LX_HANDLE_OPEN32_METAFILE, file, destroy_file);
    if (file->handle)
    {
        file->next = files;
        files = file;
    }
    lx_host_monitor_unlock(monitor);
    ULONG handle = file->handle;
    if (!handle)
    {
        free(file);
        open32SetError(8);
    }
    return handle;
}
ULONG GetMetaFileBitsEx(ULONG handle, ULONG size, PVOID data)
{
    TRACE_NATIVE("GetMetaFileBitsEx(%u, %u, %p)", handle, size, data);
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    Open32Metafile *file = borrow.resource;
    ULONG result = data && size < file->size ? size : file->size;
    if (data)
        memcpy(data, file->data, result);
    lx_handle_release(&borrow);
    return result;
}
BOOL DeleteMetaFile(ULONG handle)
{
    TRACE_NATIVE("DeleteMetaFile(%u)", handle);
    if (lx_handle_close(handle, LX_HANDLE_OPEN32_METAFILE))
        return TRUE;
    open32SetError(6);
    return FALSE;
}
void open32MetafilesCleanup(void)
{
    while (files)
        lx_handle_close(files->handle, LX_HANDLE_OPEN32_METAFILE);
    if (monitor)
        lx_host_monitor_destroy(monitor);
    monitor = NULL;
    once = (LxHostOnce)LX_HOST_ONCE_INIT;
}
