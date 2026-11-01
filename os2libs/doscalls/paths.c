/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

static unsigned editNameFold(unsigned char value)
{
    return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

APIRET16 Dos16EditName(USHORT level, PSZ source, PSZ edit, PBYTE target, USHORT capacity)
{
    TRACE_NATIVE("Dos16EditName(%u, '%s', '%s', %p, %u)", level, source, edit, target, capacity);
    if (level != 1 || !source || !edit || !target || !capacity)
        return ERROR_INVALID_PARAMETER;
    if (strpbrk(source, "\\/:") || strpbrk(edit, "\\/:"))
        return ERROR_INVALID_NAME;
    char result[CCHMAXPATHCOMP];
    unsigned used = 0;
    const char *input = source;
    for (const char *mask = edit; *mask; mask++)
    {
        if (*mask == '*')
        {
            while (*input && (!mask[1] || editNameFold(*input) != editNameFold(mask[1])))
            {
                if (used + 1 >= sizeof result)
                    return ERROR_INVALID_NAME;
                result[used++] = *input++;
            }
        }
        else if (*mask == '?')
        {
            if (*input && *input != '.')
            {
                if (used + 1 >= sizeof result)
                    return ERROR_INVALID_NAME;
                result[used++] = *input++;
            }
        }
        else
        {
            if (used + 1 >= sizeof result)
                return ERROR_INVALID_NAME;
            result[used++] = *mask;
            if (*mask == '.')
            {
                while (*input && *input != '.')
                    input++;
            }
            if (*input)
                input++;
        }
    }
    if (used && result[used - 1] == '.')
        used--;
    result[used] = 0;
    if (used + 1 > capacity)
        return ERROR_INVALID_NAME;
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, target, used + 1, LX_GUEST_WRITE, &address))
        return ERROR_INVALID_PARAMETER;
    memcpy(target, result, used + 1);
    return NO_ERROR;
}

APIRET DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal, ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb,
                        PULONG pcbBuffLength)
{
    TRACE_NATIVE("DosQueryFSAttach('%s', %u, %u, %p, %p)", pszDeviceName, (unsigned int)ulOrdinal,
                 (unsigned int)ulFSAInfoLevel, pfsqb, pcbBuffLength);
    if (!pcbBuffLength)
        return ERROR_INVALID_PARAMETER;
    unsigned drive = 26;
    if (ulFSAInfoLevel == 1)
    {
        if (!pszDeviceName || strlen(pszDeviceName) != 2 || pszDeviceName[1] != ':')
            return ERROR_INVALID_DRIVE;
        unsigned char letter = pszDeviceName[0];
        if (letter >= 'a' && letter <= 'z')
            letter -= 'a' - 'A';
        if (letter >= 'A' && letter <= 'Z')
            drive = letter - 'A';
    }
    else if (ulFSAInfoLevel == 3)
    {
        unsigned ordinal = 0;
        for (unsigned i = 0; i < 26; i++)
            if (GLoaderState.disks[i] && ++ordinal == ulOrdinal)
            {
                drive = i;
                break;
            }
    }
    else
        return ERROR_INVALID_LEVEL;
    if (drive >= 26 || !GLoaderState.disks[drive])
        return ERROR_INVALID_DRIVE;
    const char filesystem[] = "HPFS";
    const ULONG size = 8 + 3 + sizeof filesystem;
    ULONG capacity = *pcbBuffLength;
    *pcbBuffLength = size;
    if (!pfsqb || capacity < size)
        return ERROR_BUFFER_OVERFLOW;
    uint8 *output = (uint8 *)pfsqb;
    lx_guest_store_u16(output, 3); /* FSAT_LOCALDRV */
    lx_guest_store_u16(output + 2, 2);
    lx_guest_store_u16(output + 4, sizeof filesystem - 1);
    lx_guest_store_u16(output + 6, 0);
    output[8] = 'A' + drive;
    output[9] = ':';
    output[10] = 0;
    memcpy(output + 11, filesystem, sizeof filesystem);
    return NO_ERROR;
}

