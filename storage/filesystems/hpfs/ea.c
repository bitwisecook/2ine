/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS extended attributes: FEA list storage in the fnode or external sectors. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/volume/volume_internal.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* OS/2 family APIs use a length-prefixed FEA list. HPFS stores its records
   without that four-byte prefix, either in the FNODE or external sectors. */
enum
{
    FEA_LIST_HEADER_BYTES = 4, /* the list's 32-bit total length */
    /* Upper bound on an external EA run or value, so a corrupt length cannot exhaust memory. */
    AUXILIARY_MAX_BYTES = 1048576
};

static int auxiliary_read(HpfsVolume *d, uint32_t sector, uint32_t size, int tree,
                          unsigned char *output)
{
    if (!size)
        return 1;
    if (size > AUXILIARY_MAX_BYTES)
        return hpfs_fail(EFBIG);
    if (!tree)
    {
        unsigned char data[HPFS_SECTOR_BYTES];
        for (uint32_t at = 0; at < size;)
        {
            unsigned n = size - at < HPFS_SECTOR_BYTES ? size - at : HPFS_SECTOR_BYTES;
            if (!hpfs_read_sectors(d, sector + at / HPFS_SECTOR_BYTES, data, 1))
                return 0;
            memcpy(output + at, data, n);
            at += n;
        }
        return 1;
    }
    HpfsFile file = {0};
    unsigned char node[HPFS_SECTOR_BYTES];
    int ok = hpfs_read_sectors(d, sector, node, 1) && tb_le32(node) == HPFS_ANODE_SIGNATURE &&
             hpfs_add_unique_node(&file.nodes, &file.node_count, sector) &&
             hpfs_extent_tree(d, &file, node + HPFS_ANODE_TREE, hpfs_anode_capacity(node), 0);
    for (uint32_t at = 0; ok && at < size;)
    {
        unsigned char data[HPFS_SECTOR_BYTES];
        uint32_t physical = hpfs_file_sector(&file, at / HPFS_SECTOR_BYTES);
        unsigned n = size - at < HPFS_SECTOR_BYTES ? size - at : HPFS_SECTOR_BYTES;
        ok = physical && hpfs_read_sectors(d, physical, data, 1);
        if (ok)
            memcpy(output + at, data, n);
        at += n;
    }
    hpfs_file_free(&file);
    return ok;
}

static unsigned char *ea_raw(HpfsVolume *d, const unsigned char *fnode, size_t *size)
{
    uint32_t external = tb_le32(fnode + HPFS_FNODE_EAS);
    unsigned internal = tb_le16(fnode + HPFS_FNODE_EA_INTERNAL_SIZE),
             base = tb_le16(fnode + HPFS_FNODE_INTERNAL_OFFSET),
             acl = tb_le16(fnode + HPFS_FNODE_ACL_INTERNAL_SIZE);
    if ((external && internal) || external > AUXILIARY_MAX_BYTES || base > HPFS_SECTOR_BYTES ||
        acl > HPFS_SECTOR_BYTES - base || internal > HPFS_SECTOR_BYTES - base - acl)
    {
        errno = EIO;
        return NULL;
    }
    *size = external ? external : internal;
    unsigned char *data = malloc(*size ? *size : 1);
    if (!data)
        return NULL;
    if (external && !auxiliary_read(d, tb_le32(fnode + HPFS_FNODE_EA_SECTOR), external,
                                    fnode[HPFS_FNODE_EA_IN_ANODE], data))
    {
        free(data);
        return NULL;
    }
    if (internal)
        memcpy(data, fnode + base + acl, internal);
    return data;
}

