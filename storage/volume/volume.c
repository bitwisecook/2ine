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

/* One filesystem implementation. Volumes are probed in table order; the first whose probe
   accepts the path handles the operation. */
typedef struct TbVolumeOps
{
    /* NULL accepts every path, so the entry with it comes last. */
    int (*probe)(const char *path);
    int (*info)(const char *path, TbImageInfo *info);
    char **(*names)(const char *path, size_t *count);
    FILE *(*open_member)(const char *path);
    /* action is a TB_FS_* value; see tb_image_* below for the use of each argument. */
    int (*modify)(const char *path, const char *expected, int action, uint64_t offset,
                  const void *data, size_t size, const char *target);
    /* Extended attributes; NULL when the filesystem keeps none. */
    int (*metadata_get)(const char *path, const char *expected, void *data, size_t capacity,
                        size_t *size);
    int (*metadata_set)(const char *path, const char *expected, const void *data, size_t size);
    /* NULL-terminated tb_image_create_volume names this implementation formats, or NULL. */
    const char *const *formats;
    int (*format)(const char *path, const char *filesystem, uint64_t bytes);
} TbVolumeOps;

static int hpfs_format(const char *path, const char *filesystem, uint64_t bytes)
{
    (void)filesystem;
    return tb_hpfs_format(path, bytes);
}

static const char *const hpfs_formats[] = {"hpfs", NULL};
static const char *const fatfs_formats[] = {"fat32", "exfat", NULL};

static const TbVolumeOps hpfs_volume = {
    .probe = tb_hpfs_volume,
    .info = tb_hpfs_info,
    .names = tb_hpfs_names,
    .open_member = tb_hpfs_fopen,
    .modify = tb_hpfs_modify,
    .metadata_get = tb_hpfs_metadata_get,
    .metadata_set = tb_hpfs_metadata_set,
    .formats = hpfs_formats,
    .format = hpfs_format,
};

/* FAT32, exFAT and MBR-partitioned FAT. */
static const TbVolumeOps fatfs_volume = {
    .probe = tb_fat_volume,
    .info = tb_fat_info,
    .names = tb_fat_names,
    .open_member = tb_fat_fopen,
    .modify = tb_fat_modify,
    .formats = fatfs_formats,
    .format = tb_fat_format,
};

/* ISO 9660/Joliet and unpartitioned FAT12/16, including empty removable devices. */
static const TbVolumeOps builtin_volume = {
    .info = volume_disk_info,
    .names = volume_disk_names,
    .open_member = volume_disk_fopen,
    .modify = fat12_16_modify,
};

static const TbVolumeOps *const volumes[] = {&hpfs_volume, &fatfs_volume, &builtin_volume};

/* Only implementations with extended attributes are probed for metadata requests. */
static const TbVolumeOps *volume_for(const char *path, int metadata)
{
    for (size_t i = 0; i < sizeof volumes / sizeof volumes[0]; i++)
    {
        const TbVolumeOps *ops = volumes[i];
        if (metadata && !ops->metadata_get)
            continue;
        if (!ops->probe || ops->probe(path))
            return ops;
    }
    return NULL;
}

static int modify(const char *path, const char *expected, int action, uint64_t offset,
                  const void *data, size_t size, const char *target)
{
    return volume_for(path, 0)->modify(path, expected, action, offset, data, size, target);
}

int tb_image_info(const char *path, TbImageInfo *info)
{
    return volume_for(path, 0)->info(path, info);
}

char *tb_image_canonical(const char *path)
{
    TbImageInfo info;
    return tb_image_info(path, &info) ? tb_image_absolute(path) : NULL;
}

char **tb_image_names(const char *path, size_t *count)
{
    return volume_for(path, 0)->names(path, count);
}

FILE *tb_image_fopen(const char *path)
{
    return volume_for(path, 0)->open_member(path);
}

int tb_image_write(const char *path, const char *expected, uint64_t offset, const void *bytes,
                   size_t size)
{
    return modify(path, expected, TB_FS_WRITE, offset, bytes, size, NULL);
}

int tb_image_resize(const char *path, const char *expected, uint64_t size)
{
    return modify(path, expected, TB_FS_RESIZE, size, NULL, 0, NULL);
}

int tb_image_create_file(const char *path, int exclusive)
{
    return modify(path, NULL, exclusive ? TB_FS_EXCLUSIVE : TB_FS_CREATE, 0, NULL, 0, NULL);
}

int tb_image_remove(const char *path, int directory)
{
    return modify(path, NULL, directory ? TB_FS_RMDIR : TB_FS_DELETE, 0, NULL, 0, NULL);
}

int tb_image_mkdir(const char *path)
{
    return modify(path, NULL, TB_FS_MKDIR, 0, NULL, 0, NULL);
}

int tb_image_readonly(const char *path, int readonly)
{
    return modify(path, NULL, readonly ? TB_FS_READONLY : TB_FS_WRITABLE, 0, NULL, 0, NULL);
}

int tb_image_stamps(const char *path, const char *expected, const TbImageStamps *stamps)
{
    return modify(path, expected, TB_FS_TOUCH, 0, stamps, 0, NULL);
}

int tb_image_attributes(const char *path, const char *expected, unsigned attributes)
{
    attributes &= FAT_ATTRIBUTES_SETTABLE;
    return modify(path, expected, TB_FS_ATTRIBUTES, attributes, NULL, 0, NULL);
}

/* The source path selects the implementation; each one rejects a target on another volume. */
int tb_image_move(const char *source, const char *target)
{
    return modify(source, NULL, TB_FS_RENAME, 0, NULL, 0, target);
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
    for (size_t i = 0; i < sizeof volumes / sizeof volumes[0]; i++)
        for (const char *const *name = volumes[i]->formats; name && *name; name++)
            if (!strcmp(filesystem, *name))
                return volumes[i]->format(path, filesystem, bytes);
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
    const TbVolumeOps *ops = volume_for(path, 1);
    return ops ? ops->metadata_get(path, expected, data, capacity, size) : 1;
}

int tb_image_metadata_set(const char *path, const char *expected, const void *data, size_t size)
{
    int readonly = tb_image_protected(path);
    if (readonly < 0)
        return 0;
    if (readonly)
        return volume_failure(EROFS);
    const TbVolumeOps *ops = volume_for(path, 1);
    return ops ? ops->metadata_set(path, expected, data, size) : volume_failure(ENOTSUP);
}
