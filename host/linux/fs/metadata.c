/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent opaque file metadata in a private extended-attribute namespace. */
#define _GNU_SOURCE 1
#include "host/posix/fs/file_internal.h"
#include "storage/volume.h"
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

static ssize_t extra_read(int fd, const char *path, const char *name, void *data, size_t size)
{
    return fd >= 0 ? fgetxattr(fd, name, data, size) : getxattr(path, name, data, size);
}

static LxHostFsError extra_write(int fd, const char *path, const char *name, const void *data,
                                 size_t size)
{
    int failed = fd >= 0 ? fsetxattr(fd, name, data, size, 0) : setxattr(path, name, data, size, 0);
    return failed ? metadata_error() : LX_HOST_FS_OK;
}

/* Linux cannot rewrite a birth time, so a guest-set value is retained beside the file. */
void lx_host_native_extras(int fd, const char *path, LxHostFileInfo *info)
{
    uint32_t attributes;
    int64_t created;
    if (extra_read(fd, path, "user.2ine.attributes", &attributes, sizeof attributes) ==
        sizeof attributes)
        info->attributes = attributes;
    if (extra_read(fd, path, "user.2ine.created", &created, sizeof created) == sizeof created)
    {
        info->created = created;
        return;
    }
    struct statx native;
    if (!statx(fd >= 0 ? fd : AT_FDCWD, fd >= 0 ? "" : path, fd >= 0 ? AT_EMPTY_PATH : 0,
               STATX_BTIME, &native) &&
        (native.stx_mask & STATX_BTIME))
        info->created = native.stx_btime.tv_sec;
}

LxHostFsError lx_host_native_set_created(int fd, const char *path, int64_t seconds)
{
    return extra_write(fd, path, "user.2ine.created", &seconds, sizeof seconds);
}

LxHostFsError lx_host_native_set_attributes(int fd, const char *path, unsigned attributes)
{
    uint32_t value = attributes;
    return extra_write(fd, path, "user.2ine.attributes", &value, sizeof value);
}
