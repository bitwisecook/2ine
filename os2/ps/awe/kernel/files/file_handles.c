/* Public domain. No rights reserved. Wholly AI-authored. */
/* Synchronous Windows file handles retain host objects and process-local sharing contracts. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "host/fs.h"
#include "host/sync.h"
#include "host/thread.h"
#include "os2/include/bsedos.h"
#include <stdio.h>

/* Seconds from 1601-01-01 (FILETIME) to 1970-01-01 (host time). */
static const int64_t FILETIME_UNIX_EPOCH_SECONDS = 11644473600LL;

enum
{
    TICKS_PER_SECOND = 10000000,
    /* FILESTATUS3 opens with three FDATE/FTIME pairs: creation, access, write. */
    FILE_STAMP_COUNT = 3,
    FILE_STAMP_BYTES = 4,
    FILE_STAMP_TIME = 2,
    CREATE_FILE_ATTRIBUTES = WIN32_FILE_ATTRIBUTE_NORMAL | WIN32_FILE_ATTRIBUTE_READONLY |
                             WIN32_FILE_ATTRIBUTE_HIDDEN | WIN32_FILE_ATTRIBUTE_SYSTEM |
                             WIN32_FILE_ATTRIBUTE_ARCHIVE | WIN32_FILE_ATTRIBUTE_TEMPORARY,
    CREATE_FILE_SHARE_MODES =
        WIN32_FILE_SHARE_READ | WIN32_FILE_SHARE_WRITE | WIN32_FILE_SHARE_DELETE
};

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
    static const ULONG errors[] = {[LX_HOST_FS_OK] = NO_ERROR,
                                   [LX_HOST_FS_MISSING] = ERROR_FILE_NOT_FOUND,
                                   [LX_HOST_FS_NOT_DIRECTORY] = ERROR_PATH_NOT_FOUND,
                                   [LX_HOST_FS_EXISTS] = ERROR_FILE_EXISTS,
                                   [LX_HOST_FS_DENIED] = ERROR_ACCESS_DENIED,
                                   [LX_HOST_FS_NO_MEMORY] = ERROR_NOT_ENOUGH_MEMORY,
                                   [LX_HOST_FS_TOO_MANY_FILES] = ERROR_TOO_MANY_OPEN_FILES,
                                   [LX_HOST_FS_FULL] = ERROR_DISK_FULL,
                                   [LX_HOST_FS_IS_DIRECTORY] = ERROR_ACCESS_DENIED,
                                   [LX_HOST_FS_NAME_LONG] = ERROR_FILENAME_EXCED_RANGE,
                                   [LX_HOST_FS_BUSY] = ERROR_SHARING_VIOLATION,
                                   [LX_HOST_FS_READ_ONLY] = ERROR_WRITE_PROTECT,
                                   [LX_HOST_FS_CROSS_DEVICE] = ERROR_NOT_SAME_DEVICE,
                                   [LX_HOST_FS_INVALID] = ERROR_INVALID_PARAMETER,
                                   [LX_HOST_FS_IO] = ERROR_IO_DEVICE,
                                   [LX_HOST_FS_NOT_EMPTY] = ERROR_DIR_NOT_EMPTY};
    return (unsigned)error < sizeof errors / sizeof *errors ? errors[error] : ERROR_IO_DEVICE;
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
            open32SetError(ERROR_SHARING_VIOLATION);
            return UINT32_MAX;
        }
    LxHostFsError error;
    BOOL created = FALSE;
    if (call->disposition == WIN32_CREATE_NEW || call->disposition == WIN32_CREATE_ALWAYS ||
        call->disposition == WIN32_OPEN_ALWAYS)
    {
        file->file = lx_host_file_open(file->path,
                                       call->flags | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE,
                                       call->attributes & WIN32_FILE_ATTRIBUTE_READONLY, &error);
        created = file->file != NULL;
        if (!file->file && error == LX_HOST_FS_EXISTS && call->disposition != WIN32_CREATE_NEW)
            file->file = lx_host_file_open(
                file->path,
                call->flags |
                    (call->disposition == WIN32_CREATE_ALWAYS ? LX_HOST_FILE_TRUNCATE : 0),
                0, &error);
    }
    else
        file->file = lx_host_file_open(
            file->path,
            call->flags |
                (call->disposition == WIN32_TRUNCATE_EXISTING ? LX_HOST_FILE_TRUNCATE : 0),
            0, &error);
    if (!file->file)
    {
        open32SetError(file_error(error));
        return UINT32_MAX;
    }
    LxHostFileInfo info;
    error = lx_host_file_info(file->file, &info);
    if (error || info.kind == LX_HOST_FILE_DIRECTORY)
    {
        open32SetError(error ? file_error(error) : ERROR_ACCESS_DENIED);
        return UINT32_MAX;
    }
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_FILE, file, destroy_file);
    if (!handle)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return UINT32_MAX;
    }
    file->handle = handle;
    file->next = files;
    files = file;
    call->file = NULL;
    open32SetError(!created && (call->disposition == WIN32_CREATE_ALWAYS ||
                                call->disposition == WIN32_OPEN_ALWAYS)
                       ? ERROR_ALREADY_EXISTS
                       : ERROR_SUCCESS);
    return handle;
}

