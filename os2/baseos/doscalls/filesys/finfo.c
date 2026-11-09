/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* File and path information: FILESTATUS queries and updates, and FDATE/FTIME conversion. */
#include "os2/baseos/doscalls/internal.h"

/* FDATE counts years from 1980 in seven bits; FTIME counts seconds in pairs. */
enum
{
    FDATE_EPOCH_YEAR = 1980,
    FDATE_YEAR_MAXIMUM = 127,
    FDATE_MONTH_SHIFT = 5,
    FDATE_YEAR_SHIFT = 9,
    FTIME_MINUTES_SHIFT = 5,
    FTIME_HOURS_SHIFT = 11
};

/* The 16-bit FILESTATUS and FILESTATUS2 records: 32-bit ones with a USHORT attrFile. */
enum
{
    FILESTATUS16_CREATION_OFFSET = 0,
    FILESTATUS16_ACCESS_OFFSET = 4,
    FILESTATUS16_WRITE_OFFSET = 8,
    FILESTATUS16_SIZE_OFFSET = 12,
    FILESTATUS16_ALLOC_OFFSET = 16,
    FILESTATUS16_ATTRIBUTES_OFFSET = 20,
    FILESTATUS16_EASIZE_OFFSET = 22,
    FILESTATUS16_SIZE = 22,
    FILESTATUS2_16_SIZE = 26
};

/* Levels the Toolkit leaves unnamed: 7 returns the case-corrected full name, and the
   16-bit level 4 query returns every EA. */
enum
{
    FIL_QUERYCASEDNAME = 7,
    FIL16_QUERYALLEAS = 4
};

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
    date->year = value.year < FDATE_EPOCH_YEAR ? 0
                 : value.year > FDATE_EPOCH_YEAR + FDATE_YEAR_MAXIMUM
                     ? FDATE_YEAR_MAXIMUM
                     : value.year - FDATE_EPOCH_YEAR;
    time->twosecs = value.second / 2;
    time->minutes = value.minute;
    time->hours = value.hour;
}

void storeDateTime16(uint8 *data, FDATE date, FTIME time)
{
    lx_guest_store_u16(data,
                       date.day | date.month << FDATE_MONTH_SHIFT | date.year << FDATE_YEAR_SHIFT);
    lx_guest_store_u16(data + sizeof(USHORT), time.twosecs | time.minutes << FTIME_MINUTES_SHIFT |
                                                  time.hours << FTIME_HOURS_SHIFT);
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

    if (statbuf->created)
        unixTimeToOs2(statbuf->created, &st->fdateCreation, &st->ftimeCreation);
    else
        initFileCreationDateTime(&st->fdateCreation, &st->ftimeCreation);
    unixTimeToOs2(statbuf->accessed, &st->fdateLastAccess, &st->ftimeLastAccess);
    unixTimeToOs2(statbuf->modified, &st->fdateLastWrite, &st->ftimeLastWrite);

    st->cbFile = (ULONG)statbuf->size;
    st->cbFileAlloc = (ULONG)statbuf->allocated;

    if (statbuf->kind == LX_HOST_FILE_DIRECTORY)
        st->attrFile |= FILE_DIRECTORY;
    if (statbuf->readonly)
        st->attrFile |= FILE_READONLY;
    st->attrFile |= statbuf->attributes & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);

    /* Older 32-bit C runtimes request only the FILESTATUS16_SIZE prefix. */
    memcpy(pInfoBuf, st, cbInfoBuf < sizeof *st ? cbInfoBuf : sizeof *st);
    return NO_ERROR;
}

APIRET queryPathInfoStandard(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < FILESTATUS16_SIZE)
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
    st->cbList = size ? size : EA_LIST_EMPTY_SIZE;
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

        /* Level 7 returns a case-corrected full path through the regular path resolver. */
        case FIL_QUERYCASEDNAME:
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
    if (cbInfoBuf < FILESTATUS16_SIZE)
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
    st->cbList = EA_LIST_EMPTY_SIZE;
    return NO_ERROR;
}

APIRET queryFileInfoEasFromList(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FIXME("write me");
    return ERROR_INVALID_LEVEL;
}

