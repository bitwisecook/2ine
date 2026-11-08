/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

char *makeUnixPath(const char *os2path, APIRET *err)
{
    return GProcessState.makeUnixPath(os2path, (uint32 *)err);
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
    if (disknum > (sizeof(GProcessState.disks) / sizeof(GProcessState.disks[0])))
    {
        return ERROR_INVALID_DRIVE;
    }

    const char *cdir =
        GProcessState.current_dir[disknum ? (disknum - 1) : (GProcessState.current_disk - 1)];
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
            drive = (GProcessState.current_disk - 1) + 'A';
        }

        const int driveidx = drive - 'A';
        char *oldpath = (*pszName == '\\') ? "" : GProcessState.current_dir[driveidx];
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

        free(GProcessState.current_dir[driveidx]);
        GProcessState.current_dir[driveidx] = newpath;
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

APIRET16 Dos16RmDir(PSZ path, ULONG reserved)
{
    return reserved ? ERROR_INVALID_PARAMETER : DosDeleteDir(path);
}
