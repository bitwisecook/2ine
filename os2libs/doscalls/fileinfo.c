/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

void unixTimeToOs2(const int64_t seconds, FDATE *date, FTIME *time)
{
    LxHostCalendar value;
    if (!lx_host_calendar(seconds, 0, &value))
    {
        memset(date, 0, sizeof *date);
        memset(time, 0, sizeof *time);
        return;
    }
    date->day = value.day;
    date->month = value.month;
    date->year = value.year < 1980 ? 0 : value.year > 2107 ? 127 : value.year - 1980;
    time->twosecs = value.second / 2;
    time->minutes = value.minute;
    time->hours = value.hour;
}

void storeDateTime16(uint8 *data, FDATE date, FTIME time)
{
    lx_guest_store_u16(data, date.day | date.month << 5 | date.year << 9);
    lx_guest_store_u16(data + 2, time.twosecs | time.minutes << 5 | time.hours << 11);
}

void initFileCreationDateTime(FDATE *fdate, FTIME *ftime)
{
    /* Use the write date for creation time; zero dates can trigger FAT-only application behavior. */
    if (fdate)
    {
        fdate->day = 1;
        fdate->month = 1;
        fdate->year = 0;
    }

    if (ftime)
    {
        ftime->twosecs = 0;
        ftime->minutes = 0;
        ftime->hours = 0;
    }
}

APIRET queryFileInfoStandardFromStat(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FILESTATUS3 value;
    FILESTATUS3 *st = &value;
    memset(st, '\0', sizeof(*st));

    initFileCreationDateTime(&st->fdateCreation, &st->ftimeCreation);
    unixTimeToOs2(statbuf->accessed, &st->fdateLastAccess, &st->ftimeLastAccess);
    unixTimeToOs2(statbuf->modified, &st->fdateLastWrite, &st->ftimeLastWrite);

    st->cbFile = (ULONG)statbuf->size;
    st->cbFileAlloc = (ULONG)statbuf->allocated;

    if (statbuf->kind == LX_HOST_FILE_DIRECTORY)
        st->attrFile |= FILE_DIRECTORY;
    if (statbuf->readonly)
        st->attrFile |= FILE_READONLY;

    /* Older 32-bit C runtimes request the 22-byte FILESTATUS prefix. */
    memcpy(pInfoBuf, st, cbInfoBuf < sizeof *st ? cbInfoBuf : sizeof *st);
    return NO_ERROR;
}

APIRET queryPathInfoStandard(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < 22)
        return ERROR_BUFFER_OVERFLOW;

    LxHostFileInfo statbuf;
    LxHostFsError error = lx_host_path_info(unixPath, &statbuf);
    if (error == LX_HOST_FS_MISSING)
    {
        char *parent = strdup(*unixPath ? unixPath : ".");
        if (!parent)
            return ERROR_NOT_ENOUGH_MEMORY;
        char *slash = strrchr(parent, '/'), *backslash = strrchr(parent, '\\');
        if (backslash && (!slash || backslash > slash))
            slash = backslash;
        if (!slash)
            strcpy(parent, ".");
        else if (slash == parent || (slash == parent + 2 && parent[1] == ':'))
            slash[1] = 0;
        else
            *slash = 0;
        LxHostFileInfo directory;
        LxHostFsError parent_error = lx_host_path_info(parent, &directory);
        free(parent);
        if (parent_error == LX_HOST_FS_MISSING || parent_error == LX_HOST_FS_NOT_DIRECTORY ||
            (parent_error == LX_HOST_FS_OK && directory.kind != LX_HOST_FILE_DIRECTORY))
            return ERROR_PATH_NOT_FOUND;
        return parent_error ? fileError(parent_error) : ERROR_FILE_NOT_FOUND;
    }
    if (error)
        return fileError(error);
    return queryFileInfoStandardFromStat(&statbuf, pInfoBuf, cbInfoBuf);
}

APIRET queryPathInfoEaSize(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof(FILESTATUS4))
        return ERROR_BUFFER_OVERFLOW;
    const APIRET rc = queryPathInfoStandard(unixPath, pInfoBuf, cbInfoBuf);
    if (rc != NO_ERROR)
        return rc;

    FILESTATUS4 *st = (FILESTATUS4 *)pInfoBuf;
    LxHostFsError error;
    LxHostFile *file = lx_host_file_open(unixPath, LX_HOST_FILE_READ, 0, &error);
    if (!file)
        return fileError(error);
    size_t size = 0;
    error = lx_host_file_metadata_get(file, NULL, 0, &size);
    lx_host_file_close(file);
    if (error && error != LX_HOST_FS_INVALID)
        return fileError(error);
    st->cbList = size ? size : 4;
    return NO_ERROR;
}

