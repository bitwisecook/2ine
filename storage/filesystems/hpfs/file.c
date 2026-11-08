/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS file data: fnode and anode extent trees, reads, tree rebuild and resize. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void hpfs_file_free(HpfsFile *file)
{
    free(file->extents);
    free(file->nodes);
    memset(file, 0, sizeof *file);
}

static int extent_add(HpfsFile *file, uint32_t logical, uint32_t count, uint32_t sector)
{
    if (file->count >= HPFS_NODE_LIMIT)
        return hpfs_fail(EFBIG);
    if (file->count)
    {
        HpfsExtent *last = file->extents + file->count - 1;
        if ((uint64_t)last->logical + last->count != logical)
            return hpfs_fail(EIO);
        if ((uint64_t)last->sector + last->count == sector)
        {
            last->count += count;
            return 1;
        }
    }
    else if (logical)
        return hpfs_fail(EIO);
    HpfsExtent *next = realloc(file->extents, (file->count + 1) * sizeof *next);
    if (!next)
        return 0;
    file->extents = next;
    next[file->count++] = (HpfsExtent){logical, count, sector};
    return 1;
}

int hpfs_extent_tree(HpfsVolume *d, HpfsFile *file, const unsigned char *header, unsigned capacity,
                     unsigned depth)
{
    if (depth > 32)
        return hpfs_fail(EIO);
    unsigned used = header[5], internal = header[0] & 128, width = internal ? 8 : 12;
    if (used > capacity || tb_le16(header + 6) < 8 + used * width)
        return hpfs_fail(EIO);
    for (unsigned i = 0; i < used; i++)
    {
        const unsigned char *entry = header + 8 + i * width;
        if (internal)
        {
            uint32_t sector = tb_le32(entry + 4);
            unsigned char node[512];
            if (!hpfs_add_unique_node(&file->nodes, &file->node_count, sector) ||
                !hpfs_read_sectors(d, sector, node, 1) || tb_le32(node) != HPFS_ANODE_SIGNATURE ||
                tb_le32(node + 4) != sector)
                return hpfs_fail(EIO);
            if (!hpfs_extent_tree(d, file, node + 12, node[12] & 128 ? 60 : 40, depth + 1))
                return 0;
        }
        else
        {
            uint32_t logical = tb_le32(entry), count = tb_le32(entry + 4),
                     sector = tb_le32(entry + 8);
            if (!count || sector < 18 || sector >= d->sectors || count > d->sectors - sector ||
                (uint64_t)logical + count > UINT32_MAX)
                return hpfs_fail(EIO);
            if (!extent_add(file, logical, count, sector))
                return 0;
        }
    }
    return 1;
}

int hpfs_file_load(HpfsVolume *d, uint32_t sector, HpfsFile *file)
{
    memset(file, 0, sizeof *file);
    file->sector = sector;
    if (!hpfs_read_sectors(d, sector, file->data, 1) ||
        tb_le32(file->data) != HPFS_FNODE_SIGNATURE || (file->data[55] & 1))
        return hpfs_fail(EISDIR);
    if (!hpfs_extent_tree(d, file, file->data + 56, file->data[56] & 128 ? 12 : 8, 0))
    {
        hpfs_file_free(file);
        return 0;
    }
    uint64_t sectors = file->count ? (uint64_t)file->extents[file->count - 1].logical +
                                         file->extents[file->count - 1].count
                                   : 0;
    if (sectors * 512 < tb_le32(file->data + 160))
    {
        hpfs_file_free(file);
        return hpfs_fail(EIO);
    }
    return 1;
}

uint32_t hpfs_file_sector(HpfsFile *file, uint32_t logical)
{
    for (size_t i = 0; i < file->count; i++)
    {
        HpfsExtent *e = file->extents + i;
        if (logical >= e->logical && logical - e->logical < e->count)
            return e->sector + logical - e->logical;
    }
    errno = EIO;
    return 0;
}

