/* Public domain. No rights reserved. Wholly AI-authored. */
#define _DARWIN_C_SOURCE 1
#include "host.h"
#include "../common/process.h"
#include <errno.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
char *lx_host_command_line(size_t *size)
{
    if (!size)
    {
        errno = EINVAL;
        return NULL;
    }
    *size = 0;
    int argc;
    char **argv;
    argc = *_NSGetArgc();
    argv = *_NSGetArgv();
    return lx_host_pack_arguments(argc, argv, size);
}

char *lx_host_executable_path(void)
{
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    char *buffer = malloc(size);
    if (!buffer)
        return NULL;
    if (_NSGetExecutablePath(buffer, &size) != 0)
    {
        free(buffer);
        return NULL;
    }
    char *resolved = realpath(buffer, NULL);
    free(buffer);
    return resolved;
}
