/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent opaque file metadata in a private extended-attribute namespace. */
#include "hal/posix/fs_internal.h"
#include "ball/disk_image.h"
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
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
    return fsetxattr(file->fd, "com.twoine.metadata", data, size, 0, 0) == 0 ? LX_HOST_FS_OK
                                                                             : metadata_error();
}
LxHostFsError lx_host_file_metadata_get(LxHostFile *file, void *data, size_t capacity, size_t *size)
{
    if (file->image)
        return tb_image_metadata_get(file->image_path, file->image_source, data, capacity, size)
                   ? LX_HOST_FS_OK
                   : metadata_error();
    ssize_t count = fgetxattr(file->fd, "com.twoine.metadata", data, capacity, 0, 0);
    if (count < 0)
    {
        *size = 0;
        return errno == ENOATTR ? LX_HOST_FS_OK : metadata_error();
    }
    *size = (size_t)count;
    return LX_HOST_FS_OK;
}
void lx_host_native_extras(int fd, const char *path, LxHostFileInfo *info)
{
    uint32_t attributes;
    ssize_t count = fd >= 0 ? fgetxattr(fd, "com.twoine.attributes", &attributes,
                                        sizeof attributes, 0, 0)
                            : getxattr(path, "com.twoine.attributes", &attributes,
                                       sizeof attributes, 0, 0);
    if (count == sizeof attributes)
        info->attributes = attributes;
    struct stat native;
    if (!(fd >= 0 ? fstat(fd, &native) : stat(path, &native)))
        info->created = native.st_birthtimespec.tv_sec;
}
LxHostFsError lx_host_native_set_created(int fd, const char *path, int64_t seconds)
{
    struct attrlist attributes;
    memset(&attributes, 0, sizeof attributes);
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_CRTIME;
    struct timespec created = {.tv_sec = (time_t)seconds};
    int failed = fd >= 0 ? fsetattrlist(fd, &attributes, &created, sizeof created, 0)
                         : setattrlist(path, &attributes, &created, sizeof created, 0);
    return failed ? metadata_error() : LX_HOST_FS_OK;
}
LxHostFsError lx_host_native_set_attributes(int fd, const char *path, unsigned attributes)
{
    uint32_t value = attributes;
    int failed = fd >= 0 ? fsetxattr(fd, "com.twoine.attributes", &value, sizeof value, 0, 0)
                         : setxattr(path, "com.twoine.attributes", &value, sizeof value, 0, 0);
    return failed ? metadata_error() : LX_HOST_FS_OK;
}
