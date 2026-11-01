/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "runtime/core/internal.h"
void prepOs2Drives(void)
{
    const int drive_c = 'C' - 'A';
    const int total = (sizeof(GLoaderState.disks) / sizeof(GLoaderState.disks[0]));
    uint32 diskmap = 0;
    int lowestmounted = 0;
    int lowestcwd = 0;
    int i;

    FIXME("need to pass current dirs and disks to OS/2 subprocesses");

    char *cwd = lx_host_path_current();
    char *rp = cwd ? lx_host_path_canonical(cwd) : NULL;
    if ((cwd == NULL) || (rp == NULL))
    {
        fprintf(stderr, "Can't get cwd (%s), defaulting to \"/\".\n", strerror(errno));
        rp = strdup("/");
        lx_host_path_chdir(rp);
    }

    free(cwd);
    cwd = rp;

    for (i = 0; i < total; i++)
    {
        if (GLoaderState.disks[i] == NULL)
        {
            continue;
        }

        rp = lx_host_path_canonical(GLoaderState.disks[i]);
        if (rp == NULL)
        {
            fprintf(stderr, "Can't realpath \"%s\" (%s), not mounting %c:\\\n",
                    GLoaderState.disks[i], strerror(errno), i + 'A');
            free(GLoaderState.disks[i]);
            GLoaderState.disks[i] = NULL;
            continue;
        }

        if (lowestmounted < 3)
        {
            lowestmounted = i + 1;
        }

        diskmap |= (1 << i);

        const size_t rplen = strlen(rp);
        assert(rplen > 0);
        if ((strncmp(rp, cwd, rplen) == 0) &&
            ((rp[rplen - 1] == '/') || (cwd[rplen] == '/') || (cwd[rplen] == '\0')))
        {

            const char *d = cwd + rplen;
            GLoaderState.current_dir[i] = strdup((*d == '/') ? d + 1 : d);
            char *ptr = GLoaderState.current_dir[i];
            while (*ptr)
            {
                if (*ptr == '/')
                {
                    *ptr = '\\';
                }
                ptr++;
            }

            if (lowestcwd < 3)
            {
                lowestcwd = i + 1;
            }
        }
        else
        {

            GLoaderState.current_dir[i] = strdup("");
        }
        free(rp);
    }

    if (!lowestmounted)
    {
        if (getenv("TWOINE_CONFIG"))
        {
            fprintf(stderr, "2ine: explicit configuration has no accessible drives\n");
            exit(1);
        }
        diskmap |= 1u << drive_c;

        GLoaderState.disks[drive_c] = strdup("/");
        char *ptr = strdup(cwd + 1);
        GLoaderState.current_dir[drive_c] = ptr;
        while (*ptr)
        {
            if (*ptr == '/')
            {
                *ptr = '\\';
            }
            ptr++;
        }
        GLoaderState.current_disk = drive_c + 1;
    }
    else if (!lowestcwd)
    {
        /* Use the first mapped drive when cwd is outside all mounts; prefer C: over A:/B:. */
        GLoaderState.current_disk = lowestmounted;
    }
    else
    {
        GLoaderState.current_disk = lowestcwd;
    }

    free(cwd);

    GLoaderState.diskmap = diskmap;

#if 0
    fprintf(stderr, "OS/2 disks:\n");
    for (i = 0; i < total; i++) {
        if (GLoaderState.disks[i] == NULL) continue;
        fprintf(stderr, "  %c: -> mountpoint='%s' cwd='%s' current=%d\n", 'A' + i, GLoaderState.disks[i], GLoaderState.current_dir[i], GLoaderState.current_disk == (i+1));
    }
    fprintf(stderr, "\n");
#endif
}

/* Translate a host file name through the most specific configured drive. */
char *lxHostPathToGuest(const char *name)
{
    char *full = lx_host_path_canonical(name);
    if (!full)
        full = strdup(name);
    if (!full)
        return NULL;
    int drive = -1;
    size_t prefix = 0;
    for (int i = 0; i < 26; i++)
    {
        const char *mount = GLoaderState.disks[i];
        if (!mount)
            continue;
        size_t n = strlen(mount);
        while (n > 1 && mount[n - 1] == '/')
            n--;
        if (n >= prefix && !strncmp(full, mount, n) &&
            (n == 1 || full[n] == '/' || full[n] == '\0'))
        {
            prefix = n;
            drive = i;
        }
    }
    if (drive < 0)
    {
        free(full);
        return NULL;
    }
    const char *relative = full + prefix;
    if (*relative == '/')
        relative++;
    char *result = malloc(strlen(relative) + 4);
    if (result)
    {
        result[0] = 'A' + drive;
        result[1] = ':';
        result[2] = '\\';
        strcpy(result + 3, relative);
        for (char *p = result + 3; *p; p++)
            if (*p == '/')
                *p = '\\';
    }
    free(full);
    return result;
}