int hpfs_file_copy(HpfsVolume *d, HpfsFile *file, FILE *output)
{
    uint32_t size = tb_le32(file->data + 160);
    unsigned char data[512];
    for (uint32_t at = 0; at < size;)
    {
        uint32_t sector = hpfs_file_sector(file, at / 512);
        unsigned n = size - at < 512 ? size - at : 512;
        if (!sector || !hpfs_read_sectors(d, sector, data, 1) || fwrite(data, 1, n, output) != n)
            return 0;
        at += n;
    }
    return 1;
}

static int node_parent(HpfsVolume *d, uint32_t node, uint32_t parent, int fnode)
{
    unsigned char bytes[512];
    if (!hpfs_read_sectors(d, node, bytes, 1))
        return 0;
    tb_put_le32(bytes + 8, parent);
    bytes[12] = fnode ? bytes[12] | 32 : bytes[12] & ~32u;
    return hpfs_stage_sectors(d, node, bytes, 1);
}

typedef struct HpfsTreeRef
{
    uint32_t sector, end;
} HpfsTreeRef;

static int file_tree_build(HpfsVolume *d, HpfsFile *file)
{
    for (size_t i = 0; i < file->node_count; i++)
        if (!hpfs_mark_sectors(d, file->nodes[i], 1, 1))
            return 0;
    free(file->nodes);
    file->nodes = NULL;
    file->node_count = 0;
    memset(file->data + 56, 0, 104);
    if (file->count <= 8)
    {
        file->data[60] = 8 - file->count;
        file->data[61] = file->count;
        tb_put_le16(file->data + 62, (unsigned)(8 + file->count * 12));
        for (size_t i = 0; i < file->count; i++)
        {
            unsigned char *p = file->data + 64 + i * 12;
            tb_put_le32(p, file->extents[i].logical);
            tb_put_le32(p + 4, file->extents[i].count);
            tb_put_le32(p + 8, file->extents[i].sector);
        }
    }
    else
    {
        size_t count = (file->count + 39) / 40;
        HpfsTreeRef *refs = calloc(count, sizeof *refs);
        if (!refs)
            return 0;
        for (size_t group = 0; group < count; group++)
        {
            unsigned char bytes[512] = {0};
            uint32_t sector = hpfs_allocate(d, 1, 1);
            if (!sector)
            {
                free(refs);
                return 0;
            }
            size_t start = group * 40, used = file->count - start;
            if (used > 40)
                used = 40;
            tb_put_le32(bytes, HPFS_ANODE_SIGNATURE);
            tb_put_le32(bytes + 4, sector);
            bytes[16] = 40 - used;
            bytes[17] = used;
            tb_put_le16(bytes + 18, (unsigned)(8 + used * 12));
            for (size_t i = 0; i < used; i++)
            {
                unsigned char *p = bytes + 20 + i * 12;
                HpfsExtent *e = file->extents + start + i;
                tb_put_le32(p, e->logical);
                tb_put_le32(p + 4, e->count);
                tb_put_le32(p + 8, e->sector);
            }
            HpfsExtent *last = file->extents + start + used - 1;
            refs[group] = (HpfsTreeRef){sector, last->logical + last->count};
            if (!hpfs_stage_sectors(d, sector, bytes, 1))
            {
                free(refs);
                return 0;
            }
        }
        while (count > 12)
        {
            size_t groups = (count + 59) / 60;
            HpfsTreeRef *next = calloc(groups, sizeof *next);
            if (!next)
            {
                free(refs);
                return 0;
            }
            int ok = 1;
            for (size_t group = 0; ok && group < groups; group++)
            {
                unsigned char bytes[512] = {0};
                uint32_t sector = hpfs_allocate(d, 1, 1);
                if (!sector)
                {
                    ok = 0;
                    break;
                }
                size_t start = group * 60, used = count - start;
                if (used > 60)
                    used = 60;
                tb_put_le32(bytes, HPFS_ANODE_SIGNATURE);
                tb_put_le32(bytes + 4, sector);
                bytes[12] = 128;
                bytes[16] = 60 - used;
                bytes[17] = used;
                tb_put_le16(bytes + 18, (unsigned)(8 + used * 8));
                for (size_t i = 0; ok && i < used; i++)
                {
                    tb_put_le32(bytes + 20 + i * 8,
                                i + 1 == used ? UINT32_MAX : refs[start + i].end);
                    tb_put_le32(bytes + 24 + i * 8, refs[start + i].sector);
                    ok = node_parent(d, refs[start + i].sector, sector, 0);
                }
                next[group] = (HpfsTreeRef){sector, refs[start + used - 1].end};
                if (ok)
                    ok = hpfs_stage_sectors(d, sector, bytes, 1);
            }
            free(refs);
            refs = next;
            count = groups;
            if (!ok)
            {
                free(refs);
                return 0;
            }
        }
        file->data[56] = 128;
        file->data[60] = 12 - count;
        file->data[61] = count;
        tb_put_le16(file->data + 62, (unsigned)(8 + count * 8));
        for (size_t i = 0; i < count; i++)
        {
            tb_put_le32(file->data + 64 + i * 8, i + 1 == count ? UINT32_MAX : refs[i].end);
            tb_put_le32(file->data + 68 + i * 8, refs[i].sector);
            if (!node_parent(d, refs[i].sector, file->sector, 1))
            {
                free(refs);
                return 0;
            }
        }
        free(refs);
    }
    return hpfs_stage_sectors(d, file->sector, file->data, 1);
}

