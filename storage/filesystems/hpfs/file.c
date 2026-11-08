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
    if (depth > HPFS_MAX_TREE_DEPTH)
        return hpfs_fail(EIO);
    unsigned used = header[HPFS_TREE_USED], internal = header[HPFS_TREE_FLAGS] & HPFS_TREE_INTERNAL,
             width = internal ? HPFS_BRANCH_BYTES : HPFS_LEAF_BYTES;
    if (used > capacity ||
        tb_le16(header + HPFS_TREE_FIRST_FREE) < HPFS_TREE_ENTRIES + used * width)
        return hpfs_fail(EIO);
    for (unsigned i = 0; i < used; i++)
    {
        const unsigned char *entry = header + HPFS_TREE_ENTRIES + i * width;
        if (internal)
        {
            uint32_t sector = tb_le32(entry + HPFS_BRANCH_SECTOR);
            unsigned char node[HPFS_SECTOR_BYTES];
            if (!hpfs_add_unique_node(&file->nodes, &file->node_count, sector) ||
                !hpfs_read_sectors(d, sector, node, 1) || tb_le32(node) != HPFS_ANODE_SIGNATURE ||
                tb_le32(node + HPFS_ANODE_SELF) != sector)
                return hpfs_fail(EIO);
            if (!hpfs_extent_tree(d, file, node + HPFS_ANODE_TREE, hpfs_anode_capacity(node),
                                  depth + 1))
                return 0;
        }
        else
        {
            uint32_t logical = tb_le32(entry + HPFS_LEAF_LOGICAL),
                     count = tb_le32(entry + HPFS_LEAF_COUNT),
                     sector = tb_le32(entry + HPFS_LEAF_SECTOR);
            if (!count || sector < HPFS_FIRST_DATA_SECTOR || sector >= d->sectors ||
                count > d->sectors - sector || (uint64_t)logical + count > UINT32_MAX)
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
        tb_le32(file->data) != HPFS_FNODE_SIGNATURE ||
        (file->data[HPFS_FNODE_FLAGS] & HPFS_FNODE_DIRECTORY))
        return hpfs_fail(EISDIR);
    if (!hpfs_extent_tree(d, file, file->data + HPFS_FNODE_TREE,
                          file->data[HPFS_FNODE_TREE + HPFS_TREE_FLAGS] & HPFS_TREE_INTERNAL
                              ? HPFS_FNODE_BRANCHES
                              : HPFS_FNODE_LEAVES,
                          0))
    {
        hpfs_file_free(file);
        return 0;
    }
    uint64_t sectors = file->count ? (uint64_t)file->extents[file->count - 1].logical +
                                         file->extents[file->count - 1].count
                                   : 0;
    if (sectors * HPFS_SECTOR_BYTES < tb_le32(file->data + HPFS_FNODE_FILE_SIZE))
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
    uint32_t size = tb_le32(file->data + HPFS_FNODE_FILE_SIZE);
    unsigned char data[HPFS_SECTOR_BYTES];
    for (uint32_t at = 0; at < size;)
    {
        uint32_t sector = hpfs_file_sector(file, at / HPFS_SECTOR_BYTES);
        unsigned n = size - at < HPFS_SECTOR_BYTES ? size - at : HPFS_SECTOR_BYTES;
        if (!sector || !hpfs_read_sectors(d, sector, data, 1) || fwrite(data, 1, n, output) != n)
            return 0;
        at += n;
    }
    return 1;
}

