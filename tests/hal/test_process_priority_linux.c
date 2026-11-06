/* Public domain. No rights reserved. Wholly AI-authored. */
/* Linux process-wide requests reject partial per-task changes before mutation. */
#include "hal/host_process.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (errno %d)\n", __FILE__, __LINE__, #x, errno);              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static int task_count, cursor, nice_value, set_error, read_error, open_error;
static unsigned set_calls, open_calls;
static int selected;
DIR *priority_test_opendir(const char *path)
{
    CHECK(!strcmp(path, "/proc/self/task"));
    open_calls++;
    cursor = 0;
    if (open_error)
    {
        errno = open_error;
        return NULL;
    }
    return (DIR *)(uintptr_t)1;
}
struct dirent *priority_test_readdir(DIR *directory)
{
    CHECK(directory == (DIR *)(uintptr_t)1);
    static struct dirent result;
    if (cursor == task_count)
    {
        errno = read_error;
        return NULL;
    }
    snprintf(result.d_name, sizeof result.d_name, "%d", 100 + cursor++);
    return &result;
}
int priority_test_closedir(DIR *directory)
{
    CHECK(directory == (DIR *)(uintptr_t)1);
    errno = EIO; /* Closing must not replace the preflight failure. */
    return 0;
}
int priority_test_getpriority(int which, id_t task)
{
    CHECK(which == PRIO_PROCESS && task >= 100 && task < (id_t)(100 + task_count));
    errno = 0;
    return nice_value;
}
int priority_test_setpriority(int which, id_t process, int value)
{
    CHECK(which == PRIO_PROCESS && process == 0);
    set_calls++;
    selected = value;
    errno = set_error;
    return set_error ? -1 : 0;
}
int main(void)
{
    task_count = 2;
    nice_value = 0;
    CHECK(!lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_NORMAL));
    CHECK(set_calls == 0);
    CHECK(lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_IDLE) == -1 && errno == ENOTSUP);
    CHECK(set_calls == 0);
    nice_value = 19;
    CHECK(!lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_IDLE));
    CHECK(lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_NORMAL) == -1 && errno == ENOTSUP);
    CHECK(set_calls == 0);
    unsigned opened = open_calls;
    CHECK(lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_REALTIME) == -1 &&
          errno == ENOTSUP);
    CHECK(lx_host_process_set_priority((LxHostProcessPriority)99) == -1 && errno == EINVAL);
    CHECK(open_calls == opened && set_calls == 0);
    task_count = 1;
    CHECK(!lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_IDLE));
    CHECK(set_calls == 1 && selected == 19);
    set_error = EACCES;
    CHECK(lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_NORMAL) == -1 && errno == EACCES);
    CHECK(set_calls == 2 && selected == 0);
    read_error = ENOMEM;
    CHECK(lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_IDLE) == -1 && errno == ENOMEM);
    CHECK(set_calls == 2);
    read_error = 0;
    task_count = 0;
    CHECK(lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_NORMAL) == -1 && errno == ENOTSUP);
    CHECK(set_calls == 2);
    open_error = EACCES;
    CHECK(lx_host_process_set_priority(LX_HOST_PROCESS_PRIORITY_NORMAL) == -1 && errno == ENOTSUP);
    CHECK(set_calls == 2);
    puts(
        "Linux native-task preflight, unchanged no-op, single-task scheduling and native errors passed");
    return 0;
}