ULONG CreateFile(PSZ name, ULONG access, ULONG share, PVOID security, ULONG disposition,
                 ULONG attributes, ULONG template)
{
    TRACE_NATIVE("CreateFile('%s', %x, %x, %p, %u, %x, %u)", name ? name : "", (uint)access,
                 (uint)share, security, (uint)disposition, (uint)attributes, (uint)template);
    const ULONG supported = CREATE_FILE_ATTRIBUTES | WIN32_FILE_FLAG_WRITE_THROUGH |
                            WIN32_FILE_FLAG_RANDOM_ACCESS | WIN32_FILE_FLAG_SEQUENTIAL_SCAN;
    if (!name || !*name || disposition < WIN32_CREATE_NEW ||
        disposition > WIN32_TRUNCATE_EXISTING || (share & ~(unsigned)CREATE_FILE_SHARE_MODES) ||
        (access & ~(WIN32_GENERIC_READ | WIN32_GENERIC_WRITE)))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return UINT32_MAX;
    }
    if ((attributes & ~supported) || template)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return UINT32_MAX;
    }
    if (security &&
        (lx_guest_load_u32((uint8 *)security + WIN32_SECURITY_ATTRIBUTES_LENGTH) !=
             WIN32_SECURITY_ATTRIBUTES_SIZE ||
         lx_guest_load_u32((uint8 *)security + WIN32_SECURITY_ATTRIBUTES_SECURITY_DESCRIPTOR)))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return UINT32_MAX;
    }
    if (disposition == WIN32_TRUNCATE_EXISTING && !(access & WIN32_GENERIC_WRITE))
    {
        open32SetError(ERROR_ACCESS_DENIED);
        return UINT32_MAX;
    }
    uint32 error = 0;
    char *path = GProcessState.makeUnixPath ? GProcessState.makeUnixPath(name, &error) : NULL;
    if (!path)
    {
        open32SetError(error ? error : ERROR_PATH_NOT_FOUND);
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return UINT32_MAX;
    }
    file->path = path;
    file->share = share;
    /* Access is kept as FILE_SHARE_* bits, which compare directly with the share modes
       of other handles and are also the host's read and write open flags. */
    file->access = (access & WIN32_GENERIC_READ ? WIN32_FILE_SHARE_READ : 0) |
                   (access & WIN32_GENERIC_WRITE ? WIN32_FILE_SHARE_WRITE : 0);
    unsigned flags = file->access ? file->access : LX_HOST_FILE_READ;
    if (attributes & WIN32_FILE_FLAG_WRITE_THROUGH)
        flags |= LX_HOST_FILE_SYNC;
    if (!security ||
        !lx_guest_load_u32((uint8 *)security + WIN32_SECURITY_ATTRIBUTES_INHERIT_HANDLE))
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
        if (!(file->access & (writing ? WIN32_FILE_SHARE_WRITE : WIN32_FILE_SHARE_READ)))
        {
            open32SetError(ERROR_ACCESS_DENIED);
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
            open32SetError(ERROR_LOCK_VIOLATION);
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
        if (!(file->access & WIN32_FILE_SHARE_WRITE))
        {
            open32SetError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        error = lx_host_file_seek(file->file, 0, SEEK_CUR, &call->position);
        if (!error)
            error = lx_host_file_resize(file->file, call->position);
    }
    else if (call->operation == FILE_FLUSH)
        error = lx_host_file_flush(file->file);
    else if (call->operation == FILE_INFO)
        error = lx_host_file_info(file->file, &call->info);
    else if (call->operation == FILE_LOCK || call->operation == FILE_UNLOCK)
    {
        if (!(file->access & WIN32_FILE_SHARE_WRITE))
        {
            open32SetError(file->access ? ERROR_NOT_SUPPORTED : ERROR_ACCESS_DENIED);
            return FALSE;
        }
        lx_host_monitor_lock(monitor);
        call->locked = TRUE;
        if (call->operation == FILE_LOCK)
        {
            if (range_busy(file, call->position, call->length, TRUE))
            {
                open32SetError(ERROR_LOCK_VIOLATION);
                return FALSE;
            }
            FileRange *range = calloc(1, sizeof *range);
            if (!range)
            {
                open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
                open32SetError(ERROR_NOT_LOCKED);
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
            open32SetError(ERROR_LOCK_VIOLATION);
            return FALSE;
        }
    }
    if (call->operation == FILE_SEEK && error == LX_HOST_FS_INVALID)
    {
        open32SetError(ERROR_NEGATIVE_SEEK);
        return FALSE;
    }
    return result_error(error);
}

static BOOL invoke(ULONG handle, FileCall *call)
{
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FILE, &call->borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
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
    if (origin > WIN32_FILE_END)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
    open32SetError(ERROR_SUCCESS);
    return (ULONG)call.position;
}

ULONG GetFileSize(ULONG handle, PULONG high)
{
    FileCall call = {.operation = FILE_INFO};
    if (!invoke(handle, &call))
        return UINT32_MAX;
    if (high)
        *high = (ULONG)(call.info.size >> 32);
    open32SetError(ERROR_SUCCESS);
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
    return call.info.kind == LX_HOST_FILE_PIPE     ? WIN32_FILE_TYPE_PIPE
           : call.info.kind == LX_HOST_FILE_DEVICE ? WIN32_FILE_TYPE_CHAR
                                                   : WIN32_FILE_TYPE_DISK;
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
        open32SetError(ERROR_INVALID_PARAMETER);
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

/* FILETIME stays little endian at the ABI boundary. Host precision is one second. */
static void filetime_store(PVOID output, int64_t seconds)
{
    if (!output)
        return;
    uint64_t ticks =
        seconds < -FILETIME_UNIX_EPOCH_SECONDS
            ? 0
            : ((uint64_t)(seconds + FILETIME_UNIX_EPOCH_SECONDS) * (uint64_t)TICKS_PER_SECOND);
    lx_guest_store_u32((uint8 *)output + WIN32_FILETIME_LOW, (ULONG)ticks);
    lx_guest_store_u32((uint8 *)output + WIN32_FILETIME_HIGH, (ULONG)(ticks >> 32));
}

static BOOL filetime_seconds(PVOID input, int64_t *seconds)
{
    if (!input)
    {
        *seconds = INT64_MIN;
        return TRUE;
    }
    uint64_t ticks = lx_guest_load_u32((uint8 *)input + WIN32_FILETIME_LOW) |
                     (uint64_t)lx_guest_load_u32((uint8 *)input + WIN32_FILETIME_HIGH) << 32;
    if (ticks == UINT64_MAX)
    {
        *seconds = INT64_MIN;
        return TRUE;
    }
    if (ticks > INT64_MAX)
        return FALSE;
    *seconds = (int64_t)(ticks / (uint64_t)TICKS_PER_SECOND) - FILETIME_UNIX_EPOCH_SECONDS;
    return TRUE;
}

BOOL GetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FILE, &borrow))
    {
        typedef APIRET(OS2API * Query)(ULONG, ULONG, PVOID, ULONG);
        Query query = (Query)open32DosService("DosQueryFileInfo");
        /* A plain DOS handle from the _l calls: its stamps are local DOS times. */
        uint8 status[sizeof(FILESTATUS3)];
        ULONG error =
            query ? query(handle, FIL_STANDARD, status, sizeof status) : ERROR_CALL_NOT_IMPLEMENTED;
        if (!error)
        {
            PVOID outputs[] = {creation, access, write};
            for (unsigned i = 0; i < FILE_STAMP_COUNT; i++)
                if (outputs[i])
                {
                    uint8 local[WIN32_FILETIME_SIZE];
                    if (!DosDateTimeToFileTime(
                            lx_guest_load_u16(status + i * FILE_STAMP_BYTES),
                            lx_guest_load_u16(status + i * FILE_STAMP_BYTES + FILE_STAMP_TIME),
                            local) ||
                        !LocalFileTimeToFileTime(local, outputs[i]))
                        error = ERROR_INVALID_PARAMETER;
                }
        }
        if (error)
            open32SetError(error);
        return !error;
    }
    Open32File *file = borrow.resource;
    LxHostFileInfo info;
    LxHostFsError error = lx_host_file_info(file->file, &info);
    if (!error)
    {
        /* A zero FILETIME marks a host without a recorded creation time. */
        if (creation && info.created)
            filetime_store(creation, info.created);
        else if (creation)
            memset(creation, 0, WIN32_FILETIME_SIZE);
        filetime_store(access, info.accessed);
        filetime_store(write, info.modified);
    }
    lx_handle_release(&borrow);
    return result_error(error);
}