APIRET queryPathInfoFullName(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    char *guest = lxHostPathToGuest(unixPath);
    if (!guest)
        return ERROR_PATH_NOT_FOUND;
    const size_t required = strlen(guest) + 1;
    if (required > cbInfoBuf)
    {
        free(guest);
        return ERROR_BUFFER_OVERFLOW;
    }
    memcpy(pInfoBuf, guest, required);
    free(guest);
    return NO_ERROR;
}

APIRET queryPathInfoEasFromList(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FIXME("write me");
    return ERROR_INVALID_LEVEL;
}

APIRET queryPathInfo(PSZ unixPath, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    switch (ulInfoLevel)
    {
        case FIL_STANDARD:
            return queryPathInfoStandard(unixPath, pInfoBuf, cbInfoBuf);
        case FIL_QUERYEASIZE:
            return queryPathInfoEaSize(unixPath, pInfoBuf, cbInfoBuf);
        case FIL_QUERYEASFROMLIST:
            return queryPathInfoEasFromList(unixPath, pInfoBuf, cbInfoBuf);

        /* Info level 7 returns a case-corrected full path through the regular path resolver. */
        case 7:
        case FIL_QUERYFULLNAME:
            return queryPathInfoFullName(unixPath, pInfoBuf, cbInfoBuf);
        default:
            break;
    }

    return ERROR_INVALID_LEVEL;
}

APIRET DosQueryPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosQueryPathInfo('%s', %u, %p, %u)", pszPathName, (uint)ulInfoLevel, pInfoBuf,
                 (uint)cbInfoBuf);

    APIRET err = NO_ERROR;
    char *unixPath = makeUnixPath(pszPathName, &err);
    if (!unixPath)
        return err;
    err = queryPathInfo(unixPath, ulInfoLevel, pInfoBuf, cbInfoBuf);
    TRACE_RET("DosQueryPathInfo('%s', level=%u, cb=%u) -> %u", pszPathName, (uint)ulInfoLevel,
              (uint)cbInfoBuf, (uint)err);
    free(unixPath);
    return err;
}

APIRET queryFileInfoStandard(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < 22)
        return ERROR_BUFFER_OVERFLOW;
    return queryFileInfoStandardFromStat(statbuf, pInfoBuf, cbInfoBuf);
}

APIRET queryFileInfoEaSize(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof(FILESTATUS4))
        return ERROR_BUFFER_OVERFLOW;
    const APIRET rc = queryFileInfoStandard(statbuf, pInfoBuf, cbInfoBuf);
    if (rc != NO_ERROR)
        return rc;

    FILESTATUS4 *st = (FILESTATUS4 *)pInfoBuf;
    st->cbList = 4;
    return NO_ERROR;
}

APIRET queryFileInfoEasFromList(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FIXME("write me");
    return ERROR_INVALID_LEVEL;
}

APIRET DosQueryFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosQueryFileInfo(%u, %u, %p, %u)", (uint)hf, (uint)ulInfoLevel, pInfo,
                 (uint)cbInfoBuf);

    LxHandleBorrow file;
    if (!borrowFile(hf, &file))
        return ERROR_INVALID_HANDLE;
    LxHostFileInfo statbuf;
    LxHostFsError error = lx_host_file_info(file.resource, &statbuf);
    size_t ea_size = 0;
    if (!error && ulInfoLevel == FIL_QUERYEASIZE)
    {
        error = lx_host_file_metadata_get(file.resource, NULL, 0, &ea_size);
        if (error == LX_HOST_FS_INVALID)
            error = LX_HOST_FS_OK;
    }
    lx_handle_release(&file);
    if (error)
        return fileError(error);

    APIRET rc;
    switch (ulInfoLevel)
    {
        case FIL_STANDARD:
            rc = queryFileInfoStandard(&statbuf, pInfo, cbInfoBuf);
            break;
        case FIL_QUERYEASIZE:
            rc = queryFileInfoEaSize(&statbuf, pInfo, cbInfoBuf);
            if (!rc)
                ((FILESTATUS4 *)pInfo)->cbList = ea_size ? ea_size : 4;
            break;
        case FIL_QUERYEASFROMLIST:
            rc = queryFileInfoEasFromList(&statbuf, pInfo, cbInfoBuf);
            break;
        default:
            rc = ERROR_INVALID_LEVEL;
            break;
    }

    TRACE_RET("DosQueryFileInfo(%u, level=%u, cb=%u) -> %u size=%ld", (uint)hf, (uint)ulInfoLevel,
              (uint)cbInfoBuf, (uint)rc, (long)statbuf.size);
    return rc;
}

