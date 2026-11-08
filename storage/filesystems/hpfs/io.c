/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS sector I/O with staged writes, volume probe and open, and code-page tables. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/volume/volume_internal.h"
#include "storage/volume/probe.h"
#include "storage/common/host_stdio.h"
#include "storage/vendor/fatfs/ff.h"
#include "formats/endian.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int hpfs_fail(int error)
{
    errno = error;
    return 0;
}

static int read_bytes(FILE *file, uint64_t offset, void *bytes, size_t size)
{
    return !fseeko(file, (int64_t)offset, SEEK_SET) && fread(bytes, 1, size, file) == size
               ? 1
               : hpfs_fail(EIO);
}

int hpfs_read_sectors(HpfsVolume *d, uint32_t sector, void *bytes, unsigned count)
{
    if (sector >= d->sectors || count > d->sectors - sector)
        return hpfs_fail(EIO);
    for (unsigned i = 0; i < count; i++)
    {
        HpfsPatch *patch = d->changes[(sector + i) % HPFS_STAGED_BUCKETS];
        while (patch && patch->sector != sector + i)
            patch = patch->next;
        if (patch)
            memcpy((unsigned char *)bytes + i * HPFS_SECTOR_BYTES, patch->data, HPFS_SECTOR_BYTES);
        else if (!read_bytes(d->file, ((uint64_t)d->base + sector + i) * HPFS_SECTOR_BYTES,
                             (unsigned char *)bytes + i * HPFS_SECTOR_BYTES, HPFS_SECTOR_BYTES))
            return 0;
    }
    return 1;
}

int hpfs_stage_sectors(HpfsVolume *d, uint32_t sector, const void *bytes, unsigned count)
{
    if (sector >= d->sectors || count > d->sectors - sector)
        return hpfs_fail(EIO);
    for (unsigned i = 0; i < count; i++)
    {
        unsigned bucket = (sector + i) % HPFS_STAGED_BUCKETS;
        HpfsPatch *patch = d->changes[bucket];
        while (patch && patch->sector != sector + i)
            patch = patch->next;
        if (!patch)
        {
            patch = malloc(sizeof *patch);
            if (!patch)
                return 0;
            patch->sector = sector + i;
            patch->next = d->changes[bucket];
            d->changes[bucket] = patch;
        }
        memcpy(patch->data, (const unsigned char *)bytes + i * HPFS_SECTOR_BYTES,
               HPFS_SECTOR_BYTES);
    }
    return 1;
}

int hpfs_add_unique_node(uint32_t **nodes, size_t *count, uint32_t value)
{
    if (*count >= HPFS_NODE_LIMIT)
        return hpfs_fail(EFBIG);
    for (size_t i = 0; i < *count; i++)
        if ((*nodes)[i] == value)
            return hpfs_fail(EIO);
    uint32_t *next = realloc(*nodes, (*count + 1) * sizeof **nodes);
    if (!next)
        return 0;
    *nodes = next;
    next[(*count)++] = value;
    return 1;
}

void hpfs_close(HpfsVolume *d)
{
    int saved = errno;
    if (d->file)
        fclose(d->file);
    for (unsigned i = 0; i < HPFS_STAGED_BUCKETS; i++)
    {
        HpfsPatch *p = d->changes[i];
        while (p)
        {
            HpfsPatch *next = p->next;
            free(p);
            p = next;
        }
    }
    free(d->codepages);
    free(d->bitmaps);
    free(d->bitmap_locations);
    errno = saved;
}

/* Accepts an unpartitioned volume, or the first type 07 primary partition that holds one. */
static int find_superblock(FILE *file, const TbVolumeProbe *probe, uint32_t *base,
                           unsigned char *super)
{
    *base = 0;
    if (probe->hpfs)
    {
        memcpy(super, probe->hpfs_super, HPFS_SECTOR_BYTES);
        return 1;
    }
    if (!probe->mbr)
        return hpfs_fail(EINVAL);
    for (unsigned i = 0; i < MBR_PARTITIONS; i++)
    {
        const TbMbrPartition *p = probe->partitions + i;
        if (p->type != MBR_TYPE_IFS || p->sectors < HPFS_FIRST_DATA_SECTOR ||
            ((uint64_t)p->first_sector + p->sectors) * HPFS_SECTOR_BYTES > probe->bytes)
            continue;
        if (read_bytes(file,
                       ((uint64_t)p->first_sector + HPFS_SUPERBLOCK_SECTOR) * HPFS_SECTOR_BYTES,
                       super, HPFS_SECTOR_BYTES) &&
            tb_le32(super) == HPFS_SUPERBLOCK_SIGNATURE)
        {
            *base = p->first_sector;
            return 1;
        }
    }
    return hpfs_fail(EINVAL);
}

