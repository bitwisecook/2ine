/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pipe-gated wrapper, grandchild and independent service operation contracts. */
#define _POSIX_C_SOURCE 200809L
#include "hal/host_operation.h"
#include "hal/host_process.h"
#include "hal/host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

enum
{
    ROOT_LIFE,
    WORKER_READY,
    WORKER_LIFE,
    WORKER_GATE,
    SERVICE_READY,
    SERVICE_LIFE,
    SERVICE_GATE,
    TEST_FDS
};
static int endpoint(const char *directory, unsigned index, int flags)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%u", directory, index);
    int fd = open(path, flags | O_CLOEXEC | O_NONBLOCK);
    if (fd >= 0 && (flags & O_ACCMODE) == O_RDONLY)
        fcntl(fd, F_SETFL, flags);
    return fd;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--operation"))
        return lx_host_operation_supervise(argv[0], argv + 2, (char *const *)lx_host_environment());
    if (argc != 7 || !lx_host_operation_join())
        return 2;
    int service = atoi(argv[4]), root_result = atoi(argv[5]), ignore_term = atoi(argv[6]);
    if (!strcmp(argv[1], "service"))
    {
        if (getenv(LX_HOST_OPERATION_LEASE) || getenv(LX_HOST_OPERATION_OWNER))
            return 3;
        if (endpoint(argv[3], SERVICE_LIFE, O_WRONLY) < 0)
            return 12;
        int ready = endpoint(argv[3], SERVICE_READY, O_WRONLY);
        dprintf(ready, "%u\n", (unsigned)getpid());
        close(ready);
        char go;
        int gate = endpoint(argv[3], SERVICE_GATE, O_RDONLY);
        return read(gate, &go, 1) == 1 ? 0 : 4;
    }
    if (!getenv(LX_HOST_OPERATION_LEASE) || !getenv(LX_HOST_OPERATION_OWNER))
        return 5;
    if (!strcmp(argv[1], "worker"))
    {
        if (endpoint(argv[3], WORKER_LIFE, O_WRONLY) < 0)
            return 12;
        if (ignore_term)
            signal(SIGTERM, SIG_IGN);
        int ready = endpoint(argv[3], WORKER_READY, O_WRONLY);
        dprintf(ready, "%u\n", (unsigned)getpid());
        close(ready);
        char go;
        int gate = endpoint(argv[3], WORKER_GATE, O_RDONLY);
        if (read(gate, &go, 1) != 1)
            return 6;
        int file = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (file < 0 || write(file, "MZinstalled", 11) != 11 || close(file))
            return 7;
        return 0;
    }
    char *child_arguments[8];
    for (int i = 0; i <= argc; i++)
        child_arguments[i] = argv[i];
    if (!strcmp(argv[1], "middle"))
    {
        child_arguments[1] = "worker";
        LxHostChild *child =
            lx_host_child_spawn(argv[0], child_arguments, (char *const *)lx_host_environment());
        if (!child)
            return 8;
        lx_host_child_release(child);
        return 17;
    }
    if (strcmp(argv[1], "wrapper"))
        return 9;
    if (endpoint(argv[3], ROOT_LIFE, O_WRONLY) < 0)
        return 12;
    child_arguments[1] = "middle";
    LxHostChild *child =
        lx_host_child_spawn(argv[0], child_arguments, (char *const *)lx_host_environment());
    if (!child)
        return 10;
    lx_host_child_release(child);
    if (service)
    {
        child_arguments[1] = "service";
        child = lx_host_child_spawn_service(argv[0], child_arguments,
                                            (char *const *)lx_host_environment());
        if (!child)
            return 11;
        lx_host_child_release(child);
    }
    return root_result;
}
