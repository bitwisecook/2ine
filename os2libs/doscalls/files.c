/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

static USHORT write_verify;
APIRET fileError(LxHostFsError error)
{
    switch (error)
    {
        case LX_HOST_FS_OK:
            return NO_ERROR;
        case LX_HOST_FS_MISSING:
            return ERROR_FILE_NOT_FOUND;
        case LX_HOST_FS_NOT_READY:
            return ERROR_NOT_READY;
        case LX_HOST_FS_NOT_DIRECTORY:
            return ERROR_PATH_NOT_FOUND;
        case LX_HOST_FS_NOT_EMPTY:
            return ERROR_DIR_NOT_EMPTY;
        case LX_HOST_FS_EXISTS:
            return ERROR_FILE_EXISTS;
        case LX_HOST_FS_NO_MEMORY:
            return ERROR_NOT_ENOUGH_MEMORY;
        case LX_HOST_FS_TOO_MANY_FILES:
            return ERROR_TOO_MANY_OPEN_FILES;
        case LX_HOST_FS_FULL:
            return ERROR_DISK_FULL;
        case LX_HOST_FS_NAME_LONG:
            return ERROR_FILENAME_EXCED_RANGE;
        case LX_HOST_FS_CROSS_DEVICE:
            return ERROR_NOT_SAME_DEVICE;
        case LX_HOST_FS_INVALID:
            return ERROR_INVALID_PARAMETER;
        case LX_HOST_FS_IO:
            return ERROR_INVALID_FUNCTION;
        default:
            return ERROR_ACCESS_DENIED;
    }
}

void destroyFileResource(void *value)
{
    lx_host_file_close(value);
}

int initFileInfo(LxHostFile *file, HFileInfo *info, ULONG flags)
{
    memset(info, 0, sizeof *info);
    LxHostFileInfo description;
    if (!file || lx_host_file_info(file, &description) != LX_HOST_FS_OK)
    {
        lx_host_file_close(file);
        return 0;
    }
    info->handle = lx_handle_create(LX_HANDLE_FILE, file, destroyFileResource);
    if (!info->handle)
    {
        lx_host_file_close(file);
        return 0;
    }
    info->type = description.kind == LX_HOST_FILE_REGULAR  ? 0
                 : description.kind == LX_HOST_FILE_DEVICE ? 1
                                                           : 2;
    info->attr = info->type == 1 ? DAW_STDIN | DAW_STDOUT | DAW_LEVEL1 | DAW_CHARACTER : 0;
    info->flags = flags;
    return 1;
}

int borrowFile(HFILE id, LxHandleBorrow *object)
{
    grabLock(&GMutexDosCalls);
    const int ok = id < MaxHFiles && HFiles[id].handle > 1 &&
                   lx_handle_borrow(HFiles[id].handle, LX_HANDLE_FILE, object);
    ungrabLock(&GMutexDosCalls);
    return ok;
}

APIRET DosSetFileLocks(HFILE id, PFILELOCK unlock, PFILELOCK lock, ULONG timeout, ULONG flags)
{
    TRACE_NATIVE("DosSetFileLocks(%u, %p, %p, %u, %u)", (uint)id, unlock, lock, (uint)timeout,
                 (uint)flags);
    FILELOCK release = unlock ? *unlock : (FILELOCK){0}, acquire = lock ? *lock : (FILELOCK){0};
    if ((flags & ~3u) || release.lOffset < 0 || release.lRange < 0 || acquire.lOffset < 0 ||
        acquire.lRange < 0 || (uint64)release.lOffset + release.lRange > INT32_MAX ||
        (uint64)acquire.lOffset + acquire.lRange > INT32_MAX)
        return ERROR_INVALID_PARAMETER;
    if (!release.lRange && !acquire.lRange)
        return ERROR_LOCK_VIOLATION;
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
    APIRET result = NO_ERROR;
    /* The host range adapter currently implements exclusive locks. Report
       unsupported shared/atomic modes before releasing an existing range. */
    if (flags & 2)
        result = release.lOffset != acquire.lOffset || release.lRange != acquire.lRange
                     ? ERROR_LOCK_VIOLATION
                     : ERROR_ATOMIC_LOCK_NOT_SUPPORTED;
    else if (flags & 1)
        result = ERROR_NOT_SUPPORTED;
    if (!result && release.lRange)
    {
        LxHostFsError error = lx_host_file_lock_range(file.resource, (uint32)release.lOffset,
                                                      (uint32)release.lRange, 0);
        result = error == LX_HOST_FS_INVALID ? ERROR_LOCK_VIOLATION : fileError(error);
    }
    if (!result && acquire.lRange)
    {
        uint64 start = lx_host_monotonic_us();
        for (;;)
        {
            LxHostFsError error = lx_host_file_lock_range(file.resource, (uint32)acquire.lOffset,
                                                          (uint32)acquire.lRange, 1);
            if (error != LX_HOST_FS_BUSY)
            {
                result = fileError(error);
                break;
            }
            uint64 elapsed = (lx_host_monotonic_us() - start) / 1000;
            if (timeout != UINT32_MAX && elapsed >= timeout)
            {
                result = ERROR_LOCK_VIOLATION;
                break;
            }
            uint32 remaining = timeout == UINT32_MAX ? 10 : (uint32)(timeout - elapsed);
            lx_host_sleep_ms(remaining < 10 ? remaining : 10);
        }
    }
    lx_handle_release(&file);
    return result;
}