APIRET16 Dos16QFsInfo(USHORT drivenum, USHORT fslevel, PBYTE infobuf, USHORT infobufsize)
{
    TRACE_NATIVE("Dos16QFsInfo(%d, %d, %p, %d)", drivenum, fslevel, infobuf, infobufsize);
    const char label[] = "host";
    if (fslevel != 2)
        return ERROR_INVALID_LEVEL;
    if (infobufsize < 5 + sizeof label)
        return ERROR_BUFFER_OVERFLOW;
    memset(infobuf, 0, 4);
    infobuf[4] = sizeof label - 1;
    memcpy(infobuf + 5, label, sizeof label);
    return NO_ERROR;
}

char *makeUnixPath(const char *os2path, APIRET *err)
{
    return GLoaderState.makeUnixPath(os2path, (uint32 *)err);
}

char *findExePath(const char *exe, APIRET *err)
{
    FIXME(
        "eventually this needs to search the path and maybe launch native (non-OS/2) processes too");
    char *retval = lx_host_executable_path();
    if (!retval)
        *err = ERROR_NOT_ENOUGH_MEMORY;
    return retval;
}

APIRET DosQueryCurrentDir_implementation(ULONG disknum, PBYTE pBuf, PULONG pcbBuf)
{
    if (disknum > (sizeof(GLoaderState.disks) / sizeof(GLoaderState.disks[0])))
    {
        return ERROR_INVALID_DRIVE;
    }

    const char *cdir =
        GLoaderState.current_dir[disknum ? (disknum - 1) : (GLoaderState.current_disk - 1)];
    if (cdir == NULL)
    {
        FIXME("should this return ERROR_INVALID_DRIVE?");
        return ERROR_NOT_DOS_DISK;
    }

    const ULONG pathlen = (ULONG)strlen(cdir);
    if (*pcbBuf <= pathlen)
    {
        *pcbBuf = pathlen;
        return ERROR_BUFFER_OVERFLOW;
    }

    strcpy((char *)pBuf, cdir);
    *pcbBuf = pathlen;
    return NO_ERROR;
}

APIRET DosQueryCurrentDir(ULONG disknum, PBYTE pBuf, PULONG pcbBuf)
{
    TRACE_NATIVE("DosQueryCurrentDir(%u, %p, %p)", (uint)disknum, pBuf, pcbBuf);
    return DosQueryCurrentDir_implementation(disknum, pBuf, pcbBuf);
}

APIRET DosQueryCurrentDisk_implementation(PULONG pdisknum, PULONG pdiskmap)
{
    *pdisknum = GLoaderState.current_disk;
    *pdiskmap = GLoaderState.diskmap;
    return NO_ERROR;
}

APIRET DosQueryCurrentDisk(PULONG pdisknum, PULONG pdiskmap)
{
    TRACE_NATIVE("DosQueryCurrentDisk(%p, %p)", pdisknum, pdiskmap);
    return DosQueryCurrentDisk_implementation(pdisknum, pdiskmap);
}

