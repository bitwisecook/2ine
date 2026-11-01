/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

int wildcardMatch(const char *str, const char *pattern)
{

    int ext = 0;
    while (1)
    {
        const char pat = *(pattern++);
        if (pat == '\0')
        {
            return (*str == '\0') ? 1 : 0;
        }
        else if (pat == '?')
        {
            if (*str) // also matches end of string, but don't advance.
                str++;
        }
        else if ((pat == '.') && (!ext))
        {
            ext = 1;
            if (*str == '.')
                str++;
            else if (*str != '\0')
                return 0;
        }
        else if (pat == '*')
        {
            const char next = *pattern;
            while (*str && (*str != next))
                str++;
        }
        else
        {
            const char ch = *str;
            const char a = ((ch >= 'A') && (ch <= 'Z')) ? (ch - ('A' - 'a')) : ch;
            const char b = ((pat >= 'A') && (pat <= 'Z')) ? (pat - ('A' - 'a')) : pat;
            if (a == b)
                str++;
            else
                return 0;
        }
    }

    __builtin_unreachable();
}

void destroyDirectoryResource(void *value)
{
    DirFinder *directory = value;
    if (directory->dirp)
        lx_host_directory_close(directory->dirp);
    free(directory->path);
    free(directory);
}

ULONG findNextSizeNeeded(const ULONG level, const ULONG count)
{
    const ULONG minimum = level == (ULONG)-2          ? 28
                          : level == (ULONG)-1        ? 24
                          : level == FIL_STANDARD     ? 32
                          : level == FIL_QUERYEASIZE  ? 36
                          : level == FIL_STANDARDL    ? 40
                          : level == FIL_QUERYEASIZEL ? 44
                                                      : 0;
    return !minimum || count > UINT32_MAX / minimum ? UINT32_MAX : count * minimum;
}

int findNextOne(DirFinder *dirf, PVOID *ppfindbuf, PULONG pcbBuf, int *err)
{
    const ULONG attr = dirf->attr;
    const ULONG musthave = (attr & 0xFF00) >> 8;
    const ULONG mayhave = (attr & 0x00FF);

    // these attributes mean nothing on Unix, so if they are Must Haves, return nothing.
    if (musthave & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED))
        return 0;

    const char *pattern = dirf->pattern, *name;
    LxHostFileInfo statbuf;
    LxHostFsError error = LX_HOST_FS_OK;
    while (lx_host_directory_read(dirf->dirp, &name, &statbuf, &error) == 1)
    {

        const size_t namelen = strlen(name);
        if (namelen >= CCHMAXPATHCOMP)
            continue;

        if (!wildcardMatch(name, pattern))
            continue;

        const int isfile = statbuf.kind == LX_HOST_FILE_REGULAR;
        const int isdir = statbuf.kind == LX_HOST_FILE_DIRECTORY;
        if (!isfile && !isdir)
            continue; // OS/2 didn't have fifos, devs, sockets, etc.
        assert(isfile != isdir);

        if (isdir)
        {
            if ((mayhave & FILE_DIRECTORY) == 0)
                continue;
        }
        else
        {
            if (musthave & FILE_DIRECTORY)
                continue;
        }

        const int readonly = statbuf.readonly;
        if (readonly)
        {
            if ((mayhave & FILE_READONLY) == 0)
                continue;
        }
        else
        {
            if (musthave & FILE_READONLY)
                continue;
        }

        const int legacy = dirf->level == (ULONG)-1 || dirf->level == (ULONG)-2;
        const int large = dirf->level == FIL_STANDARDL || dirf->level == FIL_QUERYEASIZEL;
        const int eas = dirf->level == (ULONG)-2 || dirf->level == FIL_QUERYEASIZE ||
                        dirf->level == FIL_QUERYEASIZEL;
        const ULONG name_offset = legacy  ? (eas ? 27 : 23)
                                  : large ? (eas ? 41 : 37)
                                          : (eas ? 33 : 29);
        ULONG extent = name_offset + (ULONG)namelen + 1;
        if (!legacy)
            extent = (extent + 3) & ~3u;
        if (extent > *pcbBuf)
        {
            *err = ERROR_BUFFER_OVERFLOW;
            return 0;
        }
        uint8 *out = *ppfindbuf;
        memset(out, 0, extent);
        uint8 *fields = out + (legacy ? 0 : 4);
        FDATE date;
        FTIME time;
        initFileCreationDateTime(&date, &time);
        lx_guest_store_u16(fields, date.day | date.month << 5 | date.year << 9);
        lx_guest_store_u16(fields + 2, time.twosecs | time.minutes << 5 | time.hours << 11);
        unixTimeToOs2(statbuf.accessed, &date, &time);
        lx_guest_store_u16(fields + 4, date.day | date.month << 5 | date.year << 9);
        lx_guest_store_u16(fields + 6, time.twosecs | time.minutes << 5 | time.hours << 11);
        unixTimeToOs2(statbuf.modified, &date, &time);
        lx_guest_store_u16(fields + 8, date.day | date.month << 5 | date.year << 9);
        lx_guest_store_u16(fields + 10, time.twosecs | time.minutes << 5 | time.hours << 11);
        lx_guest_store_u32(fields + 12, (uint32)statbuf.size);
        if (large)
        {
            lx_guest_store_u32(fields + 16, (uint32)(statbuf.size >> 32));
            lx_guest_store_u32(fields + 20, (uint32)statbuf.allocated);
            lx_guest_store_u32(fields + 24, (uint32)(statbuf.allocated >> 32));
        }
        else
            lx_guest_store_u32(fields + 16, (uint32)statbuf.allocated);
        const uint32 attributes = (isdir ? FILE_DIRECTORY : 0) | (readonly ? FILE_READONLY : 0);
        if (legacy)
            lx_guest_store_u16(fields + 20, attributes);
        else
        {
            lx_guest_store_u32(fields + (large ? 28 : 20), attributes);
            lx_guest_store_u32(out, extent);
        }
        if (eas)
        {
            const size_t length = strlen(dirf->path) + namelen + 2;
            char *path = malloc(length);
            if (!path)
            {
                *err = ERROR_NOT_ENOUGH_MEMORY;
                return 0;
            }
            snprintf(path, length, "%s/%s", dirf->path, name);
            FILESTATUS4 status;
            *err = queryPathInfoEaSize(path, &status, sizeof status);
            free(path);
            if (*err)
                return 0;
            lx_guest_store_u32(fields + (legacy ? 22 : large ? 32 : 24), status.cbList);
        }
        out[name_offset - 1] = (uint8)namelen;
        memcpy(out + name_offset, name, namelen + 1);
        *ppfindbuf = out + extent;
        *pcbBuf -= extent;
        return 1;
    }

    if (error)
        *err = fileError(error);
    return 0; // No matching entries remain.
}

