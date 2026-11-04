/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pipe-driven independent processes exercising public profile operations. */
#include "os2libs/pmshapi.h"
#include "os2libs/pmshapi/profiles.h"
#include "hal/host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HINI profile;
static HINI scope(const char *name)
{
    return !strcmp(name, "user")     ? HINI_USERPROFILE
           : !strcmp(name, "system") ? HINI_SYSTEMPROFILE
           : !strcmp(name, "merged") ? HINI_PROFILE
                                     : profile;
}
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!strcmp(argv[1], "lock"))
    {
        LxHostFile *lock = lx_host_file_open(
            argv[2], LX_HOST_FILE_READ | LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE, 0, NULL);
        if (!lock || lx_host_file_lock(lock, 1) != LX_HOST_FS_OK)
            return 3;
        puts("ready");
        getchar();
        lx_host_file_close(lock);
        return 0;
    }
    profile = lx_profile_open_host(argv[2]);
    if (!profile)
        return 3;
    puts("ready");
    char line[9000];
    while (fgets(line, sizeof line, stdin))
    {
        char command[16], name[16], app[128], key[128], value[8193];
        int fields = sscanf(line, "%15s %15s %127s %127s %8192s", command, name, app, key, value);
        if (fields < 2)
            return 4;
        HINI h = scope(name);
        int ok = 0;
        if (!strcmp(command, "get") && fields == 4)
        {
            unsigned char bytes[4096];
            ULONG size = sizeof bytes;
            if (!PrfQueryProfileData(h, app, key, bytes, &size))
                puts("missing");
            else
            {
                fputs("hex:", stdout);
                for (ULONG i = 0; i < size; i++)
                    printf("%02x", bytes[i]);
                putchar('\n');
            }
            continue;
        }
        if (!strcmp(command, "set") && fields == 5)
        {
            size_t length = strlen(value);
            unsigned char bytes[4096];
            if (length % 2)
                return 4;
            for (size_t i = 0; i < length / 2; i++)
            {
                unsigned byte;
                if (sscanf(value + i * 2, "%2x", &byte) != 1)
                    return 4;
                bytes[i] = byte;
            }
            ok = PrfWriteProfileData(h, app, key, bytes, (ULONG)(length / 2));
        }
        else if (!strcmp(command, "batch") && fields == 5)
        {
            ok = 1;
            for (unsigned i = 0; i < (unsigned)strtoul(value, NULL, 10) && ok; i++)
            {
                char item[160];
                snprintf(item, sizeof item, "%s%u", key, i);
                ok = PrfWriteProfileString(h, app, item, item);
            }
        }
        else if (!strcmp(command, "del") && fields == 4)
            ok = PrfWriteProfileData(h, app, key, NULL, 0);
        else if (!strcmp(command, "drop") && fields == 3)
            ok = PrfWriteProfileData(h, app, NULL, NULL, 0);
        else if (!strcmp(command, "clear") && fields == 2)
            ok = PrfWriteProfileData(h, NULL, NULL, NULL, 0);
        puts(ok ? "ok" : "error");
    }
    return PrfCloseProfile(profile) ? 0 : 5;
}
