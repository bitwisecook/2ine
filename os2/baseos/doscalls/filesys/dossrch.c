/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Directory searches: DosFindFirst/Next/Close and their 16-bit forms. */
#include "os2/baseos/doscalls/internal.h"

/* The 16-bit DosFindFirst levels 1 and 2 return FILEFINDBUF and FILEFINDBUF2, which
   have no oNextEntryOffset, no alignment padding and a USHORT attribute. The flat
   search keeps them apart from the 32-bit levels with these private values. */
#define FIND_LEVEL_16_STANDARD ((ULONG) - 1)
#define FIND_LEVEL_16_EASIZE ((ULONG) - 2)

/* Search records. Field offsets follow oNextEntryOffset in the 32-bit records; the
   "L" records widen cbFile and cbFileAlloc to 64 bits. */
enum
{
    FIND_NEXT_OFFSET_SIZE = 4,
    FIND_CREATION_OFFSET = 0,
    FIND_ACCESS_OFFSET = 4,
    FIND_WRITE_OFFSET = 8,
    FIND_SIZE_OFFSET = 12,
    FIND_ALLOC_OFFSET = 16,
    FIND_ATTRIBUTES_OFFSET = 20,
    FIND_EASIZE_OFFSET = 24,
    FIND_16_EASIZE_OFFSET = 22,
    FIND_LARGE_SIZE_HIGH_OFFSET = 16,
    FIND_LARGE_ALLOC_OFFSET = 20,
    FIND_LARGE_ALLOC_HIGH_OFFSET = 24,
    FIND_LARGE_ATTRIBUTES_OFFSET = 28,
    FIND_LARGE_EASIZE_OFFSET = 32,
    /* achName, which follows the cchName byte, in each record. */
    FIND_16_NAME_OFFSET = 23,
    FIND_16_EASIZE_NAME_OFFSET = 27,
    FIND_NAME_OFFSET = 29,
    FIND_EASIZE_NAME_OFFSET = 33,
    FIND_LARGE_NAME_OFFSET = 37,
    FIND_LARGE_EASIZE_NAME_OFFSET = 41,
    /* 32-bit records start on a ULONG boundary. */
    FIND_RECORD_ALIGNMENT = 4
};

/* The smallest record each level returns: one with an empty name. */
enum
{
    FIND_16_MINIMUM = 24,
    FIND_16_EASIZE_MINIMUM = 28,
    FIND_STANDARD_MINIMUM = 32,
    FIND_EASIZE_MINIMUM = 36,
    FIND_STANDARDL_MINIMUM = 40,
    FIND_EASIZEL_MINIMUM = 44
};

enum
{
    FIND_ATTRIBUTE_BITS =
        FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY | FILE_ARCHIVED,
    /* The low byte lists may-have attributes, the high byte must-have ones. */
    FIND_MUST_HAVE_SHIFT = 8,
    FIND_MAY_HAVE_MASK = 0x00FF,
    FIND_MUST_HAVE_MASK = 0xFF00,
    FIND_VALID_ATTRIBUTES = FIND_ATTRIBUTE_BITS | FIND_ATTRIBUTE_BITS << FIND_MUST_HAVE_SHIFT,
    /* Search buffers are limited to one 64 KB segment. */
    FIND_BUFFER_MAXIMUM = 0xFFFF,
    /* 16-bit callers pass HDIR_CREATE as a USHORT. */
    HDIR16_CREATE = 0xFFFF,
    /* Handle 1 is HDIR_SYSTEM; 16-bit search handles are allocated above it. */
    HDIR16_FIRST_ALLOCATED = 2
};

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
    const ULONG minimum = level == FIND_LEVEL_16_EASIZE     ? FIND_16_EASIZE_MINIMUM
                          : level == FIND_LEVEL_16_STANDARD ? FIND_16_MINIMUM
                          : level == FIL_STANDARD           ? FIND_STANDARD_MINIMUM
                          : level == FIL_QUERYEASIZE        ? FIND_EASIZE_MINIMUM
                          : level == FIL_STANDARDL          ? FIND_STANDARDL_MINIMUM
                          : level == FIL_QUERYEASIZEL       ? FIND_EASIZEL_MINIMUM
                                                            : 0;
    return !minimum || count > UINT32_MAX / minimum ? UINT32_MAX : count * minimum;
}

