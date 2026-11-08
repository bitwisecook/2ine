/* Public domain. No rights reserved. Wholly AI-authored. */
/* Built-in ISO 9660 and FAT12/16 volume state shared by the readers, writer and index. */
#ifndef TB_STORAGE_VOLUME_DISK_H
#define TB_STORAGE_VOLUME_DISK_H
#include "storage/volume.h"
#include <stdint.h>
#include <stdio.h>

/* One directory entry. ISO entries locate data by offset; FAT entries by first cluster,
   with record the image offset of their 32-byte directory entry. */
typedef struct VolumeNode
{
    uint64_t offset, size;
    int64_t modified, accessed, created;
    uint32_t cluster;
    int directory;
    uint64_t record;
    unsigned attributes;
} VolumeNode;

/* An open volume read from a file, a read stream or an in-memory staged copy. fat_bits is
   zero for ISO 9660. */
typedef struct VolumeDisk
{
    struct DirectoryIndex *index;
    FILE *file;
    TbReadStream *stream;
    unsigned char *memory;
    int readonly;
    uint64_t bytes;
    unsigned fat_bits, sector_bytes, cluster_bytes;
    unsigned reserved_sectors, fat_count, root_entries, fat_sectors, sectors_per_track, heads,
        media;
    uint32_t hidden_sectors;
    uint32_t clusters;
    uint64_t data_offset;
    unsigned char *fat;
    size_t fat_size;
    int joliet;
    uint32_t serial;
    char label[12];
    VolumeNode root;
} VolumeDisk;

/* Returns 1 to continue, 0 to stop successfully and a negative value to fail. */
typedef int (*VolumeEntryCallback)(void *, const char *, const VolumeNode *);

/* Sets errno and returns 0. */
int volume_failure(int error);
int volume_read_at(VolumeDisk *d, uint64_t offset, void *data, size_t n);
void volume_close(VolumeDisk *d);
int volume_identify(VolumeDisk *d);
int volume_open(const char *path, VolumeDisk *d, const char **member, int write);
unsigned char *volume_directory_data(VolumeDisk *d, VolumeNode node, size_t *size);
int volume_utf16_name(const uint16_t *text, size_t n, char *out, size_t capacity);
int volume_safe_name(const char *name);
int volume_read_entries(VolumeDisk *d, VolumeNode directory, VolumeEntryCallback callback,
                        void *context);
int volume_lookup(VolumeDisk *d, const char *member, VolumeNode *node);
/* Copies one member into an anonymous temporary file positioned at zero. */
FILE *volume_snapshot(VolumeDisk *disk, const char *member);

/* directory_index.c: directory listings cached on a read stream. */
int volume_entries(VolumeDisk *disk, VolumeNode node, VolumeEntryCallback callback, void *context);
int volume_open_reader(TbReadStream *stream, VolumeDisk *disk);
void directory_index_stats(TbReadStream *stream, uint64_t *builds, uint64_t *hits);

/* Built-in volume operations on image paths. */
int volume_disk_info(const char *path, TbImageInfo *info);
char **volume_disk_names(const char *path, size_t *count);
FILE *volume_disk_fopen(const char *path);
#endif
