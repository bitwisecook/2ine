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
    if (bytes < 8 * 1024 * 1024 || bytes > UINT64_C(65536) * 1024 * 1024 ||
        bytes % (8 * 1024 * 1024))
        return hpfs_fail(EINVAL);
    HpfsVolume disk = {0};
    if (!format_file(path, bytes, &disk.file))
        return 0;
    disk.bytes = bytes;
    disk.sectors = (uint32_t)(bytes / 512);
    disk.bands = (disk.sectors + 16383) / 16384;
    disk.writable = 1;
    disk.codepage = 437;
    int ok = tb_image_lock_source(disk.file, 1);
    disk.bitmaps = malloc((size_t)disk.bands * 2048);
    disk.bitmap_locations = calloc(disk.bands, sizeof *disk.bitmap_locations);
    if (!disk.bitmaps || !disk.bitmap_locations)
        ok = 0;
    if (ok)
    {
        memset(disk.bitmaps, 255, (size_t)disk.bands * 2048);
        for (uint64_t i = disk.sectors; i < (uint64_t)disk.bands * 16384; i++)
            disk.bitmaps[i / 8] &= ~(1u << (i % 8));
        for (unsigned i = 0; i < 18; i++)
            disk.bitmaps[i / 8] &= ~(1u << (i % 8));
        unsigned list_sectors = ((disk.bands * 4 + 511) / 512 + 3) & ~3u;
        disk.bitmap_list = hpfs_allocate(&disk, list_sectors, 4);
        for (uint32_t i = 0; ok && i < disk.bands; i++)
        {
            disk.bitmap_locations[i] = hpfs_allocate(&disk, 4, 4);
            if (!disk.bitmap_locations[i])
                ok = 0;
        }
        disk.dir_map = hpfs_allocate(&disk, 4, 4);
        uint32_t bad = hpfs_allocate(&disk, 4, 4), hotfix = hpfs_allocate(&disk, 4, 4),
                 uid = hpfs_allocate(&disk, 8, 4), cpinfo = hpfs_allocate(&disk, 1, 1),
                 cpdata = hpfs_allocate(&disk, 1, 1);
        uint32_t hotfix_spares[8];
        for (unsigned i = 0; i < 8; i++)
            hotfix_spares[i] = hpfs_allocate(&disk, 1, 1);
        disk.dir_count = (disk.sectors / 100 + 3) & ~3u;
        if (disk.dir_count > 65536)
            disk.dir_count = 65536;
        disk.dir_start = hpfs_allocate(&disk, disk.dir_count, 4);
        memset(disk.dir_bitmap, 255, sizeof disk.dir_bitmap);
        if (!disk.bitmap_list || !disk.dir_map || !bad || !hotfix || !uid || !cpinfo || !cpdata ||
            !disk.dir_start)
            ok = 0;
        unsigned char zeros[2048] = {0}, table[2048] = {0};
        for (unsigned i = 0; i < 8; i++)
            tb_put_le32(table + (8 + i) * 4, hotfix_spares[i]);
        if (ok)
            ok = hpfs_stage_sectors(&disk, bad, zeros, 4) &&
                 hpfs_stage_sectors(&disk, hotfix, table, 4);
        memcpy(disk.boot,
               "\xeb\x3c\x90"
               "2INEHPFS",
               11);
        tb_put_le16(disk.boot + 11, 512);
        disk.boot[13] = 1;
        disk.boot[21] = 0xf8;
        tb_put_le16(disk.boot + 24, 63);
        tb_put_le16(disk.boot + 26, 255);
        tb_put_le32(disk.boot + 32, disk.sectors);
        disk.boot[36] = 0x80;
        disk.boot[38] = 0x28;
        tb_put_le32(disk.boot + 39, (uint32_t)time(NULL));
        memcpy(disk.boot + 43, "NO NAME    HPFS    ", 19);
        memcpy(disk.boot + 62, "\xfa\xf4\xeb\xfd", 4);
        disk.boot[510] = 0x55;
        disk.boot[511] = 0xaa;
        tb_put_le32(disk.super, HPFS_SUPERBLOCK_SIGNATURE);
        tb_put_le32(disk.super + 4, 0xfa53e9c5u);
        disk.super[8] = 2;
        disk.super[9] = 2;
        tb_put_le32(disk.super + 16, disk.sectors);
        tb_put_le32(disk.super + 24, disk.bitmap_list);
        tb_put_le32(disk.super + 32, bad);
        tb_put_le32(disk.super + 48, disk.dir_count);
        tb_put_le32(disk.super + 52, disk.dir_start);
        tb_put_le32(disk.super + 56, disk.dir_start + disk.dir_count - 1);
        tb_put_le32(disk.super + 60, disk.dir_map);
        tb_put_le32(disk.super + 96, uid);
        tb_put_le32(disk.spare, HPFS_SPAREBLOCK_SIGNATURE);
        tb_put_le32(disk.spare + 4, 0xfa5229c5u);
        tb_put_le32(disk.spare + 12, hotfix);
        tb_put_le32(disk.spare + 20, 8);
        tb_put_le32(disk.spare + 24, 4);
        tb_put_le32(disk.spare + 28, 4);
        tb_put_le32(disk.spare + 32, cpinfo);
        tb_put_le32(disk.spare + 36, 1);
        for (unsigned i = 0; i < 4; i++)
        {
            uint32_t sector = hpfs_allocate(&disk, 4, 4);
            if (!sector)
                ok = 0;
            tb_put_le32(disk.spare + 108 + i * 4, sector);
        }
        unsigned char info[512] = {0}, data[512] = {0};
        tb_put_le32(info, 0x494521f7u);
        tb_put_le32(info + 4, 1);
        tb_put_le16(info + 16, 1);
        tb_put_le16(info + 18, 437);
        tb_put_le32(info + 24, cpdata);
        tb_put_le32(data, 0x894521f7u);
        tb_put_le16(data + 4, 1);
        tb_put_le16(data + 20, 26);
        tb_put_le16(data + 26, 1);
        tb_put_le16(data + 28, 437);
        for (unsigned i = 0; i < 256; i++)
            disk.case_map[i] = i < 128 ? toupper(i) : i;
        for (unsigned i = 0; i < 128; i++)
        {
            unsigned value = ff_uni2oem(ff_wtoupper(ff_oem2uni(i + 128, 437)), 437);
            data[32 + i] = value && value <= 255 ? value : i + 128;
            disk.case_map[i + 128] = data[32 + i];
        }
        uint32_t checksum = hpfs_checksum(data + 26, 136);
        tb_put_le32(data + 8, checksum);
        tb_put_le32(info + 20, checksum);
        if (ok)
            ok = hpfs_stage_sectors(&disk, cpinfo, info, 1) &&
                 hpfs_stage_sectors(&disk, cpdata, data, 1) &&
                 hpfs_stage_sectors(&disk, 0, disk.boot, 1);
        for (unsigned i = 0; ok && i < list_sectors; i++)
        {
            unsigned char block[512] = {0};
            for (unsigned j = 0; j < 128 && i * 128 + j < disk.bands; j++)
                tb_put_le32(block + j * 4, disk.bitmap_locations[i * 128 + j]);
            ok = hpfs_stage_sectors(&disk, disk.bitmap_list + i, block, 1);
        }
        HpfsEntry root = {0};
        unsigned char record[36] = {0};
        tb_put_le16(record, 36);
        record[3] = 16;
        record[30] = 1;
        record[31] = '\\';
        root.record = record;
        root.size = 36;
        if (ok)
            ok = hpfs_create_fnode(&disk, &root, 1);
        disk.root = tb_le32(root.record + 4);
        tb_put_le32(disk.super + 12, disk.root);
        if (ok)
        {
            unsigned char fnode[512];
            ok = hpfs_read_sectors(&disk, disk.root, fnode, 1);
            if (ok)
            {
                tb_put_le32(fnode + 28, disk.root);
                fnode[12] = 0;
                memset(fnode + 13, 0, 15);
                ok = hpfs_stage_sectors(&disk, disk.root, fnode, 1);
            }
        }
        if (ok)
            ok = hpfs_stage_sectors(&disk, 16, disk.super, 1) && hpfs_commit(&disk);
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