int findNextOne(DirFinder *dirf, PVOID *ppfindbuf, PULONG pcbBuf, int *err)
{
    const ULONG attr = dirf->attr;
    const ULONG musthave = (attr & FIND_MUST_HAVE_MASK) >> FIND_MUST_HAVE_SHIFT;
    const ULONG mayhave = (attr & FIND_MAY_HAVE_MASK);

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

        /* Hidden and system entries need permission; the archive bit never excludes one. */
        const uint32 special = statbuf.attributes & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED);
        if ((special & (FILE_HIDDEN | FILE_SYSTEM) & ~mayhave) ||
            (musthave & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED) & ~special))
            continue;

        const int legacy =
            dirf->level == FIND_LEVEL_16_STANDARD || dirf->level == FIND_LEVEL_16_EASIZE;
        const int large = dirf->level == FIL_STANDARDL || dirf->level == FIL_QUERYEASIZEL;
        const int eas = dirf->level == FIND_LEVEL_16_EASIZE || dirf->level == FIL_QUERYEASIZE ||
                        dirf->level == FIL_QUERYEASIZEL;
        const ULONG name_offset =
            legacy  ? (eas ? FIND_16_EASIZE_NAME_OFFSET : FIND_16_NAME_OFFSET)
            : large ? (eas ? FIND_LARGE_EASIZE_NAME_OFFSET : FIND_LARGE_NAME_OFFSET)
                    : (eas ? FIND_EASIZE_NAME_OFFSET : FIND_NAME_OFFSET);
        ULONG extent = name_offset + (ULONG)namelen + 1;
        if (!legacy)
            extent = (extent + FIND_RECORD_ALIGNMENT - 1) & ~(FIND_RECORD_ALIGNMENT - 1u);
        if (extent > *pcbBuf)
        {
            *err = ERROR_BUFFER_OVERFLOW;
            return 0;
        }
        uint8 *out = *ppfindbuf;
        memset(out, 0, extent);
        uint8 *fields = out + (legacy ? 0 : FIND_NEXT_OFFSET_SIZE);
        FDATE date;
        FTIME time;
        if (statbuf.created)
            unixTimeToOs2(statbuf.created, &date, &time);
        else
            initFileCreationDateTime(&date, &time);
        storeDateTime16(fields + FIND_CREATION_OFFSET, date, time);
        unixTimeToOs2(statbuf.accessed, &date, &time);
        storeDateTime16(fields + FIND_ACCESS_OFFSET, date, time);
        unixTimeToOs2(statbuf.modified, &date, &time);
        storeDateTime16(fields + FIND_WRITE_OFFSET, date, time);
        lx_guest_store_u32(fields + FIND_SIZE_OFFSET, (uint32)statbuf.size);
        if (large)
        {
            lx_guest_store_u32(fields + FIND_LARGE_SIZE_HIGH_OFFSET, (uint32)(statbuf.size >> 32));
            lx_guest_store_u32(fields + FIND_LARGE_ALLOC_OFFSET, (uint32)statbuf.allocated);
            lx_guest_store_u32(fields + FIND_LARGE_ALLOC_HIGH_OFFSET,
                               (uint32)(statbuf.allocated >> 32));
        }
        else
            lx_guest_store_u32(fields + FIND_ALLOC_OFFSET, (uint32)statbuf.allocated);
        const uint32 attributes =
            (isdir ? FILE_DIRECTORY : 0) | (readonly ? FILE_READONLY : 0) | special;
        if (legacy)
            lx_guest_store_u16(fields + FIND_ATTRIBUTES_OFFSET, attributes);
        else
        {
            lx_guest_store_u32(fields +
                                   (large ? FIND_LARGE_ATTRIBUTES_OFFSET : FIND_ATTRIBUTES_OFFSET),
                               attributes);
            lx_guest_store_u32(out, extent);
        }
        if (eas)
        {
            const size_t length = strlen(dirf->path) + namelen + sizeof "/";
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
            lx_guest_store_u32(fields + (legacy  ? FIND_16_EASIZE_OFFSET
                                         : large ? FIND_LARGE_EASIZE_OFFSET
                                                 : FIND_EASIZE_OFFSET),
                               status.cbList);
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
    /* The last 32-bit record's zero oNextEntryOffset ends the list. */
    if (last && dirf->level != FIND_LEVEL_16_STANDARD && dirf->level != FIND_LEVEL_16_EASIZE)
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

    if (flAttribute & ~FIND_VALID_ATTRIBUTES)
        return ERROR_INVALID_PARAMETER;
    else if (cbBuf > FIND_BUFFER_MAXIMUM)
        return ERROR_INVALID_PARAMETER;
    else if ((ulInfoLevel != FIL_STANDARD) && (ulInfoLevel != FIL_QUERYEASIZE) &&
             (ulInfoLevel != FIL_STANDARDL) && (ulInfoLevel != FIL_QUERYEASIZEL) &&
             (ulInfoLevel != FIND_LEVEL_16_STANDARD) && (ulInfoLevel != FIND_LEVEL_16_EASIZE))
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
    const BOOL create = handle == HDIR_CREATE || handle == HDIR16_CREATE;
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
    else if (size > FIND_BUFFER_MAXIMUM || !count || (!data && *count))
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
    if (id == HDIR16_CREATE)
    {
        for (id = HDIR16_FIRST_ALLOCATED; id < HDIR16_CREATE && hdir16[id]; id++)
        {
        }
        if (id == HDIR16_CREATE)
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
    return findFirst16(pszFileSpec, phdir, flAttribute, pfindbuf, cbBuf, pcFileNames,
                       FIND_LEVEL_16_STANDARD);
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
    ULONG wire_level = level == FIL_STANDARD      ? FIND_LEVEL_16_STANDARD
                       : level == FIL_QUERYEASIZE ? FIND_LEVEL_16_EASIZE
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
