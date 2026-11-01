/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

APIRET fileError(LxHostFsError error)
{
    switch (error)
    {
        case LX_HOST_FS_OK:
            return NO_ERROR;
        case LX_HOST_FS_MISSING:
            return ERROR_FILE_NOT_FOUND;
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
    info->attr = DAW_STDIN | DAW_STDOUT | DAW_LEVEL1 | DAW_CHARACTER;
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

APIRET DosWrite_implementation(HFILE id, PVOID data, ULONG size, PULONG actual)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
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

APIRET DosRead_implementation(HFILE id, PVOID data, ULONG size, PULONG actual)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
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
                (info.readonly ? FILE_READONLY : 0);
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
    /* EAOP contains two far pointers and a 32-bit error offset. */
    if (ea)
    {
        const uint32 list = lx_guest_load_u32((uint8 *)ea + 4);
        lx_guest_store_u32((uint8 *)ea + 8, 0);
        if (list)
        {
            LxGuestBorrow header = {0};
            if (!lx_execution_borrow_far(GLoaderState.execution, list, 4, LX_GUEST_READ, &header))
                return ERROR_INVALID_PARAMETER;
            const uint32 size = lx_guest_load_u32(header.host);
            lx_execution_release(&header);
            if (size < 4)
                return ERROR_EA_LIST_INCONSISTENT;
            if (size > 4)
                return ERROR_EAS_NOT_SUPPORTED;
        }
    }
    HFILE handle = (HFILE)-1;
    ULONG action = 0;
    const APIRET result = doDosOpen(pszFileName, &handle, &action, cbFile, ulAttribute, fsOpenFlags,
                                    fsOpenMode, NULL);
    if (!result)
    {
        *pHf = (USHORT)handle;
        *pusAction = (USHORT)action;
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
    return ret;
}
