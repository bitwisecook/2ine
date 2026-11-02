/* Public domain. No rights reserved. Wholly AI-authored. */
/* Synchronous Windows file handles retain host objects and process-local sharing contracts. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_fs.h"
#include "hal/host_sync.h"
#include "hal/host_thread.h"
typedef struct FileRange
{
    uint64_t offset, length;
    struct FileRange *next;
} FileRange;
typedef struct Open32File
{
    LxHostFile *file;
    char *path;
    ULONG handle, access, share;
    FileRange *ranges;
    struct Open32File *next;
} Open32File;
static Open32File *files;
static LxHostMonitor *monitor;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}
static ULONG file_error(LxHostFsError error)
{
    static const ULONG errors[] = {0, 2, 3, 80, 5, 8, 4, 112, 5, 206, 32, 19, 17, 87, 1117, 145};
    return (unsigned)error < sizeof errors / sizeof *errors ? errors[error] : 1117;
}
static BOOL result_error(LxHostFsError error)
{
    if (error)
        open32SetError(file_error(error));
    return error == LX_HOST_FS_OK;
}
static void destroy_file(void *data)
{
    Open32File *file = data;
    lx_host_monitor_lock(monitor);
    Open32File **link = &files;
    while (*link && *link != file)
        link = &(*link)->next;
    if (*link)
        *link = file->next;
    lx_host_monitor_unlock(monitor);
    lx_host_file_close(file->file);
    while (file->ranges)
    {
        FileRange *range = file->ranges;
        file->ranges = range->next;
        free(range);
    }
    free(file->path);
    free(file);
}
typedef struct CreateFileCall
{
    Open32File *file;
    BOOL locked;
    unsigned flags;
    ULONG disposition, attributes;
} CreateFileCall;
static void create_cleanup(void *data)
{
    CreateFileCall *call = data;
    if (call->locked)
        lx_host_monitor_unlock(monitor);
    if (call->file)
    {
        if (call->file->file)
            lx_host_file_close(call->file->file);
        free(call->file->path);
        free(call->file);
    }
}
static uint32 create_file(void *data)
{
    CreateFileCall *call = data;
    Open32File *file = call->file;
    lx_host_monitor_lock(monitor);
    call->locked = TRUE;
    for (Open32File *other = files; other; other = other->next)
        if (!strcasecmp(other->path, file->path) &&
            ((file->access & ~other->share) || (other->access & ~file->share)))
        {
            open32SetError(32);
            return UINT32_MAX;
        }
    LxHostFsError error;
    BOOL created = FALSE;
    if (call->disposition == 1 || call->disposition == 2 || call->disposition == 4)
    {
        file->file = lx_host_file_open(file->path,
                                       call->flags | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE,
                                       call->attributes & 1, &error);
        created = file->file != NULL;
        if (!file->file && error == LX_HOST_FS_EXISTS && call->disposition != 1)
            file->file = lx_host_file_open(
                file->path, call->flags | (call->disposition == 2 ? LX_HOST_FILE_TRUNCATE : 0), 0,
                &error);
    }
    else
        file->file = lx_host_file_open(
            file->path, call->flags | (call->disposition == 5 ? LX_HOST_FILE_TRUNCATE : 0), 0,
            &error);
    if (!file->file)
    {
        open32SetError(file_error(error));
        return UINT32_MAX;
    }
    LxHostFileInfo info;
    error = lx_host_file_info(file->file, &info);
    if (error || info.kind == LX_HOST_FILE_DIRECTORY)
    {
        open32SetError(error ? file_error(error) : 5);
        return UINT32_MAX;
    }
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_FILE, file, destroy_file);
    if (!handle)
    {
        open32SetError(8);
        return UINT32_MAX;
    }
    file->handle = handle;
    file->next = files;
    files = file;
    call->file = NULL;
    open32SetError(!created && (call->disposition == 2 || call->disposition == 4) ? 183 : 0);
    return handle;
}
ULONG CreateFile(PSZ name, ULONG access, ULONG share, PVOID security, ULONG disposition,
                 ULONG attributes, ULONG template)
{
    TRACE_NATIVE("CreateFile('%s', %x, %x, %p, %u, %x, %u)", name ? name : "", (uint)access,
                 (uint)share, security, (uint)disposition, (uint)attributes, (uint) template);
    const ULONG supported = 0x80 | 0x27 | 0x100 | 0x80000000u | 0x10000000 | 0x08000000;
    if (!name || !*name || disposition < 1 || disposition > 5 || (share & ~7u) ||
        (access & ~0xc0000000u))
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    if ((attributes & ~supported) || template)
    {
        open32SetError(50);
        return UINT32_MAX;
    }
    if (security && (lx_guest_load_u32(security) != 12 || lx_guest_load_u32((uint8 *)security + 4)))
    {
        open32SetError(50);
        return UINT32_MAX;
    }
    if (disposition == 5 && !(access & 0x40000000u))
    {
        open32SetError(5);
        return UINT32_MAX;
    }
    uint32 error = 0;
    char *path = GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(name, &error) : NULL;
    if (!path)
    {
        open32SetError(error ? error : 3);
        return UINT32_MAX;
    }
    char *canonical = lx_host_path_canonical(path);
    if (canonical)
    {
        free(path);
        path = canonical;
    }
    Open32File *file = calloc(1, sizeof *file);
    if (!file)
    {
        free(path);
        open32SetError(8);
        return UINT32_MAX;
    }
    file->path = path;
    file->share = share;
    file->access = (access & 0x80000000u ? 1 : 0) | (access & 0x40000000u ? 2 : 0);
    unsigned flags = file->access ? file->access : LX_HOST_FILE_READ;
    if (attributes & 0x80000000u)
        flags |= LX_HOST_FILE_SYNC;
    if (!security || !lx_guest_load_u32((uint8 *)security + 8))
        flags |= LX_HOST_FILE_NOINHERIT;
    lx_host_once(&once, initialize);
    CreateFileCall call = {
        .file = file, .flags = flags, .disposition = disposition, .attributes = attributes};
    return lx_host_guarded_call(create_file, &call, create_cleanup, &call);
}
typedef struct FileCall
{
    LxHandleBorrow borrow;
    PVOID data;
    ULONG size, operation, origin;
    PULONG actual;
    int64_t distance;
    uint64 position;
    LxHostFileInfo info;
    uint64_t length;
    BOOL locked;
} FileCall;
enum
{
    FILE_READ,
    FILE_WRITE,
    FILE_SEEK,
    FILE_SIZE,
    FILE_FLUSH,
    FILE_INFO,
    FILE_LOCK,
    FILE_UNLOCK
};
static void release_file(void *data)
{
    FileCall *call = data;
    if (call->locked)
        lx_host_monitor_unlock(monitor);
    lx_handle_release(&call->borrow);
}
static BOOL overlaps(uint64_t offset, uint64_t length, const FileRange *range)
{
    return length && offset < range->offset + range->length && range->offset < offset + length;
}
static BOOL range_busy(Open32File *file, uint64_t offset, uint64_t length, BOOL include_self)
{
    for (Open32File *other = files; other; other = other->next)
        if ((include_self || other != file) && !strcasecmp(other->path, file->path))
            for (FileRange *range = other->ranges; range; range = range->next)
                if (overlaps(offset, length, range))
                    return TRUE;
    return FALSE;
}
static uint32 file_operation(void *data)
{
    FileCall *call = data;
    Open32File *file = call->borrow.resource;
    LxHostFsError error = LX_HOST_FS_INVALID;
    if (call->operation == FILE_READ || call->operation == FILE_WRITE)
    {
        BOOL writing = call->operation == FILE_WRITE;
        if (!(file->access & (writing ? 2 : 1)))
        {
            open32SetError(5);
            return FALSE;
        }
        lx_host_monitor_lock(monitor);
        call->locked = TRUE;
        uint64_t position;
        int conflict = 0;
        error = lx_host_file_seek(file->file, 0, 1, &position);
        if (!error)
            error = lx_host_file_range_conflict(file->file, position, call->size, &conflict);
        if (error)
            return result_error(error);
        if (conflict || range_busy(file, position, call->size, FALSE))
        {
            open32SetError(33);
            return FALSE;
        }
        size_t actual = 0;
        error = writing ? lx_host_file_write(file->file, call->data, call->size, &actual)
                        : lx_host_file_read(file->file, call->data, call->size, &actual);
        if (call->actual)
            *call->actual = (ULONG)actual;
    }
    else if (call->operation == FILE_SEEK)
        error = lx_host_file_seek(file->file, call->distance, call->origin, &call->position);
    else if (call->operation == FILE_SIZE)
    {
        if (!(file->access & 2))
        {
            open32SetError(5);
            return FALSE;
        }
        error = lx_host_file_seek(file->file, 0, 1, &call->position);
        if (!error)
            error = lx_host_file_resize(file->file, call->position);
    }
    else if (call->operation == FILE_FLUSH)
        error = lx_host_file_flush(file->file);
    else if (call->operation == FILE_INFO)
        error = lx_host_file_info(file->file, &call->info);
    else if (call->operation == FILE_LOCK || call->operation == FILE_UNLOCK)
    {
        if (!(file->access & 2))
        {
            open32SetError(file->access ? 50 : 5);
            return FALSE;
        }
        lx_host_monitor_lock(monitor);
        call->locked = TRUE;
        if (call->operation == FILE_LOCK)
        {
            if (range_busy(file, call->position, call->length, TRUE))
            {
                open32SetError(33);
                return FALSE;
            }
            FileRange *range = calloc(1, sizeof *range);
            if (!range)
            {
                open32SetError(8);
                return FALSE;
            }
            error = lx_host_file_lock_range(file->file, call->position, call->length, TRUE);
            if (error)
                free(range);
            else
            {
                range->offset = call->position;
                range->length = call->length;
                range->next = file->ranges;
                file->ranges = range;
            }
        }
        else
        {
            FileRange **link = &file->ranges;
            while (*link && ((*link)->offset != call->position || (*link)->length != call->length))
                link = &(*link)->next;
            if (!*link)
            {
                open32SetError(158);
                return FALSE;
            }
            error = lx_host_file_lock_range(file->file, call->position, call->length, FALSE);
            if (!error)
            {
                FileRange *range = *link;
                *link = range->next;
                free(range);
            }
        }
        if (error == LX_HOST_FS_BUSY)
        {
            open32SetError(33);
            return FALSE;
        }
    }
    if (call->operation == FILE_SEEK && error == LX_HOST_FS_INVALID)
    {
        open32SetError(131);
        return FALSE;
    }
    return result_error(error);
}
static BOOL invoke(ULONG handle, FileCall *call)
{
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FILE, &call->borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    return lx_host_guarded_call(file_operation, call, release_file, call);
}
int open32FileIO(ULONG handle, PVOID data, ULONG size, PULONG actual, BOOL writing)
{
    FileCall call = {.data = data,
                     .size = size,
                     .actual = actual,
                     .operation = writing ? FILE_WRITE : FILE_READ};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FILE, &call.borrow))
        return -1;
    return (int)lx_host_guarded_call(file_operation, &call, release_file, &call);
}
BOOL open32CloseFile(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FILE, &borrow))
        return FALSE;
    Open32File *file = borrow.resource;
    lx_host_monitor_lock(monitor);
    file->handle = 0;
    lx_host_monitor_unlock(monitor);
    BOOL result = lx_handle_close(handle, LX_HANDLE_OPEN32_FILE);
    lx_handle_release(&borrow);
    return result;
}
ULONG SetFilePointer(ULONG handle, LONG distance, PLONG high, ULONG origin)
{
    if (origin > 2)
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    FileCall call = {.operation = FILE_SEEK,
                     .origin = origin,
                     .distance =
                         high ? (int64_t)((uint64)(ULONG)*high << 32 | (ULONG)distance) : distance};
    if (!invoke(handle, &call))
        return UINT32_MAX;
    if (high)
        *high = (LONG)(call.position >> 32);
    open32SetError(0);
    return (ULONG)call.position;
}
ULONG GetFileSize(ULONG handle, PULONG high)
{
    FileCall call = {.operation = FILE_INFO};
    if (!invoke(handle, &call))
        return UINT32_MAX;
    if (high)
        *high = (ULONG)(call.info.size >> 32);
    open32SetError(0);
    return (ULONG)call.info.size;
}
BOOL SetEndOfFile(ULONG handle)
{
    FileCall call = {.operation = FILE_SIZE};
    return invoke(handle, &call);
}
BOOL FlushFileBuffers(ULONG handle)
{
    FileCall call = {.operation = FILE_FLUSH};
    return invoke(handle, &call);
}
ULONG GetFileType(ULONG handle)
{
    FileCall call = {.operation = FILE_INFO};
    if (!invoke(handle, &call))
        return 0;
    return call.info.kind == LX_HOST_FILE_PIPE ? 3 : call.info.kind == LX_HOST_FILE_DEVICE ? 2 : 1;
}
static BOOL lock_range(ULONG handle, ULONG offset_low, ULONG offset_high, ULONG length_low,
                       ULONG length_high, BOOL acquire)
{
    FileCall call = {.operation = acquire ? FILE_LOCK : FILE_UNLOCK,
                     .position = (uint64_t)offset_high << 32 | offset_low,
                     .length = (uint64_t)length_high << 32 | length_low};
    if (!call.length || call.position > INT64_MAX ||
        call.length > (uint64_t)INT64_MAX - call.position)
    {
        open32SetError(87);
        return FALSE;
    }
    return invoke(handle, &call);
}
BOOL LockFile(ULONG handle, ULONG offset_low, ULONG offset_high, ULONG length_low,
              ULONG length_high)
{
    TRACE_NATIVE("LockFile(%u, %x, %x, %x, %x)", (uint)handle, (uint)offset_low, (uint)offset_high,
                 (uint)length_low, (uint)length_high);
    return lock_range(handle, offset_low, offset_high, length_low, length_high, TRUE);
}
BOOL UnlockFile(ULONG handle, ULONG offset_low, ULONG offset_high, ULONG length_low,
                ULONG length_high)
{
    return lock_range(handle, offset_low, offset_high, length_low, length_high, FALSE);
}
void open32FilesCleanup(void)
{
    if (!monitor)
        return;
    for (;;)
    {
        ULONG handle = 0;
        lx_host_monitor_lock(monitor);
        for (Open32File *file = files; file && !handle; file = file->next)
            handle = file->handle;
        lx_host_monitor_unlock(monitor);
        if (!handle)
            break;
        open32CloseFile(handle);
    }
}