APIRET findNext(DirFinder *dirf, PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames)
{
    if (!dirf->dirp)
    {
        *pcFileNames = 0;
        return ERROR_NO_MORE_FILES;
    }

    const ULONG maxents = *pcFileNames;
    ULONG i;
    int err = NO_ERROR;
    memset(pfindbuf, 0, cbBuf);
    uint8 *last = NULL;
    for (i = 0; i < maxents; i++)
    {
        uint8 *current = pfindbuf;
        const uint64 position = lx_host_directory_position(dirf->dirp);
        if (!findNextOne(dirf, &pfindbuf, &cbBuf, &err))
        {
            if (err == ERROR_BUFFER_OVERFLOW)
                lx_host_directory_seek(dirf->dirp, position);
            break;
        }
        last = current;
    }
    if (last && dirf->level != (ULONG)-1 && dirf->level != (ULONG)-2)
        lx_guest_store_u32(last, 0);
    if (i && err == ERROR_BUFFER_OVERFLOW)
        err = NO_ERROR;

    *pcFileNames = i;

    if ((i == 0) && (maxents > 0) && !err)
    {
        lx_host_directory_close(dirf->dirp);
        dirf->dirp = NULL;
        err = ERROR_NO_MORE_FILES;
    }

    return err;
}

APIRET DosFindFirstLocked(PSZ pszFileSpec, PHDIR phdir, ULONG flAttribute, PVOID pfindbuf,
                          ULONG cbBuf, PULONG pcFileNames, ULONG ulInfoLevel)
{
    TRACE_NATIVE("DosFindFirst('%s', %p, %u, %p, %u, %p, %u)", pszFileSpec, phdir,
                 (uint)flAttribute, pfindbuf, (uint)cbBuf, pcFileNames, (uint)ulInfoLevel);

    if (flAttribute & 0xffffc8c8) // reserved bits that must be zero.
        return ERROR_INVALID_PARAMETER;
    else if (cbBuf > 0xFFFF)
        return ERROR_INVALID_PARAMETER;
    else if ((ulInfoLevel != FIL_STANDARD) && (ulInfoLevel != FIL_QUERYEASIZE) &&
             (ulInfoLevel != FIL_STANDARDL) && (ulInfoLevel != FIL_QUERYEASIZEL) &&
             (ulInfoLevel != (ULONG)-1) && (ulInfoLevel != (ULONG)-2))
        return ERROR_INVALID_PARAMETER;
    else if (!pcFileNames)
        return ERROR_INVALID_PARAMETER;
    else if (!pfindbuf && (*pcFileNames != 0))
        return ERROR_INVALID_PARAMETER;
    else if (!phdir)
        return ERROR_INVALID_PARAMETER;
    else if (!*phdir)
        return ERROR_INVALID_HANDLE;
    else if (cbBuf < findNextSizeNeeded(ulInfoLevel, *pcFileNames))
        return ERROR_BUFFER_OVERFLOW;

    if (flAttribute == FILE_NORMAL)
        flAttribute = FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM |
                      FILE_ARCHIVED; // most of the "may-have" bits.

    APIRET err = NO_ERROR;
    char *path = makeUnixPath(pszFileSpec, &err);
    if (!path)
        return err;

    char *pattern = strrchr(path, '/');
    if (pattern)
        *(pattern++) = '\0';
    else
        pattern = path;

    if (strlen(pattern) >= CCHMAXPATHCOMP)
    {
        free(path);
        return ERROR_FILENAME_EXCED_RANGE;
    }

    LxHandleBorrow directory = {0};
    uint32 handle = *phdir;
    /* Older callers pass the 16-bit create sentinel through the flat API. */
    const BOOL create = handle == HDIR_CREATE || handle == 0xffffu;
    DirFinder *dirf = NULL;
    if (*phdir == HDIR_SYSTEM)
    {
        dirf = &GHDir1;
    }
    else if (create)
    {
        dirf = (DirFinder *)malloc(sizeof(DirFinder));
        if (!dirf)
        {
            free(path);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        memset(dirf, '\0', sizeof(*dirf));
    }
    else
    {
        if (!lx_handle_borrow(handle, LX_HANDLE_DIRECTORY, &directory))
        {
            free(path);
            return ERROR_INVALID_HANDLE;
        }
        dirf = directory.resource;
    }

    LxHostFsError error;
    char *directory_path = strdup((path == pattern) ? "." : path);
    if (!directory_path)
    {
        if (create)
            free(dirf);
        free(path);
        lx_handle_release(&directory);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    LxHostDirectory *dirp = lx_host_directory_open((path == pattern) ? "." : path, &error);
    if (!dirp)
    {
        if (create)
            free(dirf);
        free(directory_path);
        free(path);
        lx_handle_release(&directory);
        return fileError(error);
    }

    if (dirf->dirp)
        lx_host_directory_close(dirf->dirp);

    dirf->dirp = dirp;
    free(dirf->path);
    dirf->path = directory_path;
    dirf->attr = flAttribute;
    dirf->level = ulInfoLevel;
    strcpy(dirf->pattern, pattern);
    free(path);

    err = findNext(dirf, pfindbuf, cbBuf, pcFileNames);

    /* Failed searches allocate no handle, except ERROR_EAS_DIDNT_FIT. */
    if ((err != NO_ERROR) && (err != ERROR_EAS_DIDNT_FIT))
    {
        if (dirf->dirp)
            lx_host_directory_close(dirf->dirp);
        dirf->dirp = NULL;
        if (create)
            destroyDirectoryResource(dirf);
        lx_handle_release(&directory);
        return err;
    }

    if (err == ERROR_NO_MORE_FILES)
        return ERROR_FILE_NOT_FOUND;
    if (create)
    {
        handle = lx_handle_create(LX_HANDLE_DIRECTORY, dirf, destroyDirectoryResource);
        if (!handle)
        {
            destroyDirectoryResource(dirf);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    *phdir = handle;
    lx_handle_release(&directory);
    return NO_ERROR;
}

APIRET DosFindFirst(PSZ spec, PHDIR handle, ULONG attrs, PVOID data, ULONG size, PULONG count,
                    ULONG level)
{
    grabLock(&GMutexDosCalls);
    const APIRET result = DosFindFirstLocked(spec, handle, attrs, data, size, count, level);
    TRACE_RET("DosFindFirst('%s') = %u handle=%u count=%u\n", spec, (unsigned)result,
              handle ? (unsigned)*handle : 0, count ? (unsigned)*count : 0);
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET DosFindNext(HDIR handle, PVOID data, ULONG size, PULONG count)
{
    grabLock(&GMutexDosCalls);
    LxHandleBorrow object = {0};
    DirFinder *directory = handle == HDIR_SYSTEM ? &GHDir1
                           : lx_handle_borrow(handle, LX_HANDLE_DIRECTORY, &object)
                               ? object.resource
                               : NULL;
    APIRET result;
    if (!directory)
        result = ERROR_INVALID_HANDLE;
    else if (size > 0xFFFF || !count || (!data && *count))
        result = ERROR_INVALID_PARAMETER;
    else if (size < findNextSizeNeeded(directory->level, *count))
        result = ERROR_BUFFER_OVERFLOW;
    else
        result = findNext(directory, data, size, count);
    lx_handle_release(&object);
    ungrabLock(&GMutexDosCalls);
    return result;
}

APIRET DosFindClose(HDIR handle)
{
    grabLock(&GMutexDosCalls);
    APIRET result = NO_ERROR;
    if (handle == HDIR_SYSTEM)
    {
        if (GHDir1.dirp)
            lx_host_directory_close(GHDir1.dirp);
        free(GHDir1.path);
        memset(&GHDir1, 0, sizeof GHDir1);
    }
    else if (!lx_handle_close(handle, LX_HANDLE_DIRECTORY))
        result = ERROR_INVALID_HANDLE;
    ungrabLock(&GMutexDosCalls);
    return result;
}

static APIRET16 findFirst16(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute, PVOID pfindbuf,
                            USHORT cbBuf, PUSHORT pcFileNames, ULONG level)
{
    if (!phdir || !pcFileNames)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDir16);
    USHORT id = (USHORT)*phdir;
    ULONG handle;
    if (id == 0xffff)
    {
        for (id = 2; id < 0xffff && hdir16[id]; id++)
        {
        }
        if (id == 0xffff)
        {
            ungrabLock(&GMutexDir16);
            return ERROR_NO_MORE_FILES;
        }
        handle = HDIR_CREATE;
    }
    else if (id == HDIR_SYSTEM)
        handle = HDIR_SYSTEM;
    else if (!hdir16[id])
    {
        ungrabLock(&GMutexDir16);
        return ERROR_INVALID_HANDLE;
    }
    else
        handle = hdir16[id];
    ULONG count = *pcFileNames;
    APIRET result = DosFindFirst(pszFileSpec, &handle, flAttribute, pfindbuf, cbBuf, &count, level);
    if (!result || result == ERROR_EAS_DIDNT_FIT)
    {
        if (id != HDIR_SYSTEM)
            hdir16[id] = handle;
        *phdir = (SHORT)id;
    }
    *pcFileNames = (USHORT)count;
    ungrabLock(&GMutexDir16);
    return (APIRET16)result;
}

APIRET16 Dos16FindFirst(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute, PVOID pfindbuf,
                        USHORT cbBuf, PUSHORT pcFileNames, ULONG res)
{
    if (res)
        return ERROR_INVALID_PARAMETER;
    return findFirst16(pszFileSpec, phdir, flAttribute, pfindbuf, cbBuf, pcFileNames, (ULONG)-1);
}

APIRET16 Dos16FindFirst2(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute, PVOID pfindbuf,
                         USHORT cbBuf, PUSHORT pcFileNames, USHORT level, ULONG res)
{
    TRACE_NATIVE("Dos16FindFirst2('%s', %p, %u, %p, %u, %p, %u, %u)", pszFileSpec, phdir,
                 (uint)flAttribute, pfindbuf, (uint)cbBuf, pcFileNames, (uint)level, (uint)res);
    if (res)
        return ERROR_INVALID_PARAMETER;
    if (level != FIL_STANDARD && level != FIL_QUERYEASIZE && level != FIL_STANDARDL &&
        level != FIL_QUERYEASIZEL)
        return ERROR_INVALID_LEVEL;
    ULONG wire_level = level == FIL_STANDARD      ? (ULONG)-1
                       : level == FIL_QUERYEASIZE ? (ULONG)-2
                                                  : level;
    return findFirst16(pszFileSpec, phdir, flAttribute, pfindbuf, cbBuf, pcFileNames, wire_level);
}

APIRET16 Dos16FindNext(USHORT hDir, PVOID pfindbuf, USHORT cbfindbuf, PUSHORT pcFilenames)
{
    if (!pcFilenames)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDir16);
    ULONG handle = hDir == HDIR_SYSTEM ? HDIR_SYSTEM : hdir16[hDir];
    ULONG count = *pcFilenames;
    APIRET result =
        handle ? DosFindNext(handle, pfindbuf, cbfindbuf, &count) : ERROR_INVALID_HANDLE;
    *pcFilenames = (USHORT)count;
    ungrabLock(&GMutexDir16);
    return (APIRET16)result;
}

APIRET16 Dos16FindClose(USHORT hDir)
{
    grabLock(&GMutexDir16);
    ULONG handle = hDir == HDIR_SYSTEM ? HDIR_SYSTEM : hdir16[hDir];
    APIRET result = handle ? DosFindClose(handle) : ERROR_INVALID_HANDLE;
    if (!result && hDir != HDIR_SYSTEM)
        hdir16[hDir] = 0;
    ungrabLock(&GMutexDir16);
    return (APIRET16)result;
}