APIRET DosSetCurrentDir_implementation(PSZ pszName)
{
    /* Reject trailing separators except the drive root, as required by DosSetCurrentDir. */

    if (*pszName == '\0')
    {
        return NO_ERROR;
    }
    else if ((pszName[0] == '\\') && (pszName[1] == '\0'))
    {
    }
    else if (pszName[strlen(pszName) - 1] == '\\')
    {
        return ERROR_PATH_NOT_FOUND;
    }

    FIXME("This function needs a lock"); // in case the cwd changes from another thread.

    uint32 err = 0;
    char *path = makeUnixPath(pszName, &err);
    if (!path)
        return (APIRET)err;

    /* Host chdir validates the target directory and gives child processes the same cwd. */
    LxHostFsError changed = lx_host_path_chdir(path);
    APIRET retval = changed == LX_HOST_FS_NOT_DIRECTORY || changed == LX_HOST_FS_MISSING
                        ? ERROR_PATH_NOT_FOUND
                        : fileError(changed);

    free(path);

    if (retval == NO_ERROR)
    {
        char drive = pszName[0];
        if ((drive >= 'a') && (drive <= 'z'))
        {
            drive += 'A' - 'a';
        }

        if (((drive >= 'A') && (drive <= 'Z')) && (pszName[1] == ':'))
        {
            pszName += 2; // skip drive letter and colon.
        }
        else
        {
            drive = (GLoaderState.current_disk - 1) + 'A';
        }

        const int driveidx = drive - 'A';
        char *oldpath = (*pszName == '\\') ? "" : GLoaderState.current_dir[driveidx];
        const size_t buflen = strlen(pszName) + strlen(oldpath) + 2;
        char *newpath = (char *)malloc(buflen);
        if (!newpath)
        {
            return ERROR_NOT_ENOUGH_MEMORY;
        }

        /* Normalize the per-drive cwd after resolving relative components. */
        snprintf(newpath, buflen, "%s%s%s", oldpath, *oldpath ? "\\" : "", pszName);

        char *ptr = newpath;

        while ((ptr = strstr(ptr, "\\\\")) != NULL)
        {
            memmove(ptr, ptr + 1, strlen(ptr));
        }

        if (*newpath == '\\')
        {
            memmove(newpath, newpath + 1, strlen(newpath));
        }

        char *element = newpath;
        ptr = newpath;
        while ((ptr = strchr(ptr, '\\')) != NULL)
        {
            *ptr = '\0';
            if (strcmp(element, ".") == 0)
            {
                memmove(element, ptr + 1, strlen(ptr + 1) + 1);
                ptr = element;
            }
            else if (strcmp(element, "..") == 0)
            {
                if (element == newpath)
                {
                    memmove(newpath, ptr + 1, strlen(ptr + 1) + 1);
                    ptr = newpath;
                }
                else
                {
                    assert(((int)(element - newpath)) >= 2);
                    char *prev = element - 2;
                    while ((prev != newpath) && (*prev != '\\'))
                    {
                        prev--;
                    }
                    if (prev != newpath)
                    {
                        prev++;
                    }
                    memmove(prev, ptr + 1, strlen(ptr + 1) + 1);
                    ptr = prev;
                }
            }
            else
            {
                *ptr = '\\';
                ptr++;
            }
            element = ptr;
        }

        if (strcmp(element, ".") == 0)
        {
            if (element != newpath)
            {
                element--;
            }
            *element = '\0';
        }
        else if (strcmp(element, "..") == 0)
        {
            if (element == newpath)
            {
                *newpath = '\0';
            }
            else
            {
                assert(((int)(element - newpath)) >= 2);
                char *prev = element - 2;
                while ((prev != newpath) && (*prev != '\\'))
                {
                    prev--;
                }
                *prev = '\0'; // dump parent (if any) and ".."
            }
        }

        ptr = (char *)realloc(newpath, strlen(newpath) + 1);
        if (ptr)
        {
            newpath = ptr;
        }

        free(GLoaderState.current_dir[driveidx]);
        GLoaderState.current_dir[driveidx] = newpath;
    }

    return retval;
}

APIRET DosSetCurrentDir(PSZ pszName)
{
    TRACE_NATIVE("DosSetCurrentDir('%s')", pszName);
    return DosSetCurrentDir_implementation(pszName);
}

APIRET DosCreateDir_implementation(PSZ pszDirName, PEAOP2 peaop2)
{
    if (!pszDirName)
        return ERROR_INVALID_PARAMETER;
    if (peaop2 != NULL)
    {
        FIXME("EAs not yet implemented");
        return ERROR_EA_LIST_INCONSISTENT;
    }

    uint32 err = 0;
    char *path = makeUnixPath(pszDirName, &err);
    if (!path)
        return (APIRET)err;

    const LxHostFsError error = lx_host_path_mkdir(path);
    /* DosCreateDir reports an existing name as access denied, not file exists. */
    const APIRET result = error == LX_HOST_FS_EXISTS    ? ERROR_ACCESS_DENIED
                          : error == LX_HOST_FS_MISSING ? ERROR_PATH_NOT_FOUND
                                                        : fileError(error);
    free(path);
    return result;
}

