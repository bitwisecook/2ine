/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"
char *makeOS2Path(const char *fname)
{
    char *full = lx_host_path_canonical(fname);
    if (!full)
        return NULL;

    char *retval = (char *)malloc(strlen(full) + 3);
    if (!retval)
        return NULL;

    retval[0] = 'C';
    retval[1] = ':';
    strcpy(retval + 2, full);
    free(full);
    for (char *ptr = retval + 2; *ptr; ptr++)
    {
        if (*ptr == '/')
            *ptr = '\\';
    }

    return retval;
}

int locateOneElement(char *buf)
{
    if (lx_host_path_exists(buf))
        return 1;

    LxHostDirectory *directory;
    char *ptr = strrchr(buf, '/');
    if (!ptr)
    {
        directory = lx_host_directory_open(".", NULL);
        ptr = buf;
    }
    else if (ptr == buf)
    {
        directory = lx_host_directory_open("/", NULL);
        ptr++;
    }
    else
    {
        *ptr = 0;
        directory = lx_host_directory_open(buf, NULL);
        *ptr++ = '/';
    }
    if (!directory)
        return 0;
    const char *name;
    while (lx_host_directory_read(directory, &name, NULL, NULL) == 1)
    {
        if (!strcasecmp(name, ptr))
        {
            strcpy(ptr, name);
            lx_host_directory_close(directory);
            return 1;
        }
    }
    lx_host_directory_close(directory);
    return 0;
}

int locatePathCaseInsensitive(char *buf)
{
    int rc;
    char *ptr = buf;

    if (*ptr == '\0')
        return 0;

    if (lx_host_path_exists(buf))
        return 0;

    while ((ptr = strchr(ptr + 1, '/')) != NULL)
    {
        *ptr = '\0';
        rc = locateOneElement(buf);
        *ptr = '/';
        if (!rc)
            return -2;
    }

    return locateOneElement(buf) ? 0 : -1;
}

char *lxMakeUnixPath(const char *os2path, uint32 *err)
{
    const char *mountpoint = NULL;
    const char *cwd = NULL;
    const char *fallback_cwd = NULL;
    const char *rootrel = NULL;
    int driveidx = 0;

    FIXME("map devices in 2ine.cfg");
    if ((strcasecmp(os2path, "NUL") == 0) || (strcasecmp(os2path, "\\DEV\\NUL") == 0))
        os2path = "/dev/null";

    else
    {
        char drive = os2path[0];
        if ((drive >= 'a') && (drive <= 'z'))
        {
            drive += 'A' - 'a';
        }

        if (((drive >= 'A') && (drive <= 'Z')) && (os2path[1] == ':'))
        {
            os2path += 2;
        }
        else
        {
            drive = (GLoaderState.current_disk - 1) + 'A';
        }

        driveidx = drive - 'A';
        mountpoint = GLoaderState.disks[driveidx];
        if (!mountpoint)
        {
            *err = 26;
            return NULL;
        }

        fallback_cwd = GLoaderState.current_dir[driveidx];
        if ((*os2path != '\\') && (*os2path != '/'))
        {
            cwd = fallback_cwd;
        }
        else
        {
            rootrel = os2path + 1;
        }
    }

    const size_t len =
        (mountpoint ? strlen(mountpoint) : 0) + (cwd ? strlen(cwd) : 0) + strlen(os2path) + 3;
    char *retval = (char *)malloc(len);
    if (!retval)
    {
        *err = 8;
        return NULL;
    }

    snprintf(retval, len, "%s%s%s%s%s", mountpoint ? mountpoint : "", cwd ? "/" : "",
             cwd ? cwd : "", ((!*os2path) || (*os2path == '/')) ? "" : "/", os2path);

    for (char *ptr = strchr(retval, '\\'); ptr; ptr = strchr(ptr + 1, '\\'))
        *ptr = '/';

    FIXME("this could be more efficient");
    char *ptr = retval;
    while ((ptr = strstr(ptr, "//")) != NULL)
    {
        memmove(ptr, ptr + 1, strlen(ptr));
    }

    locatePathCaseInsensitive(retval);

    /* Microsoft linker response files may use a root-relative name for a cwd file.
   Prefer the drive-root path, then try the current directory. */
    if (rootrel && (!lx_host_path_exists(retval)))
    {
        char *roots = strdup(rootrel);
        size_t rootslen = roots ? strlen(roots) : 0;
        while (rootslen && ((roots[rootslen - 1] == '\\') || (roots[rootslen - 1] == '/')))
        {
            roots[--rootslen] = '\0';
        }
        const char *dir = fallback_cwd;
        const size_t altlen = (mountpoint ? strlen(mountpoint) : 0) + (dir ? strlen(dir) : 0) +
                              (roots ? strlen(roots) : 0) + 4;
        char *alt = (char *)malloc(altlen);
        if (alt && roots)
        {
            snprintf(alt, altlen, "%s%s%s%s%s", mountpoint ? mountpoint : "", dir ? "/" : "",
                     dir ? dir : "", *roots ? "/" : "", roots);
            for (char *p = strchr(alt, '\\'); p; p = strchr(p + 1, '\\'))
                *p = '/';
            char *pp = alt;
            while ((pp = strstr(pp, "//")) != NULL)
                memmove(pp, pp + 1, strlen(pp));
            locatePathCaseInsensitive(alt);
            if (lx_host_path_exists(alt))
            {
                free(retval);
                retval = alt;
                alt = NULL;
            }
            free(alt);
        }
        free(roots);
    }

    return retval;
}