static int auxiliary_free(HpfsVolume *d, uint32_t sector, uint32_t size, int tree)
{
    if (!size)
        return 1;
    if (!tree)
        return hpfs_mark_sectors(
            d, sector, (uint32_t)(((uint64_t)size + HPFS_SECTOR_BYTES - 1) / HPFS_SECTOR_BYTES), 1);
    HpfsFile file = {0};
    unsigned char node[HPFS_SECTOR_BYTES];
    int ok = hpfs_read_sectors(d, sector, node, 1) && tb_le32(node) == HPFS_ANODE_SIGNATURE &&
             hpfs_add_unique_node(&file.nodes, &file.node_count, sector) &&
             hpfs_extent_tree(d, &file, node + HPFS_ANODE_TREE, hpfs_anode_capacity(node), 0);
    for (size_t i = 0; ok && i < file.count; i++)
        ok = hpfs_mark_sectors(d, file.extents[i].sector, file.extents[i].count, 1);
    for (size_t i = 0; ok && i < file.node_count; i++)
        ok = hpfs_mark_sectors(d, file.nodes[i], 1, 1);
    hpfs_file_free(&file);
    return ok;
}

int hpfs_ea_values_free(HpfsVolume *d, const unsigned char *fnode)
{
    size_t size;
    unsigned char *data = ea_raw(d, fnode, &size);
    if (!data)
        return 0;
    int ok = 1;
    for (size_t at = 0; ok && at < size;)
    {
        if (size - at < HPFS_EA_FIXED_BYTES)
        {
            ok = hpfs_fail(EIO);
            break;
        }
        const unsigned char *entry = data + at;
        size_t count = HPFS_EA_FIXED_BYTES + entry[HPFS_EA_NAME_LENGTH] +
                       tb_le16(entry + HPFS_EA_VALUE_LENGTH);
        unsigned method = entry[HPFS_EA_FLAGS] & HPFS_EA_METHOD;
        if (count > size - at || entry[HPFS_EA_NAME + entry[HPFS_EA_NAME_LENGTH]] ||
            (method != HPFS_EA_INLINE && method != HPFS_EA_EXTERNAL && method != HPFS_EA_TREE))
        {
            ok = hpfs_fail(EIO);
            break;
        }
        if (method)
        {
            const unsigned char *pointer = entry + HPFS_EA_FIXED_BYTES + entry[HPFS_EA_NAME_LENGTH];
            if (tb_le16(entry + HPFS_EA_VALUE_LENGTH) != HPFS_EA_POINTER_BYTES)
            {
                ok = hpfs_fail(EIO);
                break;
            }
            ok = auxiliary_free(d, tb_le32(pointer + HPFS_EA_POINTER_SECTOR),
                                tb_le32(pointer + HPFS_EA_POINTER_SIZE), method == HPFS_EA_TREE);
        }
        at += count;
    }
    free(data);
    return ok;
}