int tb_hpfs_volume(const TbVolumeProbe *probe, FILE *source)
{
    unsigned char super[HPFS_SECTOR_BYTES];
    uint32_t base;
    return find_superblock(source, probe, &base, super);
}

int hpfs_open(const char *path, int write, const char *expected, HpfsVolume *d)
{
    memset(d, 0, sizeof *d);
    int protected = tb_image_protected(path);
    if (protected < 0)
        return 0;
    if (write && protected)
        return hpfs_fail(EROFS);
    char *source = tb_image_source(path);
    if (!source)
        return 0;
    if (expected && strcmp(expected, source))
    {
        free(source);
        return hpfs_fail(ENXIO);
    }
    d->file = tb_image_open_source(source, write);
    free(source);
    if (!d->file)
        return 0;
    if (!tb_image_lock_source(d->file, write) || fseeko(d->file, 0, SEEK_END))
        goto fail;
    int64_t bytes = ftello(d->file);
    if (bytes < 0)
        goto fail;
    TbVolumeProbe probe;
    tb_volume_probe_file(d->file, (uint64_t)bytes, &probe);
    if (!find_superblock(d->file, &probe, &d->base, d->super))
        goto fail;
    d->bytes = bytes;
    d->sectors = tb_le32(d->super + HPFS_SUPER_SECTORS);
    d->root = tb_le32(d->super + HPFS_SUPER_ROOT);
    if (tb_le32(d->super + HPFS_SUPER_SIGNATURE2) != HPFS_SUPERBLOCK_SIGNATURE2 ||
        d->super[HPFS_SUPER_VERSION] != HPFS_VERSION || d->sectors < HPFS_MIN_SECTORS ||
        ((uint64_t)d->base + d->sectors) * HPFS_SECTOR_BYTES > d->bytes)
    {
        errno = EINVAL;
        goto fail;
    }
    if (!hpfs_read_sectors(d, 0, d->boot, 1) ||
        !hpfs_read_sectors(d, HPFS_SPAREBLOCK_SECTOR, d->spare, 1))
        goto fail;
    if (tb_le16(d->boot + HPFS_BOOT_SECTOR_BYTES) != HPFS_SECTOR_BYTES ||
        tb_le32(d->spare) != HPFS_SPAREBLOCK_SIGNATURE ||
        tb_le32(d->spare + HPFS_SPARE_SIGNATURE2) != HPFS_SPAREBLOCK_SIGNATURE2)
    {
        errno = EINVAL;
        goto fail;
    }
    /* Hotfix remapping and HPFS386 allocation/ACL variants require their own backend. */
    if (tb_le32(d->spare + HPFS_SPARE_HOTFIX_USED) ||
        (write && (d->super[HPFS_SUPER_FUNCTIONAL_VERSION] > HPFS_FUNCTIONAL_VERSION ||
                   d->spare[HPFS_SPARE_FLAGS] & HPFS_SPARE_NEEDS_CHECK)))
    {
        errno = ENOTSUP;
        goto fail;
    }
    unsigned char root_fnode[HPFS_SECTOR_BYTES];
    if (d->root < HPFS_FIRST_DATA_SECTOR || !hpfs_read_sectors(d, d->root, root_fnode, 1) ||
        tb_le32(root_fnode) != HPFS_FNODE_SIGNATURE ||
        !(root_fnode[HPFS_FNODE_FLAGS] & HPFS_FNODE_DIRECTORY))
    {
        errno = EINVAL;
        goto fail;
    }
    d->writable = write;
    d->bands = (d->sectors + HPFS_BAND_SECTORS - 1) / HPFS_BAND_SECTORS;
    d->bitmap_list = tb_le32(d->super + HPFS_SUPER_BITMAP_LIST);
    d->dir_start = tb_le32(d->super + HPFS_SUPER_DIR_BAND_START);
    d->dir_count = tb_le32(d->super + HPFS_SUPER_DIR_BAND_SECTORS);
    d->dir_map = tb_le32(d->super + HPFS_SUPER_DIR_BAND_BITMAP);
    if (d->dir_count > HPFS_DIR_BAND_MAX_SECTORS || d->dir_start % HPFS_DNODE_SECTORS ||
        d->dir_count % HPFS_DNODE_SECTORS || d->dir_start >= d->sectors ||
        d->dir_count > d->sectors - d->dir_start)
    {
        errno = EINVAL;
        goto fail;
    }
    d->codepage = HPFS_DEFAULT_CODEPAGE;
    for (unsigned i = 0; i < 256; i++)
        d->case_map[i] = i < 128 ? toupper(i) : i;
    uint32_t cpinfo = tb_le32(d->spare + HPFS_SPARE_CODEPAGE_INFO);
    if (cpinfo)
    {
        d->codepage_count = tb_le32(d->spare + HPFS_SPARE_CODEPAGES);
        if (!d->codepage_count || d->codepage_count > HPFS_MAX_CODEPAGES)
        {
            errno = EINVAL;
            goto fail;
        }
        d->codepages = calloc(d->codepage_count, sizeof *d->codepages);
        if (!d->codepages)
            goto fail;
        unsigned loaded = 0;
        for (unsigned page = 0; cpinfo; page++)
        {
            unsigned char info[HPFS_SECTOR_BYTES];
            if (page >= HPFS_MAX_CODEPAGES || !hpfs_read_sectors(d, cpinfo, info, 1) ||
                tb_le32(info) != HPFS_CODEPAGE_INFO_SIGNATURE ||
                tb_le32(info + HPFS_CPINFO_FIRST_INDEX) != loaded)
            {
                errno = EINVAL;
                goto fail;
            }
            unsigned count = tb_le32(info + HPFS_CPINFO_COUNT);
            if (!count || count > HPFS_CPINFO_MAX_ENTRIES || count > d->codepage_count - loaded)
            {
                errno = EINVAL;
                goto fail;
            }
            for (unsigned i = 0; i < count; i++)
            {
                const unsigned char *entry =
                    info + HPFS_CPINFO_ENTRIES + i * HPFS_CPINFO_ENTRY_BYTES;
                unsigned index = loaded + i, codepage = tb_le16(entry + HPFS_CPINFO_CODEPAGE);
                unsigned char data[HPFS_SECTOR_BYTES];
                if (tb_le16(entry + HPFS_CPINFO_INDEX) != index ||
                    !hpfs_read_sectors(d, tb_le32(entry + HPFS_CPINFO_DATA), data, 1) ||
                    tb_le32(data) != HPFS_CODEPAGE_DATA_SIGNATURE)
                {
                    errno = EINVAL;
                    goto fail;
                }
                unsigned first = tb_le16(data + HPFS_CPDATA_FIRST_INDEX),
                         entries = tb_le16(data + HPFS_CPDATA_COUNT);
                if (entries > HPFS_CPDATA_MAX_TABLES || index < first || index - first >= entries)
                {
                    errno = EINVAL;
                    goto fail;
                }
                unsigned offset = tb_le16(data + HPFS_CPDATA_OFFSETS + (index - first) * 2);
                if (offset > HPFS_SECTOR_BYTES - HPFS_CPTABLE_BYTES ||
                    tb_le16(data + offset + HPFS_CPTABLE_CODEPAGE) != codepage)
                {
                    errno = EINVAL;
                    goto fail;
                }
                /* Names are decoded only in code pages below 900 without DBCS ranges. */
                if (codepage >= 900 || tb_le16(data + offset + HPFS_CPTABLE_DBCS_RANGES) ||
                    !ff_oem2uni(128, codepage))
                {
                    errno = ENOTSUP;
                    goto fail;
                }
                HpfsCodepage *cp = d->codepages + index;
                cp->codepage = codepage;
                for (unsigned j = 0; j < 128; j++)
                    cp->map[j] = toupper(j);
                memcpy(cp->map + 128, data + offset + HPFS_CPTABLE_CASE_MAP, 128);
            }
            loaded += count;
            cpinfo = tb_le32(info + HPFS_CPINFO_NEXT);
        }
        if (loaded != d->codepage_count)
        {
            errno = EINVAL;
            goto fail;
        }
        d->codepage = d->codepages[0].codepage;
        memcpy(d->case_map, d->codepages[0].map, 256);
    }
    return 1;
fail:
    hpfs_close(d);
    d->file = NULL;
    return 0;
}
