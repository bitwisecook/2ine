/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_DISK_IMAGE_H
#define TB_DISK_IMAGE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "read_stream.h"
/* Internal paths name an image plus a member; they never name a host mount. */
typedef struct TbImageInfo
{
    uint64_t size, capacity, available;
    int64_t modified;         /* INT64_MIN when the media has no timestamp. */
    int64_t accessed, created; /* Zero when the entry does not record one. */
    unsigned attributes;       /* Hidden, system and archived bits of the entry. */
    int directory;
    int readonly;
    int fat;
    uint32_t serial;
    unsigned sector_bytes, cluster_bytes;
    unsigned reserved_sectors, fat_count, root_entries, fat_sectors, sectors_per_track, heads,
        media;
    uint32_t hidden_sectors;
    char label[12];
    char filesystem[8];
} TbImageInfo;
int tb_image_path(const char *path);
int tb_image_device(const char *path);
int tb_image_device_kind(const char *path);
char *tb_image_volume_root(const char *path);
char *tb_image_device_root(const char *control);
/* root/source/canonical return owned strings; member borrows the input path. */
char *tb_image_root(const char *source);
char *tb_image_source(const char *path);
const char *tb_image_member(const char *path);
char *tb_image_canonical(const char *path);
char *tb_image_absolute(const char *path);
int tb_image_info(const char *path, TbImageInfo *info);
char **tb_image_names(const char *path, size_t *count);
/* Returns an anonymous, private snapshot of one member, positioned at zero. */
FILE *tb_image_fopen(const char *path);
int tb_image_metadata_get(const char *path, const char *expected, void *data, size_t capacity,
                          size_t *size);
int tb_image_metadata_set(const char *path, const char *expected, const void *data, size_t size);
int tb_image_create_volume(const char *path, const char *filesystem, uint64_t bytes);
int tb_image_create_floppy(const char *path, unsigned kib);
int tb_image_create_file(const char *path, int exclusive);
int tb_image_write(const char *path, const char *expected_source, uint64_t offset, const void *data,
                   size_t size);
int tb_image_resize(const char *path, const char *expected_source, uint64_t size);
int tb_image_remove(const char *path, int directory);
int tb_image_mkdir(const char *path);
int tb_image_move(const char *source, const char *target);
int tb_image_readonly(const char *path, int readonly);
/* Sets entry dates in the volume itself. Seconds are since the Unix epoch; INT64_MIN skips. */
typedef struct TbImageStamps
{
    int64_t created, accessed, modified;
} TbImageStamps;
int tb_image_stamps(const char *path, const char *expected, const TbImageStamps *stamps);
int tb_image_attributes(const char *path, const char *expected, unsigned attributes);
int tb_image_copy(FILE *source, const char *destination, int exclusive);
typedef int (*TbImageVisitor)(void *, const char *, uint64_t, int, int64_t);
int tb_image_stream_walk(FILE *file, TbImageVisitor visitor, void *context);
int tb_image_reader_walk(TbReadStream *stream, TbImageVisitor visitor, void *context);
int tb_image_reader_list(TbReadStream *stream, const char *member, TbImageVisitor visitor,
                         void *context);
void tb_image_reader_cache_stats(TbReadStream *stream, uint64_t *builds, uint64_t *hits);
TbReadStream *tb_image_reader_member(TbReadStream *stream, const char *member);
FILE *tb_image_stream_fopen(FILE *file, const char *member);
#endif
