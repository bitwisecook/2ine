/* Public domain. No rights reserved. Wholly AI-authored. */
/* Linux open-file-description locks survive closure of independent descriptors. */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "hal/posix/fs_internal.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
static LxHostFsError range_error(int error)
{
    return error == EACCES || error == EBADF       ? LX_HOST_FS_DENIED
           : error == EINVAL || error == EOVERFLOW ? LX_HOST_FS_INVALID
                                                   : LX_HOST_FS_IO;
}
void lx_host_file_dispose(LxHostFile *file)
{
    close(file->fd);
    free(file);
}
LxHostFsError lx_host_file_lock_range(LxHostFile *file, uint64_t offset, uint64_t length,
                                      int acquire)
{
    if (!file || !length || offset > INT64_MAX || length > (uint64_t)INT64_MAX - offset)
        return LX_HOST_FS_INVALID;
    struct flock lock = {0};
    lock.l_type = acquire ? F_WRLCK : F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = (off_t)offset;
    lock.l_len = (off_t)length;
#ifdef F_OFD_SETLK
    const int command = F_OFD_SETLK;
#else
    const int command = F_SETLK;
#endif
    int result;
    do
        result = fcntl(file->fd, command, &lock);
    while (result < 0 && errno == EINTR);
    if (!result)
        return LX_HOST_FS_OK;
    return errno == EACCES || errno == EAGAIN ? LX_HOST_FS_BUSY : range_error(errno);
}
LxHostFsError lx_host_file_range_conflict(LxHostFile *file, uint64_t offset, uint64_t length,
                                          int *conflict)
{
    if (!file || !conflict || offset > INT64_MAX || length > (uint64_t)INT64_MAX - offset)
        return LX_HOST_FS_INVALID;
    *conflict = 0;
    if (!length)
        return LX_HOST_FS_OK;
    struct flock lock = {0};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = (off_t)offset;
    lock.l_len = (off_t)length;
#ifdef F_OFD_GETLK
    const int command = F_OFD_GETLK;
#else
    const int command = F_GETLK;
#endif
    if (fcntl(file->fd, command, &lock))
        return range_error(errno);
    *conflict = lock.l_type != F_UNLCK;
    return LX_HOST_FS_OK;
}
