/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/process/internal.h"

void prepOs2Drives(void)
{
    const int drive_c = 'C' - 'A';
    const int total = (sizeof(GProcessState.disks) / sizeof(GProcessState.disks[0]));
    uint32 diskmap = 0;
    int lowestmounted = 0;
    int lowestcwd = 0;
    int i;

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
        if (GProcessState.disks[i] == NULL)
        {
            continue;
        }

        rp = lx_host_path_canonical(GProcessState.disks[i]);
        if (rp == NULL)
        {
            fprintf(stderr, "Can't realpath \"%s\" (%s), not mounting %c:\\\n",
                    GProcessState.disks[i], strerror(errno), i + 'A');
            free(GProcessState.disks[i]);
            GProcessState.disks[i] = NULL;
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
            GProcessState.current_dir[i] = strdup((*d == '/') ? d + 1 : d);
            char *ptr = GProcessState.current_dir[i];
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

            GProcessState.current_dir[i] = strdup("");
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

        GProcessState.disks[drive_c] = strdup("/");
        char *ptr = strdup(cwd + 1);
        GProcessState.current_dir[drive_c] = ptr;
        while (*ptr)
        {
            if (*ptr == '/')
            {
                *ptr = '\\';
            }
            ptr++;
        }
        GProcessState.current_disk = drive_c + 1;
    }
    else if (!lowestcwd)
    {
        /* Use the first mapped drive when cwd is outside all mounts; prefer C: over A:/B:. */
        GProcessState.current_disk = lowestmounted;
    }
    else
    {
        GProcessState.current_disk = lowestcwd;
    }

    free(cwd);

    /* Launchers and guest subprocesses supply every drive's own directory.
       Canonicalize before accepting it so a forged environment cannot escape a mount. */
    for (i = 0; i < LX_DRIVE_COUNT; i++)
    {
        if (!GProcessState.disks[i])
            continue;
        char key[32];
        snprintf(key, sizeof(key), "TWOINE_DRIVE_CWD_%c", 'A' + i);
        const char *relative = getenv(key);
        if (!relative)
            continue;
        char *candidate = malloc(strlen(GProcessState.disks[i]) + strlen(relative) + 2);
        if (!candidate)
            continue;
        sprintf(candidate, "%s/%s", GProcessState.disks[i], relative);
        for (char *p = candidate; *p; p++)
            if (*p == '\\')
                *p = '/';
        char *full = lx_host_path_canonical(candidate),
             *root = lx_host_path_canonical(GProcessState.disks[i]);
        free(candidate);
        if (full && root)
        {
            size_t length = strlen(root);
            if (!strncmp(full, root, length) &&
                (!full[length] || full[length] == '/' || (length == 1 && root[0] == '/')))
            {
                const char *tail = full + length;
                while (*tail == '/')
                    tail++;
                char *directory = strdup(tail);
                if (directory)
                {
                    for (char *p = directory; *p; p++)
                        if (*p == '/')
                            *p = '\\';
                    free(GProcessState.current_dir[i]);
                    GProcessState.current_dir[i] = directory;
                }
            }
        }
        free(full);
        free(root);
    }
    const char *current = getenv("TWOINE_CURRENT_DRIVE");
    if (current && strlen(current) == 1 && current[0] >= 'A' && current[0] <= 'Z' &&
        GProcessState.disks[current[0] - 'A'])
        GProcessState.current_disk = current[0] - 'A' + 1;
    GProcessState.diskmap = diskmap;

#if 0
    fprintf(stderr, "OS/2 disks:\n");
    for (i = 0; i < total; i++) {
        if (GProcessState.disks[i] == NULL) continue;
        fprintf(stderr, "  %c: -> mountpoint='%s' cwd='%s' current=%d\n", 'A' + i, GProcessState.disks[i], GProcessState.current_dir[i], GProcessState.current_disk == (i+1));
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
    for (int i = 0; i < LX_DRIVE_COUNT; i++)
    {
        const char *mount = GProcessState.disks[i];
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
