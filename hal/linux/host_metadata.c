/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent opaque file metadata in a private extended-attribute namespace. */
#include "hal/posix/fs_internal.h"
#include <sys/xattr.h>
#include <errno.h>
static LxHostFsError metadata_error(void)
{
    if (errno == ENOTSUP || errno == EOPNOTSUPP)
        return LX_HOST_FS_INVALID;
    if (errno == EACCES || errno == EPERM)
        return LX_HOST_FS_DENIED;
    if (errno == ENOSPC || errno == EDQUOT)
        return LX_HOST_FS_FULL;
    return LX_HOST_FS_IO;
}
LxHostFsError lx_host_file_metadata_set(LxHostFile *file, const void *data, size_t size)
{
    return fsetxattr(file->fd, "user.2ine.metadata", data, size, 0) == 0 ? LX_HOST_FS_OK
                                                                         : metadata_error();
}
LxHostFsError lx_host_file_metadata_get(LxHostFile *file, void *data, size_t capacity, size_t *size)
{
    ssize_t count = fgetxattr(file->fd, "user.2ine.metadata", data, capacity);
    if (count < 0)
    {
        *size = 0;
        return errno == ENODATA ? LX_HOST_FS_OK : metadata_error();
    }
    *size = (size_t)count;
    return LX_HOST_FS_OK;
}