BOOL SetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write)
{
    int64_t btime, atime, mtime;
    if (!filetime_seconds(creation, &btime) || !filetime_seconds(access, &atime) ||
        !filetime_seconds(write, &mtime))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FILE, &borrow))
    {
        typedef APIRET(OS2API * Query)(ULONG, ULONG, PVOID, ULONG);
        typedef APIRET(OS2API * Set)(ULONG, ULONG, PVOID, ULONG);
        Query query = (Query)open32DosService("DosQueryFileInfo");
        Set set = (Set)open32DosService("DosSetFileInfo");
        uint8 status[sizeof(FILESTATUS3)];
        ULONG error = !query || !set ? ERROR_CALL_NOT_IMPLEMENTED
                                     : query(handle, FIL_STANDARD, status, sizeof status);
        PVOID inputs[] = {creation, access, write};
        for (unsigned i = 0; !error && i < FILE_STAMP_COUNT; i++)
            if (inputs[i])
            {
                uint8 local[WIN32_FILETIME_SIZE];
                if (!FileTimeToLocalFileTime(inputs[i], local) ||
                    !FileTimeToDosDateTime(local, status + i * FILE_STAMP_BYTES,
                                           status + i * FILE_STAMP_BYTES + FILE_STAMP_TIME))
                    error = ERROR_INVALID_PARAMETER;
            }
        if (!error)
            error = set(handle, FIL_STANDARD, status, sizeof status);
        if (error)
            open32SetError(error);
        return !error;
    }
    Open32File *file = borrow.resource;
    ULONG error = !(file->access & (unsigned)WIN32_FILE_SHARE_WRITE) ? ERROR_ACCESS_DENIED : 0;
    if (!error)
    {
        LxHostFsError result = lx_host_file_stamps(file->file, btime, atime, mtime);
        error = result == LX_HOST_FS_INVALID ? ERROR_NOT_SUPPORTED : file_error(result);
    }
    lx_handle_release(&borrow);
    if (error)
        open32SetError(error);
    return !error;
}
