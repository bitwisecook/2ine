/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native host-file version identity for persistent profile caches. */
#define _DARWIN_C_SOURCE 1
#include "hal/posix/fs_internal.h"
#include <sys/stat.h>
int lx_host_path_native_identity(const char *path, LxHostFileIdentity *identity)
{
    struct stat native;
    if (stat(path, &native))
        return -1;
    *identity = (LxHostFileIdentity){.device = (uint64_t)native.st_dev,
                                     .file = (uint64_t)native.st_ino,
                                     .size = native.st_size < 0 ? 0 : (uint64_t)native.st_size,
                                     .modified = native.st_mtime,
                                     .changed = native.st_ctime,
                                     .modified_nanoseconds = (uint32_t)native.st_mtimespec.tv_nsec,
                                     .changed_nanoseconds = (uint32_t)native.st_ctimespec.tv_nsec};
    return 0;
}