static APIRET fileRangeAccess(LxHostFile *file, ULONG size)
{
    LxHostFileInfo info;
    LxHostFsError error = lx_host_file_info(file, &info);
    if (error || info.kind != LX_HOST_FILE_REGULAR || !size)
        return fileError(error);
    uint64 offset = 0;
    error = lx_host_file_seek(file, 0, FILE_CURRENT, &offset);
    int conflict = 0;
    if (!error)
        error = lx_host_file_range_conflict(file, offset, size, &conflict);
    return error ? fileError(error) : conflict ? ERROR_LOCK_VIOLATION : NO_ERROR;
}

APIRET DosWrite_implementation(HFILE id, PVOID data, ULONG size, PULONG actual)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
    APIRET access = fileRangeAccess(file.resource, size);
    if (access)
    {
        if (actual)
            *actual = 0;
        lx_handle_release(&file);
        return access;
    }
    size_t count = 0;
    LxHostFsError error;
    error = (LxHostFsError)guardedResource(RESOURCE_WRITE, id, size, &file, data, &count);
    if (actual)
        *actual = (ULONG)count;
    return fileError(error);
}

APIRET DosWrite(HFILE h, PVOID buf, ULONG buflen, PULONG actual)
{
    TRACE_NATIVE("DosWrite(%u, %p, %u, %p)", (uint)h, buf, (uint)buflen, actual);
    return DosWrite_implementation(h, buf, buflen, actual);
}

APIRET DosQueryHType_implementation(HFILE hFile, PULONG pType, PULONG pAttr)
{
    APIRET retval = NO_ERROR;

    grabLock(&GMutexDosCalls);
    if ((hFile >= MaxHFiles) || (HFiles[hFile].handle <= 1))
        retval = ERROR_INVALID_HANDLE;
    else
    {

        if (pType)
            *pType = HFiles[hFile].type;
        if (pAttr)
            *pAttr = HFiles[hFile].attr;
    }
    ungrabLock(&GMutexDosCalls);

    return retval;
}

APIRET DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr)
{
    TRACE_NATIVE("DosQueryHType(%u, %p, %p)", (uint)hFile, pType, pAttr);
    return DosQueryHType_implementation(hFile, pType, pAttr);
}

APIRET doDosOpen(PSZ name, PHFILE output, PULONG action, LONGLONG size, ULONG attributes,
                 ULONG open_flags, ULONG open_mode, PEAOP2 extended)
{
    if (!name || !output || size < 0 || extended || (attributes & FILE_DIRECTORY) ||
        (open_flags & 0xfffffe00u) || (open_mode & 0xffff0000u))
        return ERROR_INVALID_PARAMETER;
    if (!*name)
        return ERROR_INVALID_NAME;
    if (open_flags & OPEN_FLAGS_DASD)
        return ERROR_OPEN_FAILED;
    unsigned flags;
    switch (open_mode & 3)
    {
        case OPEN_ACCESS_READONLY:
            flags = LX_HOST_FILE_READ;
            break;
        case OPEN_ACCESS_WRITEONLY:
            flags = LX_HOST_FILE_WRITE;
            break;
        case OPEN_ACCESS_READWRITE:
            flags = LX_HOST_FILE_READ | LX_HOST_FILE_WRITE;
            break;
        default:
            return ERROR_INVALID_PARAMETER;
    }
    const ULONG existing = open_flags & 15, missing = open_flags & 0xf0;
    if (existing > OPEN_ACTION_REPLACE_IF_EXISTS ||
        (missing != OPEN_ACTION_FAIL_IF_NEW && missing != OPEN_ACTION_CREATE_IF_NEW))
        return ERROR_INVALID_PARAMETER;
    if (!(flags & LX_HOST_FILE_WRITE) &&
        (size || missing == OPEN_ACTION_CREATE_IF_NEW || existing == OPEN_ACTION_REPLACE_IF_EXISTS))
        return ERROR_INVALID_PARAMETER;
    if (missing == OPEN_ACTION_CREATE_IF_NEW || existing == OPEN_ACTION_FAIL_IF_EXISTS)
        flags |= LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE;
    if (existing == OPEN_ACTION_REPLACE_IF_EXISTS)
        flags |= LX_HOST_FILE_TRUNCATE;
    if (open_mode & OPEN_FLAGS_WRITE_THROUGH)
        flags |= LX_HOST_FILE_SYNC;
    if (open_mode & OPEN_FLAGS_NOINHERIT)
        flags |= LX_HOST_FILE_NOINHERIT;
    APIRET result;
    char *path = makeUnixPath(name, &result);
    if (!path)
        return result;
    grabLock(&GMutexDosCalls);
    HFILE id;
    for (id = 0; id < MaxHFiles && HFiles[id].handle; id++)
    {
    }
    if (id == MaxHFiles)
    {
        ungrabLock(&GMutexDosCalls);
        free(path);
        return ERROR_TOO_MANY_OPEN_FILES;
    }
    HFiles[id].handle = 1;
    ungrabLock(&GMutexDosCalls);
    LxHostFsError error;
    LxHostFile *file = lx_host_file_open(path, flags, (attributes & FILE_READONLY) != 0, &error);
    int created = file && (flags & LX_HOST_FILE_EXCLUSIVE);
    if (!file && error == LX_HOST_FS_EXISTS && missing == OPEN_ACTION_CREATE_IF_NEW &&
        existing != OPEN_ACTION_FAIL_IF_EXISTS)
        file = lx_host_file_open(path, flags & ~LX_HOST_FILE_EXCLUSIVE,
                                 (attributes & FILE_READONLY) != 0, &error);
    if (created && (attributes & (FILE_HIDDEN | FILE_SYSTEM)))
    {
        error = lx_host_file_attributes(
            file, attributes & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED));
        if (error)
        {
            lx_host_file_close(file);
            file = NULL;
            lx_host_path_remove(path, 0);
        }
    }
    free(path);
    if (file)
    {
        LxHostFileInfo description;
        if (lx_host_file_info(file, &description) == LX_HOST_FS_OK &&
            description.kind == LX_HOST_FILE_DIRECTORY)
        {
            lx_host_file_close(file);
            file = NULL;
            error = LX_HOST_FS_IS_DIRECTORY;
        }
    }
    if (file && size && (created || existing == OPEN_ACTION_REPLACE_IF_EXISTS))
    {
        error = lx_host_file_resize(file, (uint64)size);
        if (error)
        {
            lx_host_file_close(file);
            file = NULL;
        }
    }
    HFileInfo info;
    result = file ? (initFileInfo(file, &info, open_mode) ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY)
                  : fileError(error);
    grabLock(&GMutexDosCalls);
    if (result)
        memset(&HFiles[id], 0, sizeof HFiles[id]);
    else
        HFiles[id] = info;
    ungrabLock(&GMutexDosCalls);
    if (!result)
    {
        *output = id;
        if (action)
            *action = created                                     ? FILE_CREATED
                      : existing == OPEN_ACTION_REPLACE_IF_EXISTS ? FILE_TRUNCATED
                                                                  : FILE_EXISTED;
    }
    return result;
}