int tb_hpfs_metadata_get(const char *path, const char *expected, void *output, size_t capacity,
                         size_t *required)
{
    *required = 0;
    HpfsVolume disk;
    if (!hpfs_open(path, 0, expected, &disk))
        return 0;
    HpfsDirectory parent = {0};
    size_t index;
    unsigned char fnode[HPFS_SECTOR_BYTES], *raw = NULL, *flat = NULL;
    size_t size = 0, used = FEA_LIST_HEADER_BYTES;
    int ok = hpfs_locate(&disk, path, &parent, &index, 0) &&
             hpfs_read_sectors(&disk, tb_le32(parent.entries[index].record + HPFS_DIRENT_FNODE),
                               fnode, 1) &&
             tb_le32(fnode) == HPFS_FNODE_SIGNATURE;
    if (ok)
    {
        raw = ea_raw(&disk, fnode, &size);
        flat = malloc(HPFS_EA_LIST_MAX);
        ok = raw && flat;
    }
    for (size_t at = 0; ok && at < size;)
    {
        if (size - at < HPFS_EA_FIXED_BYTES)
        {
            ok = hpfs_fail(EIO);
            break;
        }
        const unsigned char *entry = raw + at;
        size_t stored = HPFS_EA_FIXED_BYTES + entry[HPFS_EA_NAME_LENGTH] +
                        tb_le16(entry + HPFS_EA_VALUE_LENGTH),
               value = tb_le16(entry + HPFS_EA_VALUE_LENGTH);
        unsigned method = entry[HPFS_EA_FLAGS] & HPFS_EA_METHOD;
        if (stored > size - at || !entry[HPFS_EA_NAME_LENGTH] ||
            entry[HPFS_EA_NAME + entry[HPFS_EA_NAME_LENGTH]] ||
            memchr(entry + HPFS_EA_NAME, 0, entry[HPFS_EA_NAME_LENGTH]) ||
            (method != HPFS_EA_INLINE && method != HPFS_EA_EXTERNAL && method != HPFS_EA_TREE))
        {
            ok = hpfs_fail(EIO);
            break;
        }
        const unsigned char *pointer = entry + HPFS_EA_FIXED_BYTES + entry[HPFS_EA_NAME_LENGTH];
        if (method)
        {
            if (value != HPFS_EA_POINTER_BYTES)
            {
                ok = hpfs_fail(EIO);
                break;
            }
            value = tb_le32(pointer + HPFS_EA_POINTER_SIZE);
        }
        size_t count = HPFS_EA_FIXED_BYTES + entry[HPFS_EA_NAME_LENGTH] + value;
        if (count > HPFS_EA_LIST_MAX - used || value > HPFS_EA_LIST_MAX)
        {
            ok = hpfs_fail(EFBIG);
            break;
        }
        unsigned char *record = flat + used;
        record[HPFS_EA_FLAGS] = entry[HPFS_EA_FLAGS] & HPFS_EA_NEEDED;
        record[HPFS_EA_NAME_LENGTH] = entry[HPFS_EA_NAME_LENGTH];
        tb_put_le16(record + HPFS_EA_VALUE_LENGTH, (unsigned)value);
        memcpy(record + HPFS_EA_NAME, entry + HPFS_EA_NAME, entry[HPFS_EA_NAME_LENGTH] + 1);
        if (method)
            ok = auxiliary_read(&disk, tb_le32(pointer + HPFS_EA_POINTER_SECTOR), (uint32_t)value,
                                method == HPFS_EA_TREE,
                                record + HPFS_EA_FIXED_BYTES + entry[HPFS_EA_NAME_LENGTH]);
        else
            memcpy(record + HPFS_EA_FIXED_BYTES + entry[HPFS_EA_NAME_LENGTH], pointer, value);
        used += count;
        at += stored;
    }
    if (ok && size)
    {
        tb_put_le32(flat, (uint32_t)used);
        *required = used;
        if (output)
        {
            if (capacity < used)
                ok = hpfs_fail(ERANGE);
            else
                memcpy(output, flat, used);
        }
    }
    free(raw);
    free(flat);
    hpfs_directory_free(&parent);
    hpfs_close(&disk);
    return ok;
}

