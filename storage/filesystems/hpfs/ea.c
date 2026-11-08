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
static int auxiliary_read(HpfsVolume *d, uint32_t sector, uint32_t size, int tree,
                          unsigned char *output)
{
    if (!size)
        return 1;
    if (size > 1048576)
        return hpfs_fail(EFBIG);
    if (!tree)
    {
        unsigned char data[512];
        for (uint32_t at = 0; at < size;)
        {
            unsigned n = size - at < 512 ? size - at : 512;
            if (!hpfs_read_sectors(d, sector + at / 512, data, 1))
                return 0;
            memcpy(output + at, data, n);
            at += n;
        }
        return 1;
    }
    HpfsFile file = {0};
    unsigned char node[512];
    int ok = hpfs_read_sectors(d, sector, node, 1) && tb_le32(node) == HPFS_ANODE_SIGNATURE &&
             hpfs_add_unique_node(&file.nodes, &file.node_count, sector) &&
             hpfs_extent_tree(d, &file, node + 12, node[12] & 128 ? 60 : 40, 0);
    for (uint32_t at = 0; ok && at < size;)
    {
        unsigned char data[512];
        uint32_t physical = hpfs_file_sector(&file, at / 512);
        unsigned n = size - at < 512 ? size - at : 512;
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
    uint32_t external = tb_le32(fnode + 44);
    unsigned internal = tb_le16(fnode + 52), base = tb_le16(fnode + 184), acl = tb_le16(fnode + 40);
    if ((external && internal) || external > 1048576 || base > 512 || acl > 512 - base ||
        internal > 512 - base - acl)
    {
        errno = EIO;
        return NULL;
    }
    *size = external ? external : internal;
    unsigned char *data = malloc(*size ? *size : 1);
    if (!data)
        return NULL;
    if (external && !auxiliary_read(d, tb_le32(fnode + 48), external, fnode[54], data))
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
        return hpfs_mark_sectors(d, sector, (uint32_t)(((uint64_t)size + 511) / 512), 1);
    HpfsFile file = {0};
    unsigned char node[512];
    int ok = hpfs_read_sectors(d, sector, node, 1) && tb_le32(node) == HPFS_ANODE_SIGNATURE &&
             hpfs_add_unique_node(&file.nodes, &file.node_count, sector) &&
             hpfs_extent_tree(d, &file, node + 12, node[12] & 128 ? 60 : 40, 0);
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
        if (size - at < 5)
        {
            ok = hpfs_fail(EIO);
            break;
        }
        const unsigned char *entry = data + at;
        size_t count = 5u + entry[1] + tb_le16(entry + 2);
        unsigned method = entry[0] & 127;
        if (count > size - at || entry[4 + entry[1]] || (method != 0 && method != 1 && method != 3))
        {
            ok = hpfs_fail(EIO);
            break;
        }
        if (method)
        {
            const unsigned char *pointer = entry + 5 + entry[1];
            if (tb_le16(entry + 2) != 8)
            {
                ok = hpfs_fail(EIO);
                break;
            }
            ok = auxiliary_free(d, tb_le32(pointer + 4), tb_le32(pointer), method == 3);
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
    unsigned char fnode[512], *raw = NULL, *flat = NULL;
    size_t size = 0, used = 4;
    int ok = hpfs_locate(&disk, path, &parent, &index, 0) &&
             hpfs_read_sectors(&disk, tb_le32(parent.entries[index].record + 4), fnode, 1) &&
             tb_le32(fnode) == HPFS_FNODE_SIGNATURE;
    if (ok)
    {
        raw = ea_raw(&disk, fnode, &size);
        flat = malloc(65535);
        ok = raw && flat;
    }
    for (size_t at = 0; ok && at < size;)
    {
        if (size - at < 5)
        {
            ok = hpfs_fail(EIO);
            break;
        }
        const unsigned char *entry = raw + at;
        size_t stored = 5u + entry[1] + tb_le16(entry + 2), value = tb_le16(entry + 2);
        unsigned method = entry[0] & 127;
        if (stored > size - at || !entry[1] || entry[4 + entry[1]] ||
            memchr(entry + 4, 0, entry[1]) || (method != 0 && method != 1 && method != 3))
        {
            ok = hpfs_fail(EIO);
            break;
        }
        const unsigned char *pointer = entry + 5 + entry[1];
        if (method)
        {
            if (value != 8)
            {
                ok = hpfs_fail(EIO);
                break;
            }
            value = tb_le32(pointer);
        }
        size_t count = 5u + entry[1] + value;
        if (count > 65535 - used || value > 65535)
        {
            ok = hpfs_fail(EFBIG);
            break;
        }
        unsigned char *record = flat + used;
        record[0] = entry[0] & 128;
        record[1] = entry[1];
        tb_put_le16(record + 2, (unsigned)value);
        memcpy(record + 4, entry + 4, entry[1] + 1);
        if (method)
            ok = auxiliary_read(&disk, tb_le32(pointer + 4), (uint32_t)value, method == 3,
                                record + 5 + entry[1]);
        else
            memcpy(record + 5 + entry[1], pointer, value);
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
    if (size < 4 || size > 65535 || tb_le32(bytes) != size)
        return hpfs_fail(EINVAL);
    const unsigned char *data = bytes;
    unsigned needed = 0;
    for (size_t at = 4; at < size;)
    {
        if (size - at < 5)
            return hpfs_fail(EINVAL);
        size_t count = 5u + data[at + 1] + tb_le16(data + at + 2);
        if (count > size - at || (data[at] & 127) || !data[at + 1] || data[at + 4 + data[at + 1]] ||
            memchr(data + at + 4, 0, data[at + 1]))
            return hpfs_fail(EINVAL);
        needed += !!(data[at] & 128);
        at += count;
    }
    HpfsVolume disk;
    if (!hpfs_open(path, 1, expected, &disk))
        return 0;
    HpfsDirectory parent = {0};
    size_t index;
    unsigned char fnode[512];
    int ok = hpfs_load_bitmaps(&disk) && hpfs_locate(&disk, path, &parent, &index, 0) &&
             hpfs_read_sectors(&disk, tb_le32(parent.entries[index].record + 4), fnode, 1) &&
             tb_le32(fnode) == HPFS_FNODE_SIGNATURE;
    if (ok && parent.entries[index].record[3] & 1)
        ok = hpfs_fail(EACCES);
    if (ok)
        ok = hpfs_ea_values_free(&disk, fnode) && hpfs_release_external(&disk, fnode, 44);
    if (ok)
    {
        unsigned base = tb_le16(fnode + 184), acl = tb_le16(fnode + 40);
        size_t n = size - 4;
        if (!base)
            base = 196;
        if (base < 196 || base > 512 || acl > 512 - base)
            ok = hpfs_fail(EIO);
        else
        {
            memset(fnode + base + acl, 0, 512 - base - acl);
            tb_put_le32(fnode + 44, 0);
            tb_put_le32(fnode + 48, 0);
            tb_put_le16(fnode + 52, 0);
            fnode[54] = 0;
            tb_put_le16(fnode + 184, base);
            tb_put_le32(fnode + 164, needed);
            if (n <= 512 - base - acl)
            {
                memcpy(fnode + base + acl, data + 4, n);
                tb_put_le16(fnode + 52, (unsigned)n);
            }
            else
            {
                uint32_t count = (uint32_t)((n + 511) / 512),
                         first = hpfs_allocate(&disk, count, 1);
                if (!first)
                    ok = 0;
                for (uint32_t i = 0; ok && i < count; i++)
                {
                    unsigned char block[512] = {0};
                    size_t length = n - i * 512 < 512 ? n - i * 512 : 512;
                    memcpy(block, data + 4 + i * 512, length);
                    ok = hpfs_stage_sectors(&disk, first + i, block, 1);
                }
                tb_put_le32(fnode + 44, (uint32_t)n);
                tb_put_le32(fnode + 48, first);
            }
            if (ok)
            {
                unsigned char *entry = parent.entries[index].record;
                entry[2] = (entry[2] & ~(16u | 128u)) | (n ? 16 : 0) | (needed ? 128 : 0);
                tb_put_le32(entry + 24, (uint32_t)n);
                ok = hpfs_stage_sectors(&disk, tb_le32(entry + 4), fnode, 1) &&
                     hpfs_directory_save(&disk, parent.entries[index].parent, &parent) &&
                     hpfs_commit(&disk);
            }
        }
    }
    hpfs_directory_free(&parent);
    hpfs_close(&disk);
    return ok;
}
