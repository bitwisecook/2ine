/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* DosSearchPath over a path list or an environment variable. */
#include "os2/baseos/doscalls/internal.h"

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

APIRET16 Dos16SearchPath(USHORT flags, PSZ path, PSZ name, PBYTE result, USHORT capacity)
{
    return DosSearchPath(flags, path, name, result, capacity);
}
