/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS volume creation. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/volume/volume_internal.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "storage/vendor/fatfs/ff.h"
#include "formats/endian.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Boot sector BIOS parameter block fields written by format. */
enum
{
    BOOT_SECTORS_PER_CLUSTER = 13,
    BOOT_MEDIA = 21,
    BOOT_SECTORS_PER_TRACK = 24,
    BOOT_HEADS = 26,
    BOOT_TOTAL_SECTORS = 32,
    BOOT_DRIVE = 36,
    BOOT_EXTENDED_SIGNATURE = 38,
    BOOT_CODE = 62,
    BOOT_SIGNATURE = 510
};

enum
{
    FORMAT_UNIT_BYTES = 8 * 1024 * 1024, /* sizes are whole multiples of 8 MB */
    /* The hotfix list holds bad-sector, replacement-sector and fnode arrays, each with
       HOTFIX_ENTRIES slots; format sets only the replacements. */
    HOTFIX_ENTRIES = 8,
    SPARE_DNODES = 4,
    USER_TABLE_SECTORS = 8,
    DEFAULT_CODEPAGE = 437,
    CODEPAGE_TABLE = 26, /* offset of table 0 in the code-page data sector */
    ROOT_ENTRY_BYTES = 36

};

static int format_file(const char *path, uint64_t bytes, FILE **file)
{
    *file = tb_host_fopen(path, "wbx");
    if (!*file)
        return 0;
    if (fseeko(*file, (int64_t)bytes - 1, SEEK_SET) || fputc(0, *file) != 0 || fflush(*file))
    {
        fclose(*file);
        *file = NULL;
        remove(path);
        return 0;
    }
    fclose(*file);
    *file = tb_image_open_source(path, 1);
    return *file != NULL;
}

