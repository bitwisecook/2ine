/* Public domain. No rights reserved. Wholly AI-authored. */
/* Formats blank FAT12 floppy images in the standard PC geometries. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/volume.h"
#include "storage/filesystems/fat/fat12_16.h"
#include "storage/common/host.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Media descriptor bytes of the PC floppy formats. */
enum
{
    MEDIA_360K = 0xfd,
    MEDIA_720K_1200K = 0xf9,
    MEDIA_1440K_2880K = 0xf0
};

/* Every PC floppy format has 512-byte sectors, one reserved sector, two FATs and two heads. */
enum
{
    FLOPPY_SECTOR_BYTES = 512,
    FLOPPY_RESERVED_SECTORS = 1,
    FLOPPY_FAT_COUNT = 2,
    FLOPPY_HEADS = 2
};

typedef struct FloppyGeometry
{
    unsigned kib, sectors, sectors_per_cluster, fat_sectors, root_entries, sectors_per_track, media;
} FloppyGeometry;

static const FloppyGeometry geometries[] = {
    {360, 720, 2, 2, 112, 9, MEDIA_360K},           {720, 1440, 2, 3, 112, 9, MEDIA_720K_1200K},
    {1200, 2400, 1, 7, 224, 15, MEDIA_720K_1200K},  {1440, 2880, 1, 9, 224, 18, MEDIA_1440K_2880K},
    {2880, 5760, 2, 9, 240, 36, MEDIA_1440K_2880K},
};

int tb_image_create_floppy(const char *path, unsigned kib)
{
    const FloppyGeometry *g = NULL;
    for (size_t i = 0; i < sizeof geometries / sizeof geometries[0]; i++)
        if (geometries[i].kib == kib)
            g = &geometries[i];
    if (!g)
    {
        errno = EINVAL;
        return 0;
    }
    size_t size = (size_t)g->sectors * FLOPPY_SECTOR_BYTES;
    unsigned char *bytes = calloc(size, 1);
    if (!bytes)
        return 0;
    /* A short jump over the BPB to the boot code, then the OEM name. */
    memcpy(bytes,
           "\xeb\x3c\x90"
           "2INEFAT ",
           FAT_BPB_SECTOR_BYTES);
    tb_put_le16(bytes + FAT_BPB_SECTOR_BYTES, FLOPPY_SECTOR_BYTES);
    bytes[FAT_BPB_SECTORS_PER_CLUSTER] = g->sectors_per_cluster;
    tb_put_le16(bytes + FAT_BPB_RESERVED_SECTORS, FLOPPY_RESERVED_SECTORS);
    bytes[FAT_BPB_FAT_COUNT] = FLOPPY_FAT_COUNT;
    tb_put_le16(bytes + FAT_BPB_ROOT_ENTRIES, g->root_entries);
    tb_put_le16(bytes + FAT_BPB_TOTAL_SECTORS_16, g->sectors);
    bytes[FAT_BPB_MEDIA] = g->media;
    tb_put_le16(bytes + FAT_BPB_FAT_SECTORS, g->fat_sectors);
    tb_put_le16(bytes + FAT_BPB_SECTORS_PER_TRACK, g->sectors_per_track);
    tb_put_le16(bytes + FAT_BPB_HEADS, FLOPPY_HEADS);
    bytes[FAT_BPB_EXTENDED_SIGNATURE] = FAT_EXTENDED_BOOT_SIGNATURE;
    tb_put_le32(bytes + FAT_BPB_SERIAL, (uint32_t)time(NULL));
    /* Volume label and filesystem type, space padded. */
    memcpy(bytes + FAT_BPB_LABEL, "NO NAME    FAT12   ", 19);
    /* A formatted data disk; its boot sector stops instead of executing zeros. */
    memcpy(bytes + FAT_BOOT_CODE, "\xfa\xf4\xeb\xfd", 4);
    tb_put_le16(bytes + FAT_BOOT_SIGNATURE_OFFSET, FAT_BOOT_SIGNATURE);
    /* FAT entries 0 and 1 hold the media byte and an end-of-chain mark. */
    for (unsigned i = 0; i < FLOPPY_FAT_COUNT; i++)
    {
        size_t at = (FLOPPY_RESERVED_SECTORS + i * g->fat_sectors) * FLOPPY_SECTOR_BYTES;
        bytes[at] = g->media;
        bytes[at + 1] = bytes[at + 2] = 0xff;
    }
    FILE *file = tb_host_fopen(path, "wbx");
    int ok = file != NULL;
    if (file)
    {
        ok = fwrite(bytes, 1, size, file) == size;
        if (fclose(file))
            ok = 0;
        if (!ok)
            remove(path);
    }
    free(bytes);
    return ok;
}
