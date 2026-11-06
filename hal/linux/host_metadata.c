/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent opaque file metadata in a private extended-attribute namespace. */
#define _GNU_SOURCE 1
#include "hal/posix/fs_internal.h"
#include "ball/disk_image.h"
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
static LxHostFsError metadata_error(void)
{
    if (errno == EROFS)
        return LX_HOST_FS_READ_ONLY;
    if (errno == ENXIO)
        return LX_HOST_FS_NOT_READY;
    if (errno == ENOMEM)
        return LX_HOST_FS_NO_MEMORY;
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
    if (file->image)
        return tb_image_metadata_set(file->image_path, file->image_source, data, size)
                   ? LX_HOST_FS_OK
                   : metadata_error();
    return fsetxattr(file->fd, "user.2ine.metadata", data, size, 0) == 0 ? LX_HOST_FS_OK
                                                                         : metadata_error();
}
LxHostFsError lx_host_file_metadata_get(LxHostFile *file, void *data, size_t capacity, size_t *size)
{
    if (file->image)
        return tb_image_metadata_get(file->image_path, file->image_source, data, capacity, size)
                   ? LX_HOST_FS_OK
                   : metadata_error();
    ssize_t count = fgetxattr(file->fd, "user.2ine.metadata", data, capacity);
    if (count < 0)
    {
        *size = 0;
        return errno == ENODATA ? LX_HOST_FS_OK : metadata_error();
    }
    *size = (size_t)count;
    return LX_HOST_FS_OK;
}
/* Linux cannot rewrite a birth time, so a guest-set value is retained beside the file. */
LxHostFsError lx_host_file_created(LxHostFile *file, int64_t *seconds)
{
    *seconds = 0;
    if (file->image)
        return LX_HOST_FS_OK;
    if (fgetxattr(file->fd, "user.2ine.created", seconds, sizeof *seconds) == sizeof *seconds)
        return LX_HOST_FS_OK;
    struct statx native;
    if (!statx(file->fd, "", AT_EMPTY_PATH, STATX_BTIME, &native) &&
        (native.stx_mask & STATX_BTIME))
        *seconds = native.stx_btime.tv_sec;
    return LX_HOST_FS_OK;
}
LxHostFsError lx_host_file_set_created(LxHostFile *file, int64_t seconds)
{
    if (file->image)
        return LX_HOST_FS_INVALID;
    return fsetxattr(file->fd, "user.2ine.created", &seconds, sizeof seconds, 0) == 0
               ? LX_HOST_FS_OK
               : metadata_error();
}