int tb_hpfs_format(const char *path, uint64_t bytes)
{
    if (bytes < FORMAT_UNIT_BYTES || bytes > UINT64_C(65536) * 1024 * 1024 ||
        bytes % FORMAT_UNIT_BYTES)
        return hpfs_fail(EINVAL);
    HpfsVolume disk = {0};
    if (!format_file(path, bytes, &disk.file))
        return 0;
    disk.bytes = bytes;
    disk.sectors = (uint32_t)(bytes / HPFS_SECTOR_BYTES);
    disk.bands = (disk.sectors + HPFS_BAND_SECTORS - 1) / HPFS_BAND_SECTORS;
    disk.writable = 1;
    disk.codepage = DEFAULT_CODEPAGE;
    int ok = tb_image_lock_source(disk.file, 1);
    disk.bitmaps = malloc((size_t)disk.bands * HPFS_BITMAP_BYTES);
    disk.bitmap_locations = calloc(disk.bands, sizeof *disk.bitmap_locations);
    if (!disk.bitmaps || !disk.bitmap_locations)
        ok = 0;
    if (ok)
    {
        /* Every sector starts free except the reserved area and the tail of the last band. */
        memset(disk.bitmaps, 255, (size_t)disk.bands * HPFS_BITMAP_BYTES);
        for (uint64_t i = disk.sectors; i < (uint64_t)disk.bands * HPFS_BAND_SECTORS; i++)
            disk.bitmaps[i / 8] &= ~(1u << (i % 8));
        for (unsigned i = 0; i < HPFS_FIRST_DATA_SECTOR; i++)
            disk.bitmaps[i / 8] &= ~(1u << (i % 8));
        unsigned list_sectors =
            ((disk.bands * 4 + HPFS_SECTOR_BYTES - 1) / HPFS_SECTOR_BYTES + 3) & ~3u;
        disk.bitmap_list = hpfs_allocate(&disk, list_sectors, 4);
        for (uint32_t i = 0; ok && i < disk.bands; i++)
        {
            disk.bitmap_locations[i] =
                hpfs_allocate(&disk, HPFS_BITMAP_SECTORS, HPFS_BITMAP_SECTORS);
            if (!disk.bitmap_locations[i])
                ok = 0;
        }
        disk.dir_map = hpfs_allocate(&disk, HPFS_BITMAP_SECTORS, HPFS_BITMAP_SECTORS);
        uint32_t bad = hpfs_allocate(&disk, 4, 4), hotfix = hpfs_allocate(&disk, 4, 4),
                 uid = hpfs_allocate(&disk, USER_TABLE_SECTORS, 4),
                 cpinfo = hpfs_allocate(&disk, 1, 1), cpdata = hpfs_allocate(&disk, 1, 1);
        uint32_t hotfix_spares[HOTFIX_ENTRIES];
        for (unsigned i = 0; i < HOTFIX_ENTRIES; i++)
            hotfix_spares[i] = hpfs_allocate(&disk, 1, 1);
        /* The directory band takes about 1% of the volume. */
        disk.dir_count = (disk.sectors / 100 + 3) & ~3u;
        if (disk.dir_count > HPFS_DIR_BAND_MAX_SECTORS)
            disk.dir_count = HPFS_DIR_BAND_MAX_SECTORS;
        disk.dir_start = hpfs_allocate(&disk, disk.dir_count, HPFS_DNODE_SECTORS);
        memset(disk.dir_bitmap, 255, sizeof disk.dir_bitmap);
        if (!disk.bitmap_list || !disk.dir_map || !bad || !hotfix || !uid || !cpinfo || !cpdata ||
            !disk.dir_start)
            ok = 0;
        unsigned char zeros[4 * HPFS_SECTOR_BYTES] = {0}, table[4 * HPFS_SECTOR_BYTES] = {0};
        for (unsigned i = 0; i < HOTFIX_ENTRIES; i++)
            tb_put_le32(table + (HOTFIX_ENTRIES + i) * 4, hotfix_spares[i]);
        if (ok)
            ok = hpfs_stage_sectors(&disk, bad, zeros, 4) &&
                 hpfs_stage_sectors(&disk, hotfix, table, 4);
        memcpy(disk.boot,
               "\xeb\x3c\x90"
               "2INEHPFS",
               11);
        tb_put_le16(disk.boot + HPFS_BOOT_SECTOR_BYTES, HPFS_SECTOR_BYTES);
        disk.boot[BOOT_SECTORS_PER_CLUSTER] = 1;
        disk.boot[BOOT_MEDIA] = 0xf8; /* fixed disk */
        tb_put_le16(disk.boot + BOOT_SECTORS_PER_TRACK, 63);
        tb_put_le16(disk.boot + BOOT_HEADS, 255);
        tb_put_le32(disk.boot + BOOT_TOTAL_SECTORS, disk.sectors);
        disk.boot[BOOT_DRIVE] = 0x80;
        disk.boot[BOOT_EXTENDED_SIGNATURE] = 0x28;
        tb_put_le32(disk.boot + HPFS_BOOT_SERIAL, (uint32_t)time(NULL));
        memcpy(disk.boot + HPFS_BOOT_LABEL, "NO NAME    HPFS    ", 19);
        /* cli; hlt; jmp back to the hlt: the volume is not bootable. */
        memcpy(disk.boot + BOOT_CODE, "\xfa\xf4\xeb\xfd", 4);
        disk.boot[BOOT_SIGNATURE] = 0x55;
        disk.boot[BOOT_SIGNATURE + 1] = 0xaa;
        tb_put_le32(disk.super, HPFS_SUPERBLOCK_SIGNATURE);
        tb_put_le32(disk.super + HPFS_SUPER_SIGNATURE2, HPFS_SUPERBLOCK_SIGNATURE2);
        disk.super[HPFS_SUPER_VERSION] = 2;
        disk.super[HPFS_SUPER_FUNCTIONAL_VERSION] = 2;
        tb_put_le32(disk.super + HPFS_SUPER_SECTORS, disk.sectors);
        tb_put_le32(disk.super + HPFS_SUPER_BITMAP_LIST, disk.bitmap_list);
        tb_put_le32(disk.super + HPFS_SUPER_BAD_LIST, bad);
        tb_put_le32(disk.super + HPFS_SUPER_DIR_BAND_SECTORS, disk.dir_count);
        tb_put_le32(disk.super + HPFS_SUPER_DIR_BAND_START, disk.dir_start);
        tb_put_le32(disk.super + HPFS_SUPER_DIR_BAND_END, disk.dir_start + disk.dir_count - 1);
        tb_put_le32(disk.super + HPFS_SUPER_DIR_BAND_BITMAP, disk.dir_map);
        tb_put_le32(disk.super + HPFS_SUPER_USER_TABLE, uid);
        tb_put_le32(disk.spare, HPFS_SPAREBLOCK_SIGNATURE);
        tb_put_le32(disk.spare + HPFS_SPARE_SIGNATURE2, HPFS_SPAREBLOCK_SIGNATURE2);
        tb_put_le32(disk.spare + HPFS_SPARE_HOTFIX_LIST, hotfix);
        tb_put_le32(disk.spare + HPFS_SPARE_HOTFIX_TOTAL, HOTFIX_ENTRIES);
        tb_put_le32(disk.spare + HPFS_SPARE_DNODES, SPARE_DNODES);
        tb_put_le32(disk.spare + HPFS_SPARE_FREE_DNODES, SPARE_DNODES);
        tb_put_le32(disk.spare + HPFS_SPARE_CODEPAGE_INFO, cpinfo);
        tb_put_le32(disk.spare + HPFS_SPARE_CODEPAGES, 1);
        for (unsigned i = 0; i < SPARE_DNODES; i++)
        {
            uint32_t sector = hpfs_allocate(&disk, HPFS_DNODE_SECTORS, HPFS_DNODE_SECTORS);
            if (!sector)
                ok = 0;
            tb_put_le32(disk.spare + HPFS_SPARE_DNODE_LIST + i * 4, sector);
        }
        /* One code page, 437, in table 0 of one data sector. */
        unsigned char info[HPFS_SECTOR_BYTES] = {0}, data[HPFS_SECTOR_BYTES] = {0};
        unsigned char *entry = info + HPFS_CPINFO_ENTRIES, *table0 = data + CODEPAGE_TABLE;
        tb_put_le32(info, HPFS_CODEPAGE_INFO_SIGNATURE);
        tb_put_le32(info + HPFS_CPINFO_COUNT, 1);
        tb_put_le16(entry + HPFS_CPINFO_COUNTRY, 1);
        tb_put_le16(entry + HPFS_CPINFO_CODEPAGE, DEFAULT_CODEPAGE);
        tb_put_le32(entry + HPFS_CPINFO_DATA, cpdata);
        tb_put_le32(data, HPFS_CODEPAGE_DATA_SIGNATURE);
        tb_put_le16(data + HPFS_CPDATA_COUNT, 1);
        tb_put_le16(data + HPFS_CPDATA_OFFSETS, CODEPAGE_TABLE);
        tb_put_le16(table0 + HPFS_CPTABLE_COUNTRY, 1);
        tb_put_le16(table0 + HPFS_CPTABLE_CODEPAGE, DEFAULT_CODEPAGE);
        for (unsigned i = 0; i < 256; i++)
            disk.case_map[i] = i < 128 ? toupper(i) : i;
        unsigned char *case_map = table0 + HPFS_CPTABLE_CASE_MAP;
        for (unsigned i = 0; i < 128; i++)
        {
            unsigned value =
                ff_uni2oem(ff_wtoupper(ff_oem2uni(i + 128, DEFAULT_CODEPAGE)), DEFAULT_CODEPAGE);
            case_map[i] = value && value <= 255 ? value : i + 128;
            disk.case_map[i + 128] = case_map[i];
        }
        uint32_t checksum = hpfs_checksum(table0, HPFS_CPTABLE_BYTES);
        tb_put_le32(data + HPFS_CPDATA_CHECKSUMS, checksum);
        tb_put_le32(entry + HPFS_CPINFO_CHECKSUM, checksum);
        if (ok)
            ok = hpfs_stage_sectors(&disk, cpinfo, info, 1) &&
                 hpfs_stage_sectors(&disk, cpdata, data, 1) &&
                 hpfs_stage_sectors(&disk, 0, disk.boot, 1);
        for (unsigned i = 0; ok && i < list_sectors; i++)
        {
            unsigned char block[HPFS_SECTOR_BYTES] = {0};
            for (unsigned j = 0;
                 j < HPFS_BITMAP_LIST_ENTRIES && i * HPFS_BITMAP_LIST_ENTRIES + j < disk.bands; j++)
                tb_put_le32(block + j * 4, disk.bitmap_locations[i * HPFS_BITMAP_LIST_ENTRIES + j]);
            ok = hpfs_stage_sectors(&disk, disk.bitmap_list + i, block, 1);
        }
        HpfsEntry root = {0};
        /* The root directory is its own parent and has an empty name in its fnode. */
        unsigned char record[ROOT_ENTRY_BYTES] = {0};
        tb_put_le16(record + HPFS_DIRENT_SIZE, ROOT_ENTRY_BYTES);
        record[HPFS_DIRENT_ATTRIBUTES] = HPFS_ATTR_DIRECTORY;
        record[HPFS_DIRENT_NAME_LENGTH] = 1;
        record[HPFS_DIRENT_NAME] = '\\';
        root.record = record;
        root.size = ROOT_ENTRY_BYTES;
        if (ok)
            ok = hpfs_create_fnode(&disk, &root, 1);
        disk.root = tb_le32(root.record + HPFS_DIRENT_FNODE);
        tb_put_le32(disk.super + HPFS_SUPER_ROOT, disk.root);
        if (ok)
        {
            unsigned char fnode[HPFS_SECTOR_BYTES];
            ok = hpfs_read_sectors(&disk, disk.root, fnode, 1);
            if (ok)
            {
                tb_put_le32(fnode + HPFS_FNODE_PARENT, disk.root);
                fnode[HPFS_FNODE_NAME_LENGTH] = 0;
                memset(fnode + HPFS_FNODE_NAME, 0, HPFS_FNODE_NAME_BYTES);
                ok = hpfs_stage_sectors(&disk, disk.root, fnode, 1);
            }
        }
        if (ok)
            ok = hpfs_stage_sectors(&disk, HPFS_SUPERBLOCK_SECTOR, disk.super, 1) &&
                 hpfs_commit(&disk);
    }
    hpfs_close(&disk);
    if (!ok)
    {
        int saved = errno;
        remove(path);
        errno = saved;
    }
    return ok;
}
