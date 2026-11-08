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

/* Applies one TB_FS_* change to a built-in volume, staging the whole volume in memory and
   rewriting it under an exclusive lock. ISO 9660 volumes fail with EROFS. */
int fat12_16_modify(const char *path, const char *expected, int action, uint64_t offset,
                    const void *data, size_t size, const char *target);
#endif
