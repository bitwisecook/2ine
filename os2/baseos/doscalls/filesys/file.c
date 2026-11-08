/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

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
        error =
            lx_host_file_attributes(file, attributes & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED));
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

APIRET16 Dos16Delete(PSZ pszFile, ULONG res)
{
    TRACE_NATIVE("Dos16Delete('%s', %u)", pszFile, (uint)res);
    if (getenv("TWOINE_KEEP_TEMPS"))
        return NO_ERROR;
    return DosDelete_implementation(pszFile);
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
