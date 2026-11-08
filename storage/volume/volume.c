/* Public domain. No rights reserved. Wholly AI-authored. */
/* Image volume operations, dispatched to HPFS, FatFs (FAT32, exFAT and partitioned FAT)
   or the built-in ISO 9660 and FAT12/16 code. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/volume.h"
#include "storage/volume/volume_internal.h"
#include "storage/volume/volume_disk.h"
#include "storage/filesystems/fat/fat12_16.h"
#include "storage/common/host_stdio.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int tb_image_info(const char *path, TbImageInfo *info)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_info(path, info);
    if (tb_fat_volume(path))
        return tb_fat_info(path, info);
    return volume_disk_info(path, info);
}

char *tb_image_canonical(const char *path)
{
    TbImageInfo info;
    return tb_image_info(path, &info) ? tb_image_absolute(path) : NULL;
}

char **tb_image_names(const char *path, size_t *count)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_names(path, count);
    if (tb_fat_volume(path))
        return tb_fat_names(path, count);
    return volume_disk_names(path, count);
}

FILE *tb_image_fopen(const char *path)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_fopen(path);
    if (tb_fat_volume(path))
        return tb_fat_fopen(path);
    return volume_disk_fopen(path);
}

int tb_image_write(const char *path, const char *expected, uint64_t offset, const void *bytes,
                   size_t size)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_WRITE, offset, bytes, size, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_WRITE, offset, bytes, size, NULL);
    return fat12_16_write(path, expected, offset, bytes, size);
}

int tb_image_resize(const char *path, const char *expected, uint64_t size)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_RESIZE, size, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_RESIZE, size, NULL, 0, NULL);
    return fat12_16_resize(path, expected, size);
}

int tb_image_create_file(const char *path, int exclusive)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, exclusive ? TB_FS_EXCLUSIVE : TB_FS_CREATE, 0, NULL, 0,
                              NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, exclusive ? TB_FS_EXCLUSIVE : TB_FS_CREATE, 0, NULL, 0,
                             NULL);
    return fat12_16_create_file(path, exclusive);
}

int tb_image_remove(const char *path, int directory)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, directory ? TB_FS_RMDIR : TB_FS_DELETE, 0, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, directory ? TB_FS_RMDIR : TB_FS_DELETE, 0, NULL, 0, NULL);
    return fat12_16_remove(path, directory);
}

int tb_image_mkdir(const char *path)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, TB_FS_MKDIR, 0, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, TB_FS_MKDIR, 0, NULL, 0, NULL);
    return fat12_16_mkdir(path);
}

int tb_image_readonly(const char *path, int readonly)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, readonly ? TB_FS_READONLY : TB_FS_WRITABLE, 0, NULL, 0,
                              NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, readonly ? TB_FS_READONLY : TB_FS_WRITABLE, 0, NULL, 0,
                             NULL);
    return fat12_16_readonly(path, readonly);
}

int tb_image_stamps(const char *path, const char *expected, const TbImageStamps *stamps)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_TOUCH, 0, stamps, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_TOUCH, 0, stamps, 0, NULL);
    return fat12_16_stamps(path, expected, stamps);
}

int tb_image_attributes(const char *path, const char *expected, unsigned attributes)
{
    attributes &= 0x26;
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_ATTRIBUTES, attributes, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_ATTRIBUTES, attributes, NULL, 0, NULL);
    return fat12_16_attributes(path, expected, attributes);
}

int tb_image_move(const char *source, const char *target)
{
    if (tb_hpfs_volume(source))
        return tb_hpfs_modify(source, NULL, TB_FS_RENAME, 0, NULL, 0, target);
    if (tb_fat_volume(source))
        return tb_fat_modify(source, NULL, TB_FS_RENAME, 0, NULL, 0, target);
    return fat12_16_move(source, target);
}

int tb_image_copy(FILE *source, const char *destination, int exclusive)
{
    if (fseeko(source, 0, SEEK_END))
        return 0;
    int64_t size = ftello(source);
    if (size < 0 || (uint64_t)size > SIZE_MAX || (uint64_t)size > UINT32_MAX)
        return volume_failure(EFBIG);
    unsigned char *bytes = malloc(size ? (size_t)size : 1);
    if (!bytes)
        return 0;
    int ok = !fseeko(source, 0, SEEK_SET) && fread(bytes, 1, (size_t)size, source) == (size_t)size;
    if (ok)
        ok = tb_image_create_file(destination, exclusive);
    if (ok)
        ok = tb_image_resize(destination, NULL, 0) &&
             tb_image_write(destination, NULL, 0, bytes, (size_t)size);
    free(bytes);
    return ok;
}

int tb_image_create_volume(const char *path, const char *filesystem, uint64_t bytes)
{
    if (!strcmp(filesystem, "hpfs"))
        return tb_hpfs_format(path, bytes);
    if (!strcmp(filesystem, "fat32") || !strcmp(filesystem, "exfat"))
        return tb_fat_format(path, filesystem, bytes);
    return volume_failure(ENOTSUP);
}

int tb_image_metadata_get(const char *path, const char *expected, void *data, size_t capacity,
                          size_t *size)
{
    *size = 0;
    char *current = tb_image_source(path);
    if (!current)
        return 0;
    int same = !expected || !strcmp(current, expected);
    free(current);
    if (!same)
        return volume_failure(ENXIO);
    if (tb_hpfs_volume(path))
        return tb_hpfs_metadata_get(path, expected, data, capacity, size);
    return 1;
}

int tb_image_metadata_set(const char *path, const char *expected, const void *data, size_t size)
{
    int readonly = tb_image_protected(path);
    if (readonly < 0)
        return 0;
    if (readonly)
        return volume_failure(EROFS);
    if (tb_hpfs_volume(path))
        return tb_hpfs_metadata_set(path, expected, data, size);
    return volume_failure(ENOTSUP);
}
