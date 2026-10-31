/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared backing for guest address spaces, without guest ABI or OS/2 policy. */
#define _GNU_SOURCE 1
#include "host_shared.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
struct LxHostShared
{
    int fd;
    size_t size;
};
LxHostSharedResult lx_host_shared_open(const char *name, size_t size, LxHostShared **output)
{
    *output = NULL;
    LxHostShared *s = calloc(1, sizeof *s);
    if (!s)
        return LX_HOST_SHARED_NOMEM;
    const int create = size != 0;
    s->fd = shm_open(name, O_RDWR | (create ? O_CREAT | O_EXCL : 0), 0600);
    if (s->fd == -1)
    {
        int error = errno;
        free(s);
        return error == ENOENT   ? LX_HOST_SHARED_MISSING
               : error == EEXIST ? LX_HOST_SHARED_EXISTS
                                 : LX_HOST_SHARED_DENIED;
    }
    struct stat info;
    if ((create && ftruncate(s->fd, (off_t)size)) || fstat(s->fd, &info) || info.st_size <= 0 ||
        (uint64_t)info.st_size > SIZE_MAX)
    {
        close(s->fd);
        if (create)
            shm_unlink(name);
        free(s);
        return LX_HOST_SHARED_DENIED;
    }
    s->size = (size_t)info.st_size;
    *output = s;
    return LX_HOST_SHARED_OK;
}
void *lx_host_shared_map(LxHostShared *s, void *hint, int exact)
{
    void *view = mmap(hint, s->size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
    if (view == MAP_FAILED)
        return NULL;
    if (exact && view != hint)
    {
        munmap(view, s->size);
        return NULL;
    }
    return view;
}
void lx_host_shared_unmap(void *view, size_t size)
{
    munmap(view, size);
}
void lx_host_shared_close(LxHostShared *s)
{
    close(s->fd);
    free(s);
}
size_t lx_host_shared_size(const LxHostShared *s)
{
    return s->size;
}
