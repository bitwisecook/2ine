/* Public domain. No rights reserved. Wholly AI-authored. */
/* Built-in FAT12/16 reader and writer for unpartitioned images. */
#ifndef TB_STORAGE_FAT12_16_H
#define TB_STORAGE_FAT12_16_H
#include "storage/volume/volume_disk.h"

/* Boot sector: BIOS parameter block (BPB) and extended BPB field offsets. */
enum
{
    FAT_BOOT_SECTOR_BYTES = 512,
    FAT_BPB_SECTOR_BYTES = 11,
    FAT_BPB_SECTORS_PER_CLUSTER = 13,
    FAT_BPB_RESERVED_SECTORS = 14,
    FAT_BPB_FAT_COUNT = 16,
    FAT_BPB_ROOT_ENTRIES = 17,
    FAT_BPB_TOTAL_SECTORS_16 = 19,
    FAT_BPB_MEDIA = 21,
    FAT_BPB_FAT_SECTORS = 22,
    FAT_BPB_SECTORS_PER_TRACK = 24,
    FAT_BPB_HEADS = 26,
    FAT_BPB_HIDDEN_SECTORS = 28,
    FAT_BPB_TOTAL_SECTORS_32 = 32,
    FAT_BPB_EXTENDED_SIGNATURE = 38,
    FAT_BPB_SERIAL = 39,
    FAT_BPB_LABEL = 43,
    FAT_BPB_FILESYSTEM_TYPE = 54,
    FAT_BOOT_CODE = 62,
    FAT_BOOT_SIGNATURE_OFFSET = 510
};

/* Bytes 55 AA at the end of the boot sector. */
#define FAT_BOOT_SIGNATURE 0xaa55u
/* Marks an extended BPB, which carries the serial number and volume label. */
#define FAT_EXTENDED_BOOT_SIGNATURE 0x29

/* Directory entry layout. Long-name (LFN) slots reuse the 32 bytes with UTF-16 name units. */
enum
{
    FAT_ENTRY_BYTES = 32,
    FAT_NAME_BYTES = 11, /* 8.3 name, space padded, without the dot */
    FAT_ENTRY_ATTRIBUTES = 11,
    FAT_ENTRY_CREATED_TIME = 14,
    FAT_ENTRY_CREATED_DATE = 16,
    FAT_ENTRY_ACCESSED_DATE = 18,
    FAT_ENTRY_MODIFIED_TIME = 22,
    FAT_ENTRY_MODIFIED_DATE = 24,
    FAT_ENTRY_CLUSTER = 26,
    FAT_ENTRY_SIZE = 28,
    FAT_LFN_CHECKSUM = 13,
    FAT_LFN_CLUSTER = 26, /* always zero in a valid slot */
    FAT_LFN_UNITS = 13,   /* name units per slot */
    FAT_LFN_SLOTS = 20,   /* enough for FAT_LFN_NAME_UNITS plus the terminator */
    FAT_LFN_NAME_UNITS = 255,
    FAT_LFN_BUFFER_UNITS = FAT_LFN_SLOTS * FAT_LFN_UNITS
};

/* First byte of a directory entry: zero ends the directory, FAT_ENTRY_FREE marks deletion. */
#define FAT_ENTRY_FREE 0xe5
/* LFN slot ordinal byte: sequence number, with FAT_LFN_LAST on the slot stored first. */
#define FAT_LFN_ORDINAL 0x1f
#define FAT_LFN_LAST 0x40
/* Unused name units after the terminator. */
#define FAT_LFN_PADDING 0xffffu

/* DOS attribute bits, shared with HPFS and the OS/2 FILE_* attributes. */
enum
{
    FAT_ATTRIBUTE_READONLY = 0x01,
    FAT_ATTRIBUTE_HIDDEN = 0x02,
    FAT_ATTRIBUTE_SYSTEM = 0x04,
    FAT_ATTRIBUTE_VOLUME = 0x08,
    FAT_ATTRIBUTE_DIRECTORY = 0x10,
    FAT_ATTRIBUTE_ARCHIVE = 0x20,
    FAT_ATTRIBUTE_LONG_NAME = 0x0f,
    /* The bits tb_image_attributes changes and TbImageInfo reports. */
    FAT_ATTRIBUTES_SETTABLE = FAT_ATTRIBUTE_HIDDEN | FAT_ATTRIBUTE_SYSTEM | FAT_ATTRIBUTE_ARCHIVE
};

/* Clusters 0 and 1 hold the media byte and end-of-chain mark; data starts at cluster 2. */
#define FAT_FIRST_CLUSTER 2u
/* A FAT12 volume has fewer than 4085 clusters and a FAT16 volume fewer than 65525. */
#define FAT12_CLUSTER_LIMIT 4085u
#define FAT16_CLUSTER_LIMIT 65525u

/* FAT entries at or above this value are reserved or bad clusters rather than links. */
static inline unsigned fat_reserved_cluster(const VolumeDisk *d)
{
    return d->fat_bits == 12 ? 0xff0u : 0xfff0u;
}

/* Image offset of a data cluster. */
static inline uint64_t fat_cluster_offset(const VolumeDisk *d, uint32_t cluster)
{
    return d->data_offset + (uint64_t)(cluster - FAT_FIRST_CLUSTER) * d->cluster_bytes;
}

/* Reports whether a cluster number names a cluster in the data area. */
static inline int fat_data_cluster(const VolumeDisk *d, uint32_t cluster)
{
    return cluster >= FAT_FIRST_CLUSTER && cluster < d->clusters + FAT_FIRST_CLUSTER &&
           cluster < fat_reserved_cluster(d);
}

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