static int node_parent(HpfsVolume *d, uint32_t node, uint32_t parent, int fnode)
{
    unsigned char bytes[HPFS_SECTOR_BYTES];
    if (!hpfs_read_sectors(d, node, bytes, 1))
        return 0;
    tb_put_le32(bytes + HPFS_ANODE_PARENT, parent);
    unsigned char *flags = bytes + HPFS_ANODE_TREE + HPFS_TREE_FLAGS;
    *flags = fnode ? *flags | HPFS_TREE_PARENT_FNODE : *flags & ~HPFS_TREE_PARENT_FNODE;

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
    memset(file->data + HPFS_FNODE_TREE, 0, HPFS_FNODE_TREE_BYTES);
    if (file->count <= HPFS_FNODE_LEAVES)
    {
        file->data[HPFS_FNODE_TREE + HPFS_TREE_FREE] = HPFS_FNODE_LEAVES - file->count;
        file->data[HPFS_FNODE_TREE + HPFS_TREE_USED] = file->count;
        tb_put_le16(file->data + HPFS_FNODE_TREE + HPFS_TREE_FIRST_FREE,
                    (unsigned)(HPFS_TREE_ENTRIES + file->count * HPFS_LEAF_BYTES));
        for (size_t i = 0; i < file->count; i++)
        {
            unsigned char *p =
                file->data + HPFS_FNODE_TREE + HPFS_TREE_ENTRIES + i * HPFS_LEAF_BYTES;
            tb_put_le32(p + HPFS_LEAF_LOGICAL, file->extents[i].logical);
            tb_put_le32(p + HPFS_LEAF_COUNT, file->extents[i].count);
            tb_put_le32(p + HPFS_LEAF_SECTOR, file->extents[i].sector);
        }
    }
    else
    {
        /* Leaf anodes hold the extents; branch anodes are added until the fnode can hold
           the top level. */
        size_t count = (file->count + HPFS_ANODE_LEAVES - 1) / HPFS_ANODE_LEAVES;
        HpfsTreeRef *refs = calloc(count, sizeof *refs);
        if (!refs)
            return 0;
        for (size_t group = 0; group < count; group++)
        {
            unsigned char bytes[HPFS_SECTOR_BYTES] = {0};
            uint32_t sector = hpfs_allocate(d, 1, 1);
            if (!sector)
            {
                free(refs);
                return 0;
            }
            size_t start = group * HPFS_ANODE_LEAVES, used = file->count - start;
            if (used > HPFS_ANODE_LEAVES)
                used = HPFS_ANODE_LEAVES;
            tb_put_le32(bytes, HPFS_ANODE_SIGNATURE);
            tb_put_le32(bytes + HPFS_ANODE_SELF, sector);
            bytes[HPFS_ANODE_TREE + HPFS_TREE_FREE] = HPFS_ANODE_LEAVES - used;
            bytes[HPFS_ANODE_TREE + HPFS_TREE_USED] = used;
            tb_put_le16(bytes + HPFS_ANODE_TREE + HPFS_TREE_FIRST_FREE,
                        (unsigned)(HPFS_TREE_ENTRIES + used * HPFS_LEAF_BYTES));
            for (size_t i = 0; i < used; i++)
            {
                unsigned char *p =
                    bytes + HPFS_ANODE_TREE + HPFS_TREE_ENTRIES + i * HPFS_LEAF_BYTES;
                HpfsExtent *e = file->extents + start + i;
                tb_put_le32(p + HPFS_LEAF_LOGICAL, e->logical);
                tb_put_le32(p + HPFS_LEAF_COUNT, e->count);
                tb_put_le32(p + HPFS_LEAF_SECTOR, e->sector);
            }
            HpfsExtent *last = file->extents + start + used - 1;
            refs[group] = (HpfsTreeRef){sector, last->logical + last->count};
            if (!hpfs_stage_sectors(d, sector, bytes, 1))
            {
                free(refs);
                return 0;
            }
        }
        while (count > HPFS_FNODE_BRANCHES)
        {
            size_t groups = (count + HPFS_ANODE_BRANCHES - 1) / HPFS_ANODE_BRANCHES;
            HpfsTreeRef *next = calloc(groups, sizeof *next);
            if (!next)
            {
                free(refs);
                return 0;
            }
            int ok = 1;
            for (size_t group = 0; ok && group < groups; group++)
            {
                unsigned char bytes[HPFS_SECTOR_BYTES] = {0};
                uint32_t sector = hpfs_allocate(d, 1, 1);
                if (!sector)
                {
                    ok = 0;
                    break;
                }
                size_t start = group * HPFS_ANODE_BRANCHES, used = count - start;
                if (used > HPFS_ANODE_BRANCHES)
                    used = HPFS_ANODE_BRANCHES;
                tb_put_le32(bytes, HPFS_ANODE_SIGNATURE);
                tb_put_le32(bytes + HPFS_ANODE_SELF, sector);
                bytes[HPFS_ANODE_TREE + HPFS_TREE_FLAGS] = HPFS_TREE_INTERNAL;
                bytes[HPFS_ANODE_TREE + HPFS_TREE_FREE] = HPFS_ANODE_BRANCHES - used;
                bytes[HPFS_ANODE_TREE + HPFS_TREE_USED] = used;
                tb_put_le16(bytes + HPFS_ANODE_TREE + HPFS_TREE_FIRST_FREE,
                            (unsigned)(HPFS_TREE_ENTRIES + used * HPFS_BRANCH_BYTES));
                for (size_t i = 0; ok && i < used; i++)
                {
                    /* The last branch covers the rest of the file. */
                    tb_put_le32(bytes + HPFS_ANODE_TREE + HPFS_TREE_ENTRIES + HPFS_BRANCH_END +
                                    i * HPFS_BRANCH_BYTES,
                                i + 1 == used ? UINT32_MAX : refs[start + i].end);
                    tb_put_le32(bytes + HPFS_ANODE_TREE + HPFS_TREE_ENTRIES + HPFS_BRANCH_SECTOR +
                                    i * HPFS_BRANCH_BYTES,
                                refs[start + i].sector);
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
        file->data[HPFS_FNODE_TREE + HPFS_TREE_FLAGS] = HPFS_TREE_INTERNAL;
        file->data[HPFS_FNODE_TREE + HPFS_TREE_FREE] = HPFS_FNODE_BRANCHES - count;
        file->data[HPFS_FNODE_TREE + HPFS_TREE_USED] = count;
        tb_put_le16(file->data + HPFS_FNODE_TREE + HPFS_TREE_FIRST_FREE,
                    (unsigned)(HPFS_TREE_ENTRIES + count * HPFS_BRANCH_BYTES));
        for (size_t i = 0; i < count; i++)
        {
            tb_put_le32(file->data + HPFS_FNODE_TREE + HPFS_TREE_ENTRIES + HPFS_BRANCH_END +
                            i * HPFS_BRANCH_BYTES,
                        i + 1 == count ? UINT32_MAX : refs[i].end);
            tb_put_le32(file->data + HPFS_FNODE_TREE + HPFS_TREE_ENTRIES + HPFS_BRANCH_SECTOR +
                            i * HPFS_BRANCH_BYTES,
                        refs[i].sector);

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
    if (bytes > HPFS_MAX_FILE_BYTES)
        return hpfs_fail(EFBIG);
    uint32_t needed = (uint32_t)((bytes + HPFS_SECTOR_BYTES - 1) / HPFS_SECTOR_BYTES);
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
        for (uint32_t i = HPFS_FIRST_DATA_SECTOR; i < d->sectors; i++)
            free += hpfs_sector_is_free(d, i);
        if (needed - allocated > free)
            return hpfs_fail(ENOSPC);
        unsigned char zero[HPFS_SECTOR_BYTES] = {0};
        for (uint32_t at = HPFS_FIRST_DATA_SECTOR; allocated < needed && at < d->sectors;)
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
    uint32_t previous = tb_le32(file->data + HPFS_FNODE_FILE_SIZE);
    if (bytes > previous && previous % HPFS_SECTOR_BYTES)
    {
        unsigned char data[HPFS_SECTOR_BYTES];
        uint32_t sector = hpfs_file_sector(file, previous / HPFS_SECTOR_BYTES);
        if (!sector || !hpfs_read_sectors(d, sector, data, 1))
            return 0;
        memset(data + previous % HPFS_SECTOR_BYTES, 0,
               HPFS_SECTOR_BYTES - previous % HPFS_SECTOR_BYTES);
        if (!hpfs_stage_sectors(d, sector, data, 1))
            return 0;
    }
    tb_put_le32(file->data + HPFS_FNODE_FILE_SIZE, (uint32_t)bytes);
    return file_tree_build(d, file);
}
