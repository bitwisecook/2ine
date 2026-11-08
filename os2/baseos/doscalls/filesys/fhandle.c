/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

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

APIRET16 Dos16Close(USHORT hFile)
{
    TRACE_NATIVE("Dos16Close(%u)", (uint)hFile);
    return DosClose_implementation(hFile);
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
