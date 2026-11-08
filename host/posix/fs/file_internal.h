/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private POSIX file representation shared by filesystem and metadata adapters. */
#ifndef TWOINE_HOST_POSIX_FS_FILE_INTERNAL_H
#define TWOINE_HOST_POSIX_FS_FILE_INTERNAL_H
#include "host/fs.h"
struct LxImageLock;

struct LxHostFile
{
    int fd;
    int image;
    int image_writable;
    int image_readonly;
    char *image_path, *image_source;
    struct LxImageLock *image_lock;
    int archive_checked;
};

/* Platform stat layouts stay inside the selected macOS/Linux adapter. */
int lx_host_path_native_identity(const char *path, LxHostFileIdentity *identity);
void lx_host_file_dispose(LxHostFile *file);
/* A negative descriptor selects the path. Host platforms keep what stat cannot express. */
void lx_host_native_extras(int fd, const char *path, LxHostFileInfo *info);
LxHostFsError lx_host_native_set_created(int fd, const char *path, int64_t seconds);
LxHostFsError lx_host_native_set_attributes(int fd, const char *path, unsigned attributes);
#endif