APIRET DosSetPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf,
                      ULONG flOptions)
{
    TRACE_NATIVE("DosSetPathInfo('%s', %u, %p, %u, %u)", pszPathName, (uint)ulInfoLevel, pInfoBuf,
                 (uint)cbInfoBuf, (uint)flOptions);
    if (ulInfoLevel != FIL_STANDARD)
        return ERROR_INVALID_LEVEL;
    if (!pInfoBuf || cbInfoBuf < sizeof(FILESTATUS3))
        return ERROR_BUFFER_OVERFLOW;
    APIRET error = NO_ERROR;
    char *path = makeUnixPath(pszPathName, &error);
    if (!path)
        return error;
    error = fileError(
        lx_host_path_readonly(path, (((FILESTATUS3 *)pInfoBuf)->attrFile & FILE_READONLY) != 0));
    free(path);
    return error;
}

static void storeStatus16(PVOID buffer, USHORT level, const FILESTATUS4 *value)
{
    uint8 *p = buffer;
    storeDateTime16(p, value->fdateCreation, value->ftimeCreation);
    storeDateTime16(p + 4, value->fdateLastAccess, value->ftimeLastAccess);
    storeDateTime16(p + 8, value->fdateLastWrite, value->ftimeLastWrite);
    lx_guest_store_u32(p + 12, value->cbFile);
    lx_guest_store_u32(p + 16, value->cbFileAlloc);
    lx_guest_store_u16(p + 20, value->attrFile);
    if (level == FIL_QUERYEASIZE)
        lx_guest_store_u32(p + 22, value->cbList);
}

APIRET16 Dos16QPathInfo(PSZ path, USHORT level, PVOID buffer, USHORT capacity, ULONG reserved)
{
    if (reserved)
        return ERROR_INVALID_PARAMETER;
    if (level == FIL_QUERYFULLNAME)
        return (APIRET16)DosQueryPathInfo(path, level, buffer, capacity);
    unsigned size = level == FIL_STANDARD ? 22 : level == FIL_QUERYEASIZE ? 26 : 0;
    if (!size)
        return ERROR_INVALID_LEVEL;
    if (capacity < size)
        return ERROR_BUFFER_OVERFLOW;
    FILESTATUS4 value = {0};
    APIRET result = DosQueryPathInfo(path, level, &value, sizeof value);
    if (!result)
        storeStatus16(buffer, level, &value);
    return (APIRET16)result;
}

APIRET16 Dos16QFileInfo(USHORT hf, USHORT level, PVOID buffer, USHORT capacity)
{
    TRACE_NATIVE("Dos16QFileInfo(%u, %u, %p, %u)", hf, level, buffer, capacity);
    if (level == 4)
        return capacity < 12 ? ERROR_BUFFER_OVERFLOW : queryAllEas16(hf, buffer);
    const uint32 size = level == FIL_STANDARD ? 22 : level == FIL_QUERYEASIZE ? 26 : 0;
    if (!size)
        return ERROR_INVALID_LEVEL;
    if (capacity < size)
        return ERROR_BUFFER_OVERFLOW;
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, buffer, size, LX_GUEST_WRITE, &address))
        return ERROR_INVALID_PARAMETER;
    FILESTATUS4 value = {0};
    const APIRET result = DosQueryFileInfo(hf, level, &value, sizeof value);
    if (!result)
        storeStatus16(buffer, level, &value);
    return result;
}

APIRET16 Dos16SetFileInfo(USHORT hf, USHORT level, PVOID buffer, USHORT capacity)
{
    if (level == 2)
        return capacity < 12 ? ERROR_BUFFER_OVERFLOW : applyEas16(hf, buffer);
    if (level != FIL_STANDARD)
        return ERROR_INVALID_LEVEL;
    if (capacity < 22)
        return ERROR_BUFFER_OVERFLOW;
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, buffer, 22, LX_GUEST_READ, &address))
        return ERROR_INVALID_PARAMETER;
    FILESTATUS3 value = {0};
    /* The current file-info setter applies the permission attribute. */
    value.attrFile = lx_guest_load_u16((uint8 *)buffer + 20);
    return DosSetFileInfo(hf, level, &value, sizeof value);
}

APIRET DosSetFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosSetFileInfo(%u, %u, %p, %u)", (uint)hf, (uint)ulInfoLevel, pInfo,
                 (uint)cbInfoBuf);

    if (ulInfoLevel != FIL_STANDARD)
        return ERROR_INVALID_LEVEL;
    if (!pInfo || cbInfoBuf < sizeof(FILESTATUS3))
        return ERROR_BUFFER_OVERFLOW;
    LxHandleBorrow file;
    if (!borrowFile(hf, &file))
        return ERROR_INVALID_HANDLE;
    const FILESTATUS3 *info = pInfo;
    const LxHostFsError error =
        lx_host_file_readonly(file.resource, (info->attrFile & FILE_READONLY) != 0);
    lx_handle_release(&file);
    return fileError(error);
}
