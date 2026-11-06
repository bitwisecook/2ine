/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#include "hal/host.h"
#include "hal/host_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/resource.h>
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

char *const *lx_host_environment(void)
{
    extern char **environ;
    return environ;
}

int lx_host_process_set_priority(LxHostProcessPriority priority)
{
    int wanted;
    switch (priority)
    {
        case LX_HOST_PROCESS_PRIORITY_NORMAL:
            wanted = 0;
            break;
        case LX_HOST_PROCESS_PRIORITY_IDLE:
            wanted = 19;
            break;
        case LX_HOST_PROCESS_PRIORITY_REALTIME:
            errno = ENOTSUP;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }
    /* Linux nice values are per task. Do not mutate just the calling thread
       when the process has other tasks with a different scheduling level. */
    DIR *tasks = opendir("/proc/self/task");
    if (!tasks)
    {
        errno = ENOTSUP;
        return -1;
    }
    unsigned count = 0;
    int different = 0, error = 0;
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(tasks);
        if (!entry)
        {
            error = errno;
            break;
        }
        char *end;
        long tid = strtol(entry->d_name, &end, 10);
        if (!*entry->d_name || *end || tid <= 0 || tid > INT_MAX)
            continue;
        errno = 0;
        int current = getpriority(PRIO_PROCESS, (id_t)tid);
        if (current == -1 && errno)
        {
            if (errno == ESRCH)
                continue;
            error = errno;
            break;
        }
        count++;
        different |= current != wanted;
    }
    closedir(tasks);
    if (error || !count)
    {
        errno = error ? error : ENOTSUP;
        return -1;
    }
    if (count > 1)
    {
        if (!different)
            return 0;
        errno = ENOTSUP;
        return -1;
    }
    return setpriority(PRIO_PROCESS, 0, wanted);
}
