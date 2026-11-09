/* Public domain. No rights reserved. Wholly AI-authored. */
/* Virtual paths that name a disk image or a removable-media device and a member in it. */
#ifndef TB_STORAGE_IMAGE_PATH_H
#define TB_STORAGE_IMAGE_PATH_H
#include <stddef.h>

/* /@2ine-image/<hex source>/member names an image file; /@2ine-device/<hex control>/member
   names a device whose control file selects the current medium. */
int tb_image_path(const char *path);
int tb_image_device(const char *path);
int tb_image_device_kind(const char *path);
char *tb_image_volume_root(const char *path);
char *tb_image_device_root(const char *control);
/* root/source/absolute return owned strings; member borrows the input path. */
char *tb_image_root(const char *source);
char *tb_image_source(const char *path);
const char *tb_image_member(const char *path);
char *tb_image_absolute(const char *path);

/* A device control file names the device kind ("cdrom", "floppy" or "disk"),
   its read-only flag and the current medium (NULL when none is inserted).
   CD devices are always read-only. Returns an owned buffer of *size bytes, or
   NULL with errno ENAMETOOLONG when the medium path is longer than the file
   holds. The storage layer reads the file again for every operation, so a
   rewritten control file swaps the medium of a running guest. */
unsigned char *tb_image_device_control(const char *kind, int readonly, const char *medium,
                                       size_t *size);
#endif