int hpfs_file_resize(HpfsVolume *d, HpfsFile *file, uint64_t bytes)
{
    if (bytes > 0x7fffffffu)
        return hpfs_fail(EFBIG);
    uint32_t needed = (uint32_t)((bytes + 511) / 512);
    uint32_t allocated =
        file->count ? file->extents[file->count - 1].logical + file->extents[file->count - 1].count
                    : 0;
    if (needed < allocated)
    {
        size_t used = 0;
        for (size_t i = 0; i < file->count; i++)
        {
            HpfsExtent extent = file->extents[i];
            if (extent.logical >= needed)
            {
                if (!hpfs_mark_sectors(d, extent.sector, extent.count, 1))
                    return 0;
                continue;
            }
            if (extent.count > needed - extent.logical)
            {
                uint32_t keep = needed - extent.logical;
                if (!hpfs_mark_sectors(d, extent.sector + keep, extent.count - keep, 1))
                    return 0;
                extent.count = keep;
            }
            file->extents[used++] = extent;
        }
        file->count = used;
    }
    else if (needed > allocated)
    {
        uint32_t free = 0;
        for (uint32_t i = 18; i < d->sectors; i++)
            free += hpfs_sector_is_free(d, i);
        if (needed - allocated > free)
            return hpfs_fail(ENOSPC);
        unsigned char zero[512] = {0};
        for (uint32_t at = 18; allocated < needed && at < d->sectors;)
        {
            if (!hpfs_sector_is_free(d, at))
            {
                at++;
                continue;
            }
            uint32_t count = 0;
            while (count < needed - allocated && at + count < d->sectors &&
                   hpfs_sector_is_free(d, at + count))
                count++;
            if (!hpfs_mark_sectors(d, at, count, 0) || !extent_add(file, allocated, count, at))
                return 0;
            for (uint32_t i = 0; i < count; i++)
                if (!hpfs_stage_sectors(d, at + i, zero, 1))
                    return 0;
            allocated += count;
            at += count;
        }
        if (allocated < needed)
            return hpfs_fail(ENOSPC);
    }
    uint32_t previous = tb_le32(file->data + 160);
    if (bytes > previous && previous % 512)
    {
        unsigned char data[512];
        uint32_t sector = hpfs_file_sector(file, previous / 512);
        if (!sector || !hpfs_read_sectors(d, sector, data, 1))
            return 0;
        memset(data + previous % 512, 0, 512 - previous % 512);
        if (!hpfs_stage_sectors(d, sector, data, 1))
            return 0;
    }
    tb_put_le32(file->data + 160, (uint32_t)bytes);
    return file_tree_build(d, file);
}