/* Zero date and time fields leave the file's stamp unchanged. */
static int64_t os2DateSeconds(FDATE date, FTIME time)
{
    if (!date.day && !date.month && !date.year && !time.twosecs && !time.minutes && !time.hours)
        return INT64_MIN;
    LxHostCalendar calendar = {.year = FDATE_EPOCH_YEAR + date.year,
                               .month = date.month,
                               .day = date.day,
                               .hour = time.hours,
                               .minute = time.minutes,
                               .second = time.twosecs * 2};
    int64_t seconds;
    return lx_host_calendar_seconds(&calendar, 0, &seconds) ? seconds : INT64_MIN;
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
                ((FILESTATUS4 *)pInfo)->cbList = ea_size ? ea_size : EA_LIST_EMPTY_SIZE;
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

/* Applies a FILESTATUS3 to an open file, or to a path when file is NULL. Values that
   already match are skipped, so a query/set round trip needs no extended attributes. */
static APIRET applyStatus(LxHostFile *file, const char *path, const FILESTATUS3 *info)
{
    LxHostFileInfo current;
    LxHostFsError error =
        file ? lx_host_file_info(file, &current) : lx_host_path_info(path, &current);
    if (error)
        return fileError(error);
    FDATE date;
    FTIME time;
    if (current.created)
        unixTimeToOs2(current.created, &date, &time);
    else
        initFileCreationDateTime(&date, &time);
    int64_t created = os2DateSeconds(info->fdateCreation, info->ftimeCreation);
    if (!memcmp(&date, &info->fdateCreation, sizeof date) &&
        !memcmp(&time, &info->ftimeCreation, sizeof time))
        created = INT64_MIN;
    int64_t accessed = os2DateSeconds(info->fdateLastAccess, info->ftimeLastAccess);
    int64_t modified = os2DateSeconds(info->fdateLastWrite, info->ftimeLastWrite);
    const unsigned attributes = info->attrFile & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);
    if (created != INT64_MIN || accessed != INT64_MIN || modified != INT64_MIN)
        error = file ? lx_host_file_stamps(file, created, accessed, modified)
                     : lx_host_path_stamps(path, created, accessed, modified);
    if (!error && attributes != current.attributes)
        error = file ? lx_host_file_attributes(file, attributes)
                     : lx_host_path_attributes(path, attributes);
    if (!error)
    {
        const int readonly = (info->attrFile & FILE_READONLY) != 0;
        error =
            file ? lx_host_file_readonly(file, readonly) : lx_host_path_readonly(path, readonly);
    }
    return error == LX_HOST_FS_INVALID ? ERROR_NOT_SUPPORTED : fileError(error);
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
    error = applyStatus(NULL, path, pInfoBuf);
    free(path);
    return error;
}

static void storeStatus16(PVOID buffer, USHORT level, const FILESTATUS4 *value)
{
    uint8 *p = buffer;
    storeDateTime16(p + FILESTATUS16_CREATION_OFFSET, value->fdateCreation, value->ftimeCreation);
    storeDateTime16(p + FILESTATUS16_ACCESS_OFFSET, value->fdateLastAccess, value->ftimeLastAccess);
    storeDateTime16(p + FILESTATUS16_WRITE_OFFSET, value->fdateLastWrite, value->ftimeLastWrite);
    lx_guest_store_u32(p + FILESTATUS16_SIZE_OFFSET, value->cbFile);
    lx_guest_store_u32(p + FILESTATUS16_ALLOC_OFFSET, value->cbFileAlloc);
    lx_guest_store_u16(p + FILESTATUS16_ATTRIBUTES_OFFSET, value->attrFile);
    if (level == FIL_QUERYEASIZE)
        lx_guest_store_u32(p + FILESTATUS16_EASIZE_OFFSET, value->cbList);
}

APIRET16 Dos16QPathInfo(PSZ path, USHORT level, PVOID buffer, USHORT capacity, ULONG reserved)
{
    if (reserved)
        return ERROR_INVALID_PARAMETER;
    if (level == FIL_QUERYFULLNAME)
        return (APIRET16)DosQueryPathInfo(path, level, buffer, capacity);
    unsigned size = level == FIL_STANDARD      ? FILESTATUS16_SIZE
                    : level == FIL_QUERYEASIZE ? FILESTATUS2_16_SIZE
                                               : 0;
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
    if (level == FIL16_QUERYALLEAS)
        return capacity < EAOP16_SIZE ? ERROR_BUFFER_OVERFLOW : queryAllEas16(hf, buffer);
    const uint32 size = level == FIL_STANDARD      ? FILESTATUS16_SIZE
                        : level == FIL_QUERYEASIZE ? FILESTATUS2_16_SIZE
                                                   : 0;
    if (!size)
        return ERROR_INVALID_LEVEL;
    if (capacity < size)
        return ERROR_BUFFER_OVERFLOW;
    uint32 address;
    if (!lx_execution_address(GProcessState.execution, buffer, size, LX_GUEST_WRITE, &address))
        return ERROR_INVALID_PARAMETER;
    FILESTATUS4 value = {0};
    const APIRET result = DosQueryFileInfo(hf, level, &value, sizeof value);
    if (!result)
        storeStatus16(buffer, level, &value);
    return result;
}

APIRET16 Dos16SetFileInfo(USHORT hf, USHORT level, PVOID buffer, USHORT capacity)
{
    /* Level 2 sets the EAs an EAOP names. */
    if (level == FIL_QUERYEASIZE)
        return capacity < EAOP16_SIZE ? ERROR_BUFFER_OVERFLOW : applyEas16(hf, buffer);
    if (level != FIL_STANDARD)
        return ERROR_INVALID_LEVEL;
    if (capacity < FILESTATUS16_SIZE)
        return ERROR_BUFFER_OVERFLOW;
    uint32 address;
    if (!lx_execution_address(GProcessState.execution, buffer, FILESTATUS16_SIZE, LX_GUEST_READ,
                              &address))
        return ERROR_INVALID_PARAMETER;
    FILESTATUS3 value = {0};
    /* The current file-info setter applies the permission attribute. */
    value.attrFile = lx_guest_load_u16((uint8 *)buffer + FILESTATUS16_ATTRIBUTES_OFFSET);
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
    const APIRET error = applyStatus(file.resource, NULL, pInfo);
    lx_handle_release(&file);
    return error;
}
