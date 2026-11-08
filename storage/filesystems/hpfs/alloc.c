/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS sector and dnode allocation bitmaps, checksums and commit of staged sectors. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int hpfs_load_bitmaps(HpfsVolume *d)
{
    if (d->bitmaps)
        return 1;
    d->bitmaps = malloc((size_t)d->bands * HPFS_BITMAP_BYTES);
    d->bitmap_locations = calloc(d->bands, sizeof *d->bitmap_locations);
    if (!d->bitmaps || !d->bitmap_locations)
        return 0;
    unsigned char list[HPFS_SECTOR_BYTES];
    uint32_t current = UINT32_MAX;
    for (uint32_t i = 0; i < d->bands; i++)
    {
        uint32_t sector = d->bitmap_list + i / HPFS_BITMAP_LIST_ENTRIES;
        if (sector != current)
        {
            if (!hpfs_read_sectors(d, sector, list, 1))
                return 0;
            current = sector;
        }
        d->bitmap_locations[i] = tb_le32(list + (i % HPFS_BITMAP_LIST_ENTRIES) * 4);
        if (d->bitmap_locations[i] < HPFS_FIRST_DATA_SECTOR ||
            d->bitmap_locations[i] % HPFS_BITMAP_SECTORS ||
            !hpfs_read_sectors(d, d->bitmap_locations[i],
                               d->bitmaps + (size_t)i * HPFS_BITMAP_BYTES, HPFS_BITMAP_SECTORS))
            return hpfs_fail(EIO);
    }
    return hpfs_read_sectors(d, d->dir_map, d->dir_bitmap, HPFS_BITMAP_SECTORS);
}

int hpfs_sector_is_free(HpfsVolume *d, uint32_t sector)
{
    return sector < d->sectors && !!(d->bitmaps[sector / 8] & (1u << (sector % 8)));
}

int hpfs_mark_sectors(HpfsVolume *d, uint32_t sector, uint32_t count, int free)
{
    if (!sector || sector >= d->sectors || count > d->sectors - sector)
        return hpfs_fail(EIO);
    for (uint32_t i = sector; i < sector + count; i++)
    {
        if (free)
            d->bitmaps[i / 8] |= 1u << (i % 8);
        else
            d->bitmaps[i / 8] &= ~(1u << (i % 8));
    }
    return 1;
}

uint32_t hpfs_allocate(HpfsVolume *d, unsigned count, unsigned alignment)
{
    for (uint32_t i = HPFS_FIRST_DATA_SECTOR; i < d->sectors && count <= d->sectors - i;)
    {
        if (i % alignment)
        {
            i += alignment - i % alignment;
            continue;
        }
        unsigned n = 0;
        while (n < count && hpfs_sector_is_free(d, i + n))
            n++;
        if (n == count)
        {
            hpfs_mark_sectors(d, i, count, 0);
            return i;
        }
        i += n + 1;
    }
    errno = ENOSPC;
    return 0;
}

uint32_t hpfs_allocate_dnode(HpfsVolume *d)
{
    for (uint32_t i = 0; i < d->dir_count / HPFS_DNODE_SECTORS; i++)
        if (d->dir_bitmap[i / 8] & (1u << (i % 8)))
        {
            d->dir_bitmap[i / 8] &= ~(1u << (i % 8));
            return d->dir_start + i * HPFS_DNODE_SECTORS;
        }
    return hpfs_allocate(d, HPFS_DNODE_SECTORS, HPFS_DNODE_SECTORS);
}

int hpfs_free_dnode(HpfsVolume *d, uint32_t sector)
{
    if (sector >= d->dir_start && sector < d->dir_start + d->dir_count)
    {
        uint32_t bit = (sector - d->dir_start) / HPFS_DNODE_SECTORS;
        d->dir_bitmap[bit / 8] |= 1u << (bit % 8);
        return 1;
    }
    return hpfs_mark_sectors(d, sector, HPFS_DNODE_SECTORS, 1);
}

uint32_t hpfs_checksum(const unsigned char *bytes, size_t n)
{
    uint32_t value = 0;
    while (n--)
    {
        value += *bytes++;
        value = value << 7 | value >> 25;
    }
    return value;
}

static int sync_source(FILE *file)
{
    if (fflush(file))
        return 0;
    return tb_host_sync(file);
}

int hpfs_commit(HpfsVolume *d)
{
    if (!hpfs_load_bitmaps(d))
        return 0;
    for (uint32_t i = 0; i < d->bands; i++)
        if (!hpfs_stage_sectors(d, d->bitmap_locations[i],
                                d->bitmaps + (size_t)i * HPFS_BITMAP_BYTES, HPFS_BITMAP_SECTORS))
            return 0;
    if (!hpfs_stage_sectors(d, d->dir_map, d->dir_bitmap, HPFS_BITMAP_SECTORS))
        return 0;
    tb_put_le32(d->spare + HPFS_SPARE_SUPER_CHECKSUM, hpfs_checksum(d->super, HPFS_SECTOR_BYTES));
    tb_put_le32(d->spare + HPFS_SPARE_CHECKSUM, 0);
    tb_put_le32(d->spare + HPFS_SPARE_CHECKSUM, hpfs_checksum(d->spare, HPFS_SECTOR_BYTES));
    if (!hpfs_stage_sectors(d, HPFS_SPAREBLOCK_SECTOR, d->spare, 1))
        return 0;
    /* A failed write leaves the HPFS dirty bit set for the original checker. */
    unsigned char dirty[HPFS_SECTOR_BYTES];
    memcpy(dirty, d->spare, HPFS_SECTOR_BYTES);
    dirty[HPFS_SPARE_FLAGS] |= HPFS_SPARE_DIRTY;

    if (fseeko(d->file, ((uint64_t)d->base + HPFS_SPAREBLOCK_SECTOR) * HPFS_SECTOR_BYTES,
               SEEK_SET) ||
        fwrite(dirty, 1, HPFS_SECTOR_BYTES, d->file) != HPFS_SECTOR_BYTES || !sync_source(d->file))
        return hpfs_fail(EIO);
    for (unsigned i = 0; i < HPFS_STAGED_BUCKETS; i++)
        for (HpfsPatch *p = d->changes[i]; p; p = p->next)
        {
            if (p->sector == HPFS_SPAREBLOCK_SECTOR)
                continue;
            if (fseeko(d->file, ((uint64_t)d->base + p->sector) * HPFS_SECTOR_BYTES, SEEK_SET) ||
                fwrite(p->data, 1, HPFS_SECTOR_BYTES, d->file) != HPFS_SECTOR_BYTES)
                return hpfs_fail(EIO);
        }
    if (!sync_source(d->file))
        return hpfs_fail(EIO);
    if (fseeko(d->file, ((uint64_t)d->base + HPFS_SPAREBLOCK_SECTOR) * HPFS_SECTOR_BYTES,
               SEEK_SET) ||
        fwrite(d->spare, 1, HPFS_SECTOR_BYTES, d->file) != HPFS_SECTOR_BYTES || fflush(d->file))
        return hpfs_fail(EIO);
    return tb_host_sync(d->file);
}