APIRET DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile, ULONG ulAttribute,
               ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosOpen('%s', %p, %p, %u, %u, %u, %u, %p)", pszFileName, pHf, pulAction,
                 (uint)cbFile, (uint)ulAttribute, (uint)fsOpenFlags, (uint)fsOpenMode, peaop2);
    const APIRET rc = doDosOpen(pszFileName, pHf, pulAction, (LONGLONG)cbFile, ulAttribute,
                                fsOpenFlags, fsOpenMode, peaop2);
    TRACE_RET("DosOpen('%s') -> %u handle=%u action=%u", pszFileName, (uint)rc,
              (uint)(pHf ? *pHf : 0), (uint)(pulAction ? *pulAction : 0));
    return rc;
}

APIRET DosSetFilePtr_implementation(HFILE id, LONG distance, ULONG origin, PULONG output)
{
    if (origin > FILE_END)
        return ERROR_INVALID_FUNCTION;
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
    uint64 position;
    const LxHostFsError error = lx_host_file_seek(file.resource, distance, origin, &position);
    lx_handle_release(&file);
    if (!error && output)
        *output = (ULONG)position;
    return error == LX_HOST_FS_INVALID ? ERROR_NEGATIVE_SEEK : fileError(error);
}

APIRET DosSetFilePtr(HFILE hFile, LONG ib, ULONG method, PULONG ibActual)
{
    TRACE_NATIVE("DosSetFilePtr(%u, %d, %u, %p)", (uint)hFile, (int)ib, (uint)method, ibActual);
    return DosSetFilePtr_implementation(hFile, ib, method, ibActual);
}

APIRET16 Dos16SetFilePtrL(USHORT id, ULONG high, ULONG low, ULONG method, PVOID position)
{
    if (method > 2 || !position)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
    uint64_t result;
    int64_t offset = (int64_t)((uint64_t)high << 32 | low);
    LxHostFsError error = lx_host_file_seek(file.resource, offset, method, &result);
    lx_handle_release(&file);
    if (!error)
    {
        lx_guest_store_u32(position, (uint32_t)result);
        lx_guest_store_u32((uint8 *)position + 4, (uint32_t)((uint64_t)result >> 32));
    }
    return (APIRET16)fileError(error);
}

APIRET DosRead_implementation(HFILE id, PVOID data, ULONG size, PULONG actual)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
    APIRET access = fileRangeAccess(file.resource, size);
    if (access)
    {
        if (actual)
            *actual = 0;
        lx_handle_release(&file);
        return access;
    }
    size_t count = 0;
    LxHostFsError error;
    error = (LxHostFsError)guardedResource(RESOURCE_READ, id, size, &file, data, &count);
    if (actual)
        *actual = (ULONG)count;
    return fileError(error);
}

APIRET DosRead(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual)
{
    TRACE_NATIVE("DosRead(%u, %p, %u, %p)", (uint)hFile, pBuffer, (uint)cbRead, pcbActual);
    return DosRead_implementation(hFile, pBuffer, cbRead, pcbActual);
}

APIRET DosClose_implementation(HFILE id)
{
    grabLock(&GMutexDosCalls);
    APIRET result = ERROR_INVALID_HANDLE;
    if (id < MaxHFiles && HFiles[id].handle > 1)
    {
        uint32 handle = HFiles[id].handle;
        memset(&HFiles[id], 0, sizeof HFiles[id]);
        result = lx_handle_close(handle, LX_HANDLE_FILE) ? NO_ERROR : ERROR_INVALID_HANDLE;
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET DosClose(HFILE hFile)
{
    TRACE_NATIVE("DosClose(%u)", (uint)hFile);
    return DosClose_implementation(hFile);
}

APIRET DosDelete_implementation(PSZ name)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    APIRET result;
    char *path = makeUnixPath(name, &result);
    if (!path)
        return result;
    result = fileError(lx_host_path_remove(path, 0));
    free(path);
    return result;
}

APIRET DosDelete(PSZ name)
{
    return DosDelete_implementation(name);
}

APIRET DosOpenL(PSZ pszFileName, PHFILE pHf, PULONG pulAction, LONGLONG cbFile, ULONG ulAttribute,
                ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosOpenL('%s', %p, %p, %llu, %u, %u, %u, %p)", pszFileName, pHf, pulAction,
                 (unsigned long long)cbFile, (uint)ulAttribute, (uint)fsOpenFlags, (uint)fsOpenMode,
                 peaop2);
    return doDosOpen(pszFileName, pHf, pulAction, cbFile, ulAttribute, fsOpenFlags, fsOpenMode,
                     peaop2);
}

