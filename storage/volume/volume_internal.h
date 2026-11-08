/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_IMAGE_INTERNAL_H
#define TB_IMAGE_INTERNAL_H
#include "storage/volume.h"
/* Internal filesystem implementations share source access and device policy. */
FILE *tb_image_open_source(const char *source, int write);
int tb_image_lock_source(FILE *file, int write);
int tb_image_protected(const char *path);
/* Decodes an image path, or reads a device's control file for its current medium and kind
   (1 CD, 2 floppy, 3 disk) and read-only flag; readonly may be NULL. Returns an owned path. */
char *tb_image_resolve_source(const char *path, int *kind, int *readonly);
int tb_hpfs_volume(const char *path);
int tb_hpfs_info(const char *path, TbImageInfo *info);
char **tb_hpfs_names(const char *path, size_t *count);
FILE *tb_hpfs_fopen(const char *path);
int tb_hpfs_modify(const char *path, const char *expected, int action, uint64_t offset,
                   const void *data, size_t size, const char *target);
int tb_hpfs_metadata_get(const char *path, const char *expected, void *data, size_t capacity,
                         size_t *size);
int tb_hpfs_metadata_set(const char *path, const char *expected, const void *data, size_t size);
int tb_hpfs_format(const char *path, uint64_t bytes);
int tb_fat_volume(const char *path);
int tb_fat_info(const char *path, TbImageInfo *info);
char **tb_fat_names(const char *path, size_t *count);
FILE *tb_fat_fopen(const char *path);
int tb_fat_modify(const char *path, const char *expected, int action, uint64_t offset,
                  const void *data, size_t size, const char *target);
int tb_fat_format(const char *path, const char *filesystem, uint64_t bytes);

enum
{
    TB_FS_CREATE,
    TB_FS_EXCLUSIVE,
    TB_FS_WRITE,
    TB_FS_RESIZE,
    TB_FS_DELETE,
    TB_FS_RMDIR,
    TB_FS_MKDIR,
    TB_FS_RENAME,
    TB_FS_READONLY,
    TB_FS_WRITABLE,
    TB_FS_TOUCH,
    TB_FS_ATTRIBUTES
};
#endif