APIRET DosCreateDir(PSZ pszDirName, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosCreateDir('%s', %p)", pszDirName, peaop2);
    return DosCreateDir_implementation(pszDirName, peaop2);
}

APIRET16 Dos16SelectDisk(USHORT drive)
{
    TRACE_NATIVE("Dos16SelectDisk(%u)", (uint)drive);
    return (APIRET16)DosSetDefaultDisk((ULONG)drive);
}

APIRET16 Dos16QCurDisk(PUSHORT drivenum, PULONG drivemap)
{
    TRACE_NATIVE("Dos16QCurDisk(%p, %p)", drivenum, drivemap);
    ULONG uldrivenum;
    const APIRET rc = DosQueryCurrentDisk_implementation(&uldrivenum, drivemap);
    *drivenum = (USHORT)uldrivenum;
    return (APIRET16)rc;
}

APIRET16 Dos16QCurDir(USHORT drivenum, PBYTE dirpath, PUSHORT dirpathlen)
{
    TRACE_NATIVE("Dos16QCurDir(%u, %p, %p)", (uint)drivenum, dirpath, dirpathlen);
    ULONG uldirpathlen = (ULONG)*dirpathlen;
    const APIRET rc = DosQueryCurrentDir_implementation(drivenum, dirpath, &uldirpathlen);
    *dirpathlen = (USHORT)uldirpathlen;
    return (APIRET16)rc;
}

APIRET16 Dos16ChDir(PSZ pszDir, ULONG res)
{
    TRACE_NATIVE("Dos16ChDir('%s', %u)", pszDir, res);
    if (res != 0)
    { // reserved, and must be zero, or return error.
        return ERROR_INVALID_PARAMETER;
    }
    return (APIRET16)DosSetCurrentDir_implementation(pszDir);
}

APIRET16 Dos16MkDir(PSZ pszDirName, PEAOP2 peaop2)
{
    TRACE_NATIVE("Dos16MkDir('%s', %p)", pszDirName, peaop2);
    return DosCreateDir_implementation(pszDirName, peaop2);
}

APIRET16 Dos16MkDir2(PSZ pszDirName, PEAOP2 peaop2)
{
    TRACE_NATIVE("Dos16MkDir('%s', %p)", pszDirName, peaop2);
    return DosCreateDir_implementation(pszDirName, peaop2);
}

APIRET DosSetDefaultDisk(ULONG disknum)
{
    TRACE_NATIVE("DosSetDefaultDisk(%u)", (uint)disknum);

    if ((disknum < 1) || (disknum > 26) || (!GLoaderState.disks[disknum - 1]))
        return ERROR_INVALID_DRIVE;

    GLoaderState.current_disk = disknum;
    return NO_ERROR;
}

APIRET DosDeleteDir(PSZ pszDir)
{
    TRACE_NATIVE("DosDeleteDir('%s')", pszDir ? pszDir : "");

    if (!pszDir)
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *unixpath = makeUnixPath(pszDir, &err);
    if (!unixpath)
        return err;

    const LxHostFsError error = lx_host_path_remove(unixpath, 1);
    free(unixpath);
    return error == LX_HOST_FS_MISSING ? ERROR_PATH_NOT_FOUND : fileError(error);
}