APIRET DosSetFileSize(HFILE id, ULONG size)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
    const LxHostFsError error = lx_host_file_resize(file.resource, size);
    lx_handle_release(&file);
    return fileError(error);
}

APIRET DosDupHandle_implementation(HFILE source, PHFILE output)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    APIRET result = NO_ERROR;
    HFILE target = *output;
    LxHandleBorrow original = {0};
    if (source >= MaxHFiles || HFiles[source].handle <= 1 ||
        !lx_handle_borrow(HFiles[source].handle, LX_HANDLE_FILE, &original))
        result = ERROR_INVALID_HANDLE;
    else
    {
        if (target == (HFILE)-1)
        {
            for (target = 0; target < MaxHFiles && HFiles[target].handle; target++)
            {
            }
            if (target == MaxHFiles)
                result = ERROR_TOO_MANY_OPEN_FILES;
        }
        else if (target >= MaxHFiles || HFiles[target].handle)
            result = ERROR_INVALID_TARGET_HANDLE;
        if (!result)
        {
            LxHostFsError error;
            LxHostFile *copy = lx_host_file_duplicate(original.resource, &error);
            if (!copy)
                result = fileError(error);
            else
            {
                HFileInfo info;
                if (!initFileInfo(copy, &info, HFiles[source].flags))
                    result = ERROR_NOT_ENOUGH_MEMORY;
                else
                {
                    HFiles[target] = info;
                    *output = target;
                }
            }
        }
    }
    lx_handle_release(&original);
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET DosDupHandle(HFILE hFile, PHFILE pHfile)
{
    TRACE_NATIVE("DosDupHandle(%u, %p)", hFile, pHfile);
    return DosDupHandle_implementation(hFile, pHfile);
}

APIRET16 Dos16QFileMode(PCHAR filepath, PUSHORT attr, ULONG res)
{
    TRACE_NATIVE("Dos16QFileMode('%s', %p, %d)", filepath, attr, res);
    APIRET err;
    char *path = makeUnixPath(filepath, &err);
    if (!path)
        return err;
    LxHostFileInfo info;
    const LxHostFsError error = lx_host_path_info(path, &info);
    free(path);
    if (error)
        return fileError(error);
    if (attr)
        *attr = (info.kind == LX_HOST_FILE_DIRECTORY ? FILE_DIRECTORY : 0) |
                (info.readonly ? FILE_READONLY : 0) |
                (info.attributes & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED));
    return NO_ERROR;
}

APIRET16 Dos16ChgFilePtr(USHORT handle, LONG distance, USHORT whence, PULONG newoffset)
{
    TRACE_NATIVE("Dos16ChgFlePtr(%u, %u, %u, %p)", (uint)handle, (uint)distance, (uint)whence,
                 newoffset);
    const APIRET16 ret =
        (APIRET16)DosSetFilePtr_implementation(handle, distance, whence, newoffset);
    TRACE_RET("Dos16ChgFilePtr(%u, %d, %u) -> %u off=%u", (uint)handle, (int)distance, (uint)whence,
              (uint)ret, (uint)(newoffset ? *newoffset : 0));
    return ret;
}

