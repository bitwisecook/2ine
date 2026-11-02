/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private POSIX file representation shared by filesystem and metadata adapters. */
#ifndef TWOINE_POSIX_FS_INTERNAL_H
#define TWOINE_POSIX_FS_INTERNAL_H
#include "hal/host_fs.h"
struct LxImageLock;
struct LxHostFile
{
    int fd;
    int image;
    int image_writable;
    int image_readonly;
    char *image_path, *image_source;
    struct LxImageLock *image_lock;
};
void lx_host_file_dispose(LxHostFile *file);
#endif
