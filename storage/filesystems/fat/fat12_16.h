/* Public domain. No rights reserved. Wholly AI-authored. */
/* Built-in FAT12/16 reader and writer for unpartitioned images. */
#ifndef TB_STORAGE_FAT12_16_H
#define TB_STORAGE_FAT12_16_H
#include "storage/volume/volume_disk.h"

/* Parses the boot sector BPB, loads the first FAT and reads the volume label. */
int fat_identify(VolumeDisk *d);
/* Returns 0 at the end of a chain and UINT32_MAX with errno for a broken chain. */
uint32_t fat_next_cluster(VolumeDisk *d, uint32_t cluster);
/* Image offset of a byte within a directory, or UINT64_MAX past the end of its chain. */
uint64_t fat_directory_offset(VolumeDisk *d, VolumeNode directory, size_t at);
int fat_read_entries(VolumeDisk *d, VolumeNode directory, const unsigned char *data, size_t size,
                     VolumeEntryCallback callback, void *context);
uint64_t fat_available_bytes(VolumeDisk *d);

/* Mutators on image paths; expected, when set, must match the current source. */
int fat12_16_write(const char *path, const char *expected, uint64_t offset, const void *bytes,
                   size_t size);
int fat12_16_resize(const char *path, const char *expected, uint64_t size);
int fat12_16_create_file(const char *path, int exclusive);
int fat12_16_remove(const char *path, int directory);
int fat12_16_mkdir(const char *path);
int fat12_16_readonly(const char *path, int readonly);
int fat12_16_stamps(const char *path, const char *expected, const TbImageStamps *stamps);
int fat12_16_attributes(const char *path, const char *expected, unsigned attributes);
int fat12_16_move(const char *source, const char *target);
#endif