APIRET DosCopy(PSZ pszOld, PSZ pszNew, ULONG option)
{
    TRACE_NATIVE("DosCopy('%s', '%s', %u)", pszOld ? pszOld : "", pszNew ? pszNew : "",
                 (uint)option);

    if ((!pszOld) || (!pszNew))
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *oldpath = makeUnixPath(pszOld, &err);
    if (!oldpath)
        return err;
    char *newpath = makeUnixPath(pszNew, &err);
    if (!newpath)
    {
        free(oldpath);
        return err;
    }

    LxHostFileInfo target_info;
    if (lx_host_path_info(newpath, &target_info) == LX_HOST_FS_OK &&
        target_info.kind == LX_HOST_FILE_DIRECTORY)
    {
        const char *base = pszOld;
        for (const char *p = pszOld; *p; p++)
            if (*p == '/' || *p == '\\' || *p == ':')
                base = p + 1;
        size_t size = strlen(pszNew) + strlen(base) + 2;
        char *destination = malloc(size);
        if (!destination)
        {
            free(oldpath);
            free(newpath);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        snprintf(destination, size, "%s\\%s", pszNew, base);
        free(newpath);
        newpath = makeUnixPath(destination, &err);
        free(destination);
        if (!newpath)
        {
            free(oldpath);
            return err;
        }
    }
    char *source_identity = lx_host_path_canonical(oldpath);
    char *target_identity = lx_host_path_canonical(newpath);
    BOOL same = source_identity && target_identity && !strcmp(source_identity, target_identity);
    free(source_identity);
    free(target_identity);
    if (same)
    {
        free(oldpath);
        free(newpath);
        return ERROR_ACCESS_DENIED;
    }

    unsigned flags = LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE;
    if (!(option & (DCPY_EXISTING | DCPY_APPEND)))
        flags |= LX_HOST_FILE_EXCLUSIVE;
    if (!(option & DCPY_APPEND))
        flags |= LX_HOST_FILE_TRUNCATE;
    LxHostFsError error;
    LxHostFile *input = lx_host_file_open(oldpath, LX_HOST_FILE_READ, 0, &error);
    APIRET retval = input ? NO_ERROR : fileError(error);
    if (input)
    {
        LxHostFile *output = lx_host_file_open(newpath, flags, 0, &error);
        if (!output)
            retval = fileError(error);
        else
        {
            if (option & DCPY_APPEND)
                retval = fileError(lx_host_file_seek(output, 0, 2, NULL));
            char data[16384];
            while (!retval)
            {
                size_t count;
                error = lx_host_file_read(input, data, sizeof data, &count);
                if (error)
                {
                    retval = fileError(error);
                    break;
                }
                if (!count)
                    break;
                size_t written = 0;
                while (written < count)
                {
                    size_t actual;
                    error = lx_host_file_write(output, data + written, count - written, &actual);
                    if (error || !actual)
                    {
                        retval = error ? fileError(error) : ERROR_DISK_FULL;
                        break;
                    }
                    written += actual;
                }
            }
            lx_host_file_close(output);
        }
        lx_host_file_close(input);
    }

    free(newpath);
    free(oldpath);
    return retval;
}

APIRET DosMove(PSZ pszOld, PSZ pszNew)
{
    TRACE_NATIVE("DosMove('%s', '%s')", pszOld ? pszOld : "", pszNew ? pszNew : "");

    if ((!pszOld) || (!pszNew))
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *oldpath = makeUnixPath(pszOld, &err);
    if (!oldpath)
        return err;
    char *newpath = makeUnixPath(pszNew, &err);
    if (!newpath)
    {
        free(oldpath);
        return err;
    }

    APIRET retval = fileError(lx_host_path_move(oldpath, newpath));

    free(newpath);
    free(oldpath);
    return retval;
}

APIRET16 Dos16Move(PSZ pszOld, PSZ pszNew, ULONG reserved)
{
    TRACE_NATIVE("Dos16Move('%s', '%s', %u)", pszOld, pszNew, reserved);
    return reserved ? ERROR_INVALID_PARAMETER : DosMove(pszOld, pszNew);
}
APIRET DosSetRelMaxFH(PLONG request, PULONG current)
{
    grabLock(&GMutexDosCalls);
    if (request && *request > 0)
    {
        uint32 increase = (uint32)*request;
        if (increase > 0x10000u - MaxHFiles)
            increase = 0x10000u - MaxHFiles;
        HFileInfo *files =
            increase ? realloc(HFiles, sizeof *files * (MaxHFiles + increase)) : NULL;
        if (files)
        {
            memset(files + MaxHFiles, 0, sizeof *files * increase);
            HFiles = files;
            MaxHFiles += increase;
        }
        else
            increase = 0;
        *request = (LONG)increase;
    }
    if (current)
        *current = MaxHFiles;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET DosWaitNPipe(PSZ pszName, ULONG ulTimeOut)
{
    TRACE_NATIVE("DosWaitNPipe('%s', %u)", pszName ? pszName : "", (uint)ulTimeOut);

    if ((!pszName) || (!*pszName))
        return ERROR_INVALID_PARAMETER;

    /* No named-pipe provider exists, so waiting cannot make an instance available. */
    return ERROR_PIPE_NOT_CONNECTED;
}

APIRET DosForceDelete(PSZ name)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    APIRET result;
    char *path = makeUnixPath(name, &result);
    if (!path)
        return result;
    LxHostFsError error = lx_host_path_remove(path, 0);
    if (error == LX_HOST_FS_DENIED || error == LX_HOST_FS_READ_ONLY)
    {
        if (!lx_host_path_readonly(path, 0))
            error = lx_host_path_remove(path, 0);
    }
    result = fileError(error);
    free(path);
    return result;
}

APIRET DosResetBuffer(HFILE id)
{
    grabLock(&GMutexDosCalls);
    APIRET result = NO_ERROR;
    const uint32 first = id == UINT32_MAX ? 0 : id, last = id == UINT32_MAX ? MaxHFiles : id + 1;
    if (id != UINT32_MAX && (id >= MaxHFiles || HFiles[id].handle <= 1))
        result = ERROR_INVALID_HANDLE;
    else
        for (uint32 i = first; i < last; i++)
        {
            LxHandleBorrow file;
            if (HFiles[i].handle > 1 && lx_handle_borrow(HFiles[i].handle, LX_HANDLE_FILE, &file))
            {
                LxHostFileInfo info;
                /* Pipes/devices have no filesystem buffers to commit. */
                LxHostFsError error = lx_host_file_info(file.resource, &info);
                if (!error && info.kind == LX_HOST_FILE_REGULAR)
                    error = lx_host_file_flush(file.resource);
                lx_handle_release(&file);
                if (!result)
                    result = fileError(error);
            }
        }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET16 Dos16BufReset(USHORT file)
{
    return (APIRET16)DosResetBuffer(file == 0xffff ? UINT32_MAX : file);
}

APIRET16 Dos16QFHandState(USHORT file, PUSHORT state)
{
    ULONG value;
    APIRET result = DosQueryFHState(file, &value);
    if (!result)
        *state = (USHORT)value;
    return (APIRET16)result;
}

APIRET16 Dos16SetFHandState(USHORT file, USHORT state)
{
    if (state & ~0xffu)
        return ERROR_NOT_SUPPORTED;
    grabLock(&GMutexDosCalls);
    APIRET result = ERROR_INVALID_HANDLE;
    if (file < MaxHFiles && HFiles[file].handle > 1)
    {
        LxHandleBorrow object;
        if (lx_handle_borrow(HFiles[file].handle, LX_HANDLE_FILE, &object))
        {
            result =
                fileError(lx_host_file_inherit(object.resource, !(state & OPEN_FLAGS_NOINHERIT)));
            if (!result)
                HFiles[file].flags =
                    (HFiles[file].flags & ~OPEN_FLAGS_NOINHERIT) | (state & OPEN_FLAGS_NOINHERIT);
            lx_handle_release(&object);
        }
    }
    ungrabLock(&GMutexDosCalls);
    return (APIRET16)result;
}

APIRET DosQueryFHState(HFILE hFile, PULONG pMode)
{
    TRACE_NATIVE("DosQueryFHState(%u, %p)", (uint)hFile, pMode);

    APIRET retval = NO_ERROR;
    ULONG tmp = 0;
    if (!pMode)
        pMode = &tmp;

    grabLock(&GMutexDosCalls);
    if ((hFile < MaxHFiles) && (HFiles[hFile].handle > 1))
        *pMode = HFiles[hFile].flags;
    else
        retval = ERROR_INVALID_HANDLE;
    ungrabLock(&GMutexDosCalls);

    return retval;
}

APIRET DosSetMaxFH(ULONG cFH)
{
    TRACE_NATIVE("DosSetMaxFH(%u)", (uint)cFH);
    grabLock(&GMutexDosCalls);

    if (cFH < MaxHFiles || cFH > 0x10000u)
    {
        ungrabLock(&GMutexDosCalls);
        return ERROR_INVALID_PARAMETER;
    }

    if (cFH == MaxHFiles)
    {
        ungrabLock(&GMutexDosCalls);
        return NO_ERROR;
    }

    HFileInfo *info = (HFileInfo *)realloc(HFiles, sizeof(HFileInfo) * (cFH));
    if (info != NULL)
    {
        HFiles = info;
        info += MaxHFiles;
        for (ULONG i = MaxHFiles; i < cFH; i++, info++)
        {
            info->handle = 0;
            info->type = 0;
            info->attr = 0;
            info->flags = 0;
        }
        MaxHFiles = cFH;
    }

    ungrabLock(&GMutexDosCalls);

    return NO_ERROR;
}

APIRET16 Dos16DevIOCtl(PVOID pdata, PVOID pparms, USHORT fun, USHORT category, USHORT hdev)
{
    TRACE_NATIVE("Dos16DevIOCtl(%p, %p, %u, %u, %u)", pdata, pparms, (uint)fun, (uint)category,
                 (uint)hdev);
    return ERROR_INVALID_FUNCTION;
}

APIRET16 Dos16SetMaxFH(USHORT cfh)
{
    TRACE_NATIVE("Dos16SetMaxFH(%u)", (uint)cfh);
    return (APIRET16)DosSetMaxFH((ULONG)cfh);
}

APIRET16 Dos16NewSize(USHORT handle, ULONG size)
{
    return DosSetFileSize(handle, size);
}

APIRET16 Dos16QHandType(USHORT handle, PUSHORT ptype, PUSHORT pflags)
{
    TRACE_NATIVE("Dos16QHandType(%u, %p, %p)", (uint)handle, ptype, pflags);
    ULONG ultype, ulattr;
    const APIRET rc = DosQueryHType_implementation((HFILE)handle, &ultype, &ulattr);
    if (rc == NO_ERROR)
    {
        if (ptype)
            *ptype = (USHORT)ultype;
        if (pflags)
            *pflags = (USHORT)ulattr;
    }
    return (APIRET16)rc;
}

APIRET16 Dos16Write(USHORT h, PVOID buf, USHORT buflen, PUSHORT actual)
{
    TRACE_NATIVE("Dos16Write(%u, %p, %u, %p)", (uint)h, buf, (uint)buflen, actual);
    ULONG ulactual = actual ? *actual : 0;
    APIRET rc = DosWrite_implementation(h, buf, buflen, &ulactual);
    if (actual)
        *actual = (USHORT)ulactual;
    return (APIRET16)rc;
}

APIRET16 Dos16Open(PSZ pszFileName, PUSHORT pHf, PUSHORT pusAction, ULONG cbFile,
                   USHORT ulAttribute, USHORT fsOpenFlags, USHORT fsOpenMode, ULONG ulReserved)
{
    TRACE_NATIVE("Dos16Open('%s', %p, %p, %u, %u, %u, %u, %u)", pszFileName, pHf, pusAction,
                 (uint)cbFile, (uint)ulAttribute, (uint)fsOpenFlags, (uint)fsOpenMode,
                 (uint)ulReserved);
    HFILE hf = (HFILE)-1;
    ULONG action = 0;
    APIRET ret = doDosOpen(pszFileName, &hf, &action, (LONGLONG)cbFile, ulAttribute, fsOpenFlags,
                           fsOpenMode, NULL);
    if (pusAction)
        *pusAction = (USHORT)action;
    if (pHf)
        *pHf = (USHORT)hf;
    TRACE_RET("Dos16Open('%s') -> %u hf=%u action=%u", pszFileName, (uint)ret, (uint)hf,
              (uint)action);
    return (APIRET16)ret;
}

APIRET16 Dos16Open2(PSZ pszFileName, PUSHORT pHf, PUSHORT pusAction, ULONG cbFile,
                    USHORT ulAttribute, USHORT fsOpenFlags, ULONG fsOpenMode, PVOID ea,
                    ULONG reserved)
{
    TRACE_NATIVE("Dos16Open2('%s', %p, %p, %u, %u, %u, %u, %p, %u)", pszFileName, pHf, pusAction,
                 (uint)cbFile, (uint)ulAttribute, (uint)fsOpenFlags, (uint)fsOpenMode, ea,
                 (uint)reserved);
    if (reserved || !pHf || !pusAction)
        return ERROR_INVALID_PARAMETER;
    if (ea)
        lx_guest_store_u32((uint8 *)ea + 8, 0);
    HFILE handle = (HFILE)-1;
    ULONG action = 0;
    APIRET result = doDosOpen(pszFileName, &handle, &action, cbFile, ulAttribute, fsOpenFlags,
                              fsOpenMode, NULL);
    if (!result && ea && action != FILE_EXISTED)
    {
        result = applyEas16(handle, ea);
        if (result)
            DosClose_implementation(handle);
    }
    if (!result)
    {
        *pHf = (USHORT)handle;
        *pusAction = (USHORT)action;
    }
    return (APIRET16)result;
}

APIRET16 Dos16OpenL(PSZ name, PUSHORT file, PUSHORT action, ULONG high, ULONG low,
                    USHORT attributes, USHORT flags, ULONG mode, PVOID ea, ULONG reserved)
{
    TRACE_NATIVE("Dos16OpenL('%s', %p, %p, %u:%u, %u, %u, %u, %p, %u)", name, file, action,
                 (uint)high, (uint)low, (uint)attributes, (uint)flags, (uint)mode, ea,
                 (uint)reserved);
    if (!file || !action)
        return ERROR_INVALID_PARAMETER;
    if (ea)
        lx_guest_store_u32((uint8 *)ea + 8, 0);
    HFILE handle;
    ULONG taken;
    LONGLONG size = (LONGLONG)((uint64_t)high << 32 | low);
    APIRET result = doDosOpen(name, &handle, &taken, size, attributes, flags, mode, NULL);
    if (!result && ea && taken != FILE_EXISTED)
    {
        result = applyEas16(handle, ea);
        if (result)
            DosClose_implementation(handle);
    }
    if (!result)
    {
        *file = (USHORT)handle;
        *action = (USHORT)taken;
    }
    return (APIRET16)result;
}

APIRET16 Dos16Close(USHORT hFile)
{
    TRACE_NATIVE("Dos16Close(%u)", (uint)hFile);
    return DosClose_implementation(hFile);
}

APIRET16 Dos16Delete(PSZ pszFile, ULONG res)
{
    TRACE_NATIVE("Dos16Delete('%s', %u)", pszFile, (uint)res);
    if (getenv("TWOINE_KEEP_TEMPS"))
        return NO_ERROR;
    return DosDelete_implementation(pszFile);
}

APIRET16 Dos16DupHandle(USHORT hFile, PUSHORT pHfile)
{
    TRACE_NATIVE("Dos16DupHandle(%u, %p)", (uint)hFile, pHfile);
    HFILE phf = *(int16_t *)pHfile;
    APIRET ret = DosDupHandle_implementation(hFile, &phf);
    *pHfile = phf;
    return ret;
}

APIRET16 Dos16Read(USHORT hFile, PVOID pBuffer, USHORT cbRead, PUSHORT pcbActual)
{
    TRACE_NATIVE("Dos16Read(%u, %p, %u, %p)", (uint)hFile, pBuffer, (uint)cbRead, pcbActual);
    ULONG actual;
    APIRET ret = DosRead_implementation(hFile, pBuffer, cbRead, &actual);
    *pcbActual = (USHORT)actual;
    TRACE_RET("Dos16Read(%u, %u) -> %u, actual=%u", hFile, cbRead, (uint)ret, (uint)actual);
    return ret;
}

APIRET16 Dos16SetFileMode(PCHAR filepath, USHORT attr, ULONG reserved)
{
    TRACE_NATIVE("Dos16SetFileMode('%s', %u, %u)", filepath, attr, (uint)reserved);
    if (reserved ||
        (attr & ~(FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY | FILE_ARCHIVED)))
        return ERROR_INVALID_PARAMETER;
    APIRET error = NO_ERROR;
    char *path = makeUnixPath(filepath, &error);
    if (!path)
        return error;
    LxHostFileInfo info;
    const unsigned attributes = attr & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);
    LxHostFsError result = lx_host_path_info(path, &info);
    if (!result && attributes != info.attributes)
        result = lx_host_path_attributes(path, attributes);
    if (!result)
        result = lx_host_path_readonly(path, (attr & FILE_READONLY) != 0);
    error = result == LX_HOST_FS_INVALID ? ERROR_NOT_SUPPORTED : fileError(result);
    free(path);
    return error;
}

APIRET DosDevIOCtl(HFILE device, ULONG category, ULONG function, PVOID parameters,
                   ULONG parameter_max, PULONG parameter_size, PVOID data, ULONG data_max,
                   PULONG data_size)
{
    TRACE_NATIVE("DosDevIOCtl(%u, %u, %u, %p, %u, %p, %p, %u, %p)", (uint)device, (uint)category,
                 (uint)function, parameters, (uint)parameter_max, parameter_size, data,
                 (uint)data_max, data_size);
    if (device == (HFILE)-1 && category == 8 && function == 0x63)
    {
        if (!parameters || parameter_max < 2 || !data || !data_size)
            return ERROR_INVALID_PARAMETER;
        const uint8 *input = parameters;
        TRACE_NATIVE("DSK_GETDEVICEPARAMS command=%u drive=%u", input[0], input[1]);
        if (input[0] > 1 || input[1] >= 26 || !GLoaderState.disks[input[1]])
            return ERROR_INVALID_DRIVE;
        if (data_max < 36)
        {
            *data_size = 36;
            return ERROR_BUFFER_OVERFLOW;
        }
        uint64_t total, available;
        LxHostFsError error = lx_host_path_space(GLoaderState.disks[input[1]], &total, &available);
        if (error)
            return fileError(error);
        LxHostVolumeInfo volume;
        error = lx_host_path_volume(GLoaderState.disks[input[1]], &volume);
        if (error)
            return fileError(error);
        int floppy = volume.removable && !strcmp(volume.filesystem, "FAT");
        unsigned sector_bytes = volume.sector_bytes;
        unsigned track = floppy ? volume.sectors_per_track : 63;
        unsigned heads = floppy ? volume.heads : 255;
        uint64_t sectors = total / sector_bytes;
        uint64_t cylinders = track && heads ? sectors / ((uint64_t)track * heads) : 0;
        uint8 *output = data;
        memset(output, 0, 36);
        lx_guest_store_u16(output, sector_bytes);
        output[2] = volume.cluster_bytes / sector_bytes;
        lx_guest_store_u16(output + 3, volume.reserved_sectors);
        output[5] = volume.fat_count;
        lx_guest_store_u16(output + 6, volume.root_entries);
        lx_guest_store_u16(output + 8, sectors <= 65535 ? sectors : 0);
        output[10] = floppy ? volume.media : 0xf8;
        lx_guest_store_u16(output + 11, volume.fat_sectors);
        lx_guest_store_u16(output + 13, track);
        lx_guest_store_u16(output + 15, heads);
        lx_guest_store_u32(output + 17, volume.hidden_sectors);
        lx_guest_store_u32(output + 21, sectors > UINT32_MAX ? UINT32_MAX : sectors);
        lx_guest_store_u16(output + 31, cylinders > 65535 ? 65535 : cylinders);
        /* Toolkit bsedev.h: 3.5-inch = 2, fixed = 5, unknown = 7. */
        output[33] = floppy ? 2 : volume.removable ? 7 : 5;
        lx_guest_store_u16(output + 34, volume.removable ? 0 : 1);
        *data_size = 36;
        if (parameter_size)
            *parameter_size = 2;
        return NO_ERROR;
    }
    LxHandleBorrow file;
    if (!borrowFile(device, &file))
        return ERROR_INVALID_HANDLE;
    lx_handle_release(&file);
    return ERROR_INVALID_FUNCTION;
}

APIRET16 Dos16QVerify(PUSHORT enabled)
{
    if (!enabled)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    *enabled = write_verify;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}
APIRET16 Dos16SetVerify(USHORT enabled)
{
    if (enabled > 1)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    write_verify = enabled;
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}
APIRET16 Dos16MakePipe(PUSHORT reader, PUSHORT writer, USHORT size)
{
    (void)size;
    if (!reader || !writer || reader == writer)
        return ERROR_INVALID_PARAMETER;
    HFILE slots[2] = {UINT32_MAX, UINT32_MAX};
    grabLock(&GMutexDosCalls);
    unsigned count = 0;
    for (HFILE i = 0; i < MaxHFiles && i < UINT16_MAX && count < 2; i++)
        if (!HFiles[i].handle)
            slots[count++] = i;
    if (count != 2)
    {
        ungrabLock(&GMutexDosCalls);
        return ERROR_TOO_MANY_OPEN_FILES;
    }
    HFiles[slots[0]].handle = HFiles[slots[1]].handle = 1;
    ungrabLock(&GMutexDosCalls);
    LxHostFile *input = NULL, *output = NULL;
    APIRET result = fileError(lx_host_file_pipe(&input, &output));
    HFileInfo infos[2] = {{0}};
    if (!result)
    {
        if (!initFileInfo(input, &infos[0], OPEN_ACCESS_READONLY))
        {
            lx_host_file_close(output);
            result = ERROR_NOT_ENOUGH_MEMORY;
        }
        else if (!initFileInfo(output, &infos[1], OPEN_ACCESS_WRITEONLY))
        {
            lx_handle_close(infos[0].handle, LX_HANDLE_FILE);
            result = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    grabLock(&GMutexDosCalls);
    if (result)
        HFiles[slots[0]].handle = HFiles[slots[1]].handle = 0;
    else
    {
        HFiles[slots[0]] = infos[0];
        HFiles[slots[1]] = infos[1];
        *reader = (USHORT)slots[0];
        *writer = (USHORT)slots[1];
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET DosShutdown(ULONG reserved)
{
    if (reserved)
        return ERROR_INVALID_PARAMETER;
    APIRET result = NO_ERROR;
    for (HFILE id = 0; id < MaxHFiles; id++)
    {
        LxHandleBorrow file;
        if (borrowFile(id, &file))
        {
            LxHostFileInfo info;
            if (!lx_host_file_info(file.resource, &info) && info.kind == LX_HOST_FILE_REGULAR)
            {
                APIRET error = fileError(lx_host_file_flush(file.resource));
                if (error && !result)
                    result = error;
            }
            lx_handle_release(&file);
        }
    }
    return result;
}