APIRET DosSearchPath(ULONG flag, PSZ pszPathOrName, PSZ pszFilename, PBYTE pBuf, ULONG cbBuf)
{
    TRACE_NATIVE("DosSearchPath(%u, '%s', '%s', %p, %u)", (uint)flag,
                 pszPathOrName ? pszPathOrName : "", pszFilename ? pszFilename : "", pBuf,
                 (uint)cbBuf);

    if ((!pszFilename) || (!*pszFilename) || (!pBuf) || (!cbBuf))
        return ERROR_INVALID_PARAMETER;

    const char *file = pszFilename;
    char fullpath[PATH_MAX];
    APIRET err = NO_ERROR;

    // A file name that carries its own drive/absolute path bypasses the list.
    if (strchr(file, ':') || (strchr(file, '\\') && (*file == '\\')) ||
        (strchr(file, '/') && (*file == '/')))
    {
        char *unixpath = makeUnixPath(file, &err);
        if (!unixpath)
            return err;
        const int found = lx_host_path_exists(unixpath);
        free(unixpath);
        if (!found)
            return ERROR_FILE_NOT_FOUND;
        if (strlen(file) + 1 > cbBuf)
            return ERROR_BUFFER_OVERFLOW;
        strcpy((char *)pBuf, file);
        return NO_ERROR;
    }

    // pszPathOrName is either a path list or the name of an environment
    //  variable holding one.
    char pathlist[PATH_MAX] = {0};
    const char *path = pszPathOrName;
    if (flag & SEARCH_ENVIRONMENT)
    {
        char *value = NULL;
        const APIRET envrc =
            (path && *path) ? DosScanEnv((PSZ)path, &value) : ERROR_ENVVAR_NOT_FOUND;
        if (envrc != NO_ERROR)
        {
            if (!(flag & SEARCH_IGNORENETERRS))
                return envrc;
            path = NULL;
        }
        else
        {
            strncpy(pathlist, value, sizeof(pathlist) - 1);
            path = pathlist;
        }
    }

    int found = 0;

    if (flag & SEARCH_CUR_DIRECTORY)
    {
        snprintf(fullpath, sizeof(fullpath), "%s", file);
        char *unixpath = makeUnixPath(fullpath, &err);
        if (unixpath)
        {
            found = lx_host_path_exists(unixpath);
            free(unixpath);
        }
    }

    const char *dir = path;
    while ((!found) && dir && *dir)
    {
        const char *end = strchr(dir, ';');
        const size_t dirlen = end ? (size_t)(end - dir) : strlen(dir);
        if (dirlen == 0)
        {
            snprintf(fullpath, sizeof(fullpath), "%s", file);
        }
        else if ((dirlen + 1 + strlen(file)) < sizeof(fullpath))
        {
            snprintf(fullpath, sizeof(fullpath), "%.*s\\%s", (int)dirlen, dir, file);
        }
        else
        {
            return ERROR_FILENAME_EXCED_RANGE;
        }

        char *unixpath = makeUnixPath(fullpath, &err);
        if (unixpath)
        {
            found = lx_host_path_exists(unixpath);
            free(unixpath);
        }

        dir = end ? end + 1 : NULL;
    }

    if (!found)
        return ERROR_FILE_NOT_FOUND;
    if (strlen(fullpath) + 1 > cbBuf)
        return ERROR_BUFFER_OVERFLOW;

    strcpy((char *)pBuf, fullpath);
    return NO_ERROR;
}

APIRET DosQueryFSInfo(ULONG drive, ULONG level, PVOID data, ULONG capacity)
{
    TRACE_NATIVE("DosQueryFSInfo(%u, %u, %p, %u)", (uint)drive, (uint)level, data, (uint)capacity);
    if (!drive)
        drive = GLoaderState.current_disk;
    if (drive > 26 || !drive || !GLoaderState.disks[drive - 1])
        return ERROR_INVALID_DRIVE;
    uint8 *output = data;
    if (!output)
        return ERROR_INVALID_PARAMETER;
    if (level == 1)
    {
        if (capacity < 18)
            return ERROR_BUFFER_OVERFLOW;
        uint64_t total, available;
        LxHostFsError error = lx_host_path_space(GLoaderState.disks[drive - 1], &total, &available);
        if (error)
            return fileError(error);
        uint64_t units = total / 4096, free_units = available / 4096;
        lx_guest_store_u32(output, 0);
        lx_guest_store_u32(output + 4, 8);
        lx_guest_store_u32(output + 8, units > UINT32_MAX ? UINT32_MAX : units);
        lx_guest_store_u32(output + 12, free_units > UINT32_MAX ? UINT32_MAX : free_units);
        lx_guest_store_u16(output + 16, 512);
        return NO_ERROR;
    }
    if (level == 2)
    {
        if (capacity < 17)
            return ERROR_BUFFER_OVERFLOW;
        memset(output, 0, 17);
        output[4] = 4;
        memcpy(output + 5, "2ine", 5);
        return NO_ERROR;
    }
    return ERROR_INVALID_LEVEL;
}
