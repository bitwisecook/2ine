/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
char *lx_host_command_line(size_t *size)
{
    if (!size)
    {
        errno = EINVAL;
        return NULL;
    }
    *size = 0;
    FILE *input = fopen("/proc/self/cmdline", "rb");
    if (!input)
        return NULL;
    char *buffer = NULL;
    size_t used = 0;
    for (;;)
    {
        char *next = realloc(buffer, used + 4096);
        if (!next)
        {
            free(buffer);
            fclose(input);
            return NULL;
        }
        buffer = next;
        size_t n = fread(buffer + used, 1, 4096, input);
        used += n;
        if (n < 4096)
        {
            if (ferror(input))
            {
                free(buffer);
                fclose(input);
                return NULL;
            }
            break;
        }
    }
    fclose(input);
    *size = used;
    return buffer;
}

char *lx_host_executable_path(void)
{
    for (size_t size = 256; size <= 1024 * 1024; size *= 2)
    {
        char *buffer = malloc(size);
        if (!buffer)
            return NULL;
        ssize_t n = readlink("/proc/self/exe", buffer, size - 1);
        if (n >= 0 && (size_t)n < size - 1)
        {
            buffer[n] = 0;
            return buffer;
        }
        free(buffer);
        if (n < 0)
            return NULL;
    }
    return NULL;
}