int tb_hpfs_metadata_set(const char *path, const char *expected, const void *bytes, size_t size)
{
    if (size < FEA_LIST_HEADER_BYTES || size > HPFS_EA_LIST_MAX || tb_le32(bytes) != size)
        return hpfs_fail(EINVAL);
    const unsigned char *data = bytes;
    unsigned needed = 0;
    for (size_t at = FEA_LIST_HEADER_BYTES; at < size;)
    {
        if (size - at < HPFS_EA_FIXED_BYTES)
            return hpfs_fail(EINVAL);
        size_t count = HPFS_EA_FIXED_BYTES + data[at + HPFS_EA_NAME_LENGTH] +
                       tb_le16(data + at + HPFS_EA_VALUE_LENGTH);
        if (count > size - at || (data[at + HPFS_EA_FLAGS] & HPFS_EA_METHOD) ||
            !data[at + HPFS_EA_NAME_LENGTH] ||
            data[at + HPFS_EA_NAME + data[at + HPFS_EA_NAME_LENGTH]] ||
            memchr(data + at + HPFS_EA_NAME, 0, data[at + HPFS_EA_NAME_LENGTH]))
            return hpfs_fail(EINVAL);
        needed += !!(data[at + HPFS_EA_FLAGS] & HPFS_EA_NEEDED);
        at += count;
    }
    HpfsVolume disk;
    if (!hpfs_open(path, 1, expected, &disk))
        return 0;
    HpfsDirectory parent = {0};
    size_t index;
    unsigned char fnode[HPFS_SECTOR_BYTES];
    int ok = hpfs_load_bitmaps(&disk) && hpfs_locate(&disk, path, &parent, &index, 0) &&
             hpfs_read_sectors(&disk, tb_le32(parent.entries[index].record + HPFS_DIRENT_FNODE),
                               fnode, 1) &&
             tb_le32(fnode) == HPFS_FNODE_SIGNATURE;
    if (ok && parent.entries[index].record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_READONLY)
        ok = hpfs_fail(EACCES);
    if (ok)
        ok = hpfs_ea_values_free(&disk, fnode) &&
             hpfs_release_external(&disk, fnode, HPFS_FNODE_EAS);
    if (ok)
    {
        unsigned base = tb_le16(fnode + HPFS_FNODE_INTERNAL_OFFSET),
                 acl = tb_le16(fnode + HPFS_FNODE_ACL_INTERNAL_SIZE);
        size_t n = size - FEA_LIST_HEADER_BYTES;
        if (!base)
            base = HPFS_FNODE_FREE_AREA;
        if (base < HPFS_FNODE_FREE_AREA || base > HPFS_SECTOR_BYTES ||
            acl > HPFS_SECTOR_BYTES - base)
            ok = hpfs_fail(EIO);
        else
        {
            memset(fnode + base + acl, 0, HPFS_SECTOR_BYTES - base - acl);
            tb_put_le32(fnode + HPFS_FNODE_EAS, 0);
            tb_put_le32(fnode + HPFS_FNODE_EA_SECTOR, 0);
            tb_put_le16(fnode + HPFS_FNODE_EA_INTERNAL_SIZE, 0);
            fnode[HPFS_FNODE_EA_IN_ANODE] = 0;
            tb_put_le16(fnode + HPFS_FNODE_INTERNAL_OFFSET, base);
            tb_put_le32(fnode + HPFS_FNODE_NEEDED_EAS, needed);
            if (n <= HPFS_SECTOR_BYTES - base - acl)
            {
                memcpy(fnode + base + acl, data + FEA_LIST_HEADER_BYTES, n);
                tb_put_le16(fnode + HPFS_FNODE_EA_INTERNAL_SIZE, (unsigned)n);
            }
            else
            {
                uint32_t count = (uint32_t)((n + HPFS_SECTOR_BYTES - 1) / HPFS_SECTOR_BYTES),
                         first = hpfs_allocate(&disk, count, 1);
                if (!first)
                    ok = 0;
                for (uint32_t i = 0; ok && i < count; i++)
                {
                    unsigned char block[HPFS_SECTOR_BYTES] = {0};
                    size_t length = n - i * HPFS_SECTOR_BYTES < HPFS_SECTOR_BYTES
                                        ? n - i * HPFS_SECTOR_BYTES
                                        : HPFS_SECTOR_BYTES;
                    memcpy(block, data + FEA_LIST_HEADER_BYTES + i * HPFS_SECTOR_BYTES, length);
                    ok = hpfs_stage_sectors(&disk, first + i, block, 1);
                }
                tb_put_le32(fnode + HPFS_FNODE_EAS, (uint32_t)n);
                tb_put_le32(fnode + HPFS_FNODE_EA_SECTOR, first);
            }
            if (ok)
            {
                unsigned char *entry = parent.entries[index].record;
                entry[HPFS_DIRENT_FLAGS] =
                    (entry[HPFS_DIRENT_FLAGS] &
                     ~(unsigned)(HPFS_DIRENT_HAS_EAS | HPFS_DIRENT_NEEDS_EAS)) |
                    (n ? HPFS_DIRENT_HAS_EAS : 0) | (needed ? HPFS_DIRENT_NEEDS_EAS : 0);
                tb_put_le32(entry + HPFS_DIRENT_EA_SIZE, (uint32_t)n);
                ok = hpfs_stage_sectors(&disk, tb_le32(entry + HPFS_DIRENT_FNODE), fnode, 1) &&
                     hpfs_directory_save(&disk, parent.entries[index].parent, &parent) &&
                     hpfs_commit(&disk);
            }
        }
    }
    hpfs_directory_free(&parent);
    hpfs_close(&disk);
    return ok;
}
