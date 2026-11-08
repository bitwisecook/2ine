/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS directories: dnode B-tree read and rebuild, entry names, path lookup. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/volume/volume_internal.h"
#include "storage/common/host_stdio.h"
#include "storage/vendor/fatfs/ff.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void hpfs_directory_free(HpfsDirectory *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->entries[i].record);
    free(list->entries);
    free(list->nodes);
    memset(list, 0, sizeof *list);
}

static int directory_read_node(HpfsVolume *d, uint32_t sector, HpfsDirectory *list, uint32_t parent,
                               unsigned depth)
{
    if (depth > 32 || !hpfs_add_unique_node(&list->nodes, &list->node_count, sector))
        return hpfs_fail(EIO);
    unsigned char block[2048];
    if (sector % 4 || !hpfs_read_sectors(d, sector, block, 4) ||
        tb_le32(block) != HPFS_DNODE_SIGNATURE || tb_le32(block + 16) != sector)
        return hpfs_fail(EIO);
    unsigned end = tb_le32(block + 4);
    if (end > 2048 || end < 52)
        return hpfs_fail(EIO);
    for (unsigned at = 20; at < end;)
    {
        const unsigned char *entry = block + at;
        unsigned size = tb_le16(entry), flags = entry[2];
        if (size < 32 || size % 4 || size > end - at ||
            31u + entry[30] + ((flags & 4) ? 4u : 0u) > size)
            return hpfs_fail(EIO);
        if ((flags & 4) &&
            !directory_read_node(d, tb_le32(entry + size - 4), list, parent, depth + 1))
            return 0;
        if (flags & 8)
            return at + size == end ? 1 : hpfs_fail(EIO);
        if ((entry[29] & 128) ||
            ((entry[29] & 127) && (!d->codepages || (entry[29] & 127) >= d->codepage_count)))
            return hpfs_fail(ENOTSUP);
        HpfsEntry *next = realloc(list->entries, (list->count + 1) * sizeof *next);
        if (!next)
            return 0;
        list->entries = next;
        size -= (flags & 4) ? 4 : 0;
        unsigned char *copy = malloc(size);
        if (!copy)
            return 0;
        memcpy(copy, entry, size);
        copy[2] &= ~4u;
        tb_put_le16(copy, size);
        list->entries[list->count++] = (HpfsEntry){copy, size, parent};
        at += tb_le16(entry);
    }
    return hpfs_fail(EIO);
}

int hpfs_directory_read(HpfsVolume *d, uint32_t fnode, HpfsDirectory *list)
{
    memset(list, 0, sizeof *list);
    unsigned char file[512];
    if (!hpfs_read_sectors(d, fnode, file, 1) || tb_le32(file) != HPFS_FNODE_SIGNATURE ||
        !(file[55] & 1))
        return hpfs_fail(ENOTDIR);
    uint32_t root = tb_le32(file + ((file[56] & 128) ? 68 : 72));
    if (!directory_read_node(d, root, list, fnode, 0))
    {
        hpfs_directory_free(list);
        return 0;
    }
    return 1;
}

char *hpfs_name_utf8(HpfsVolume *d, const unsigned char *entry)
{
    size_t n = entry[30], used = 0;
    char *name = malloc(n * 3 + 1);
    if (!name)
        return NULL;
    for (size_t i = 0; i < n; i++)
    {
        unsigned cp = entry[29] & 127;
        unsigned c = entry[31 + i],
                 value = c < 128 ? c
                                 : ff_oem2uni(c, d->codepages ? d->codepages[cp].codepage
                                                              : d->codepage);
        if (!value || value == '/' || value == '\\' || value == ':')
        {
            free(name);
            errno = EINVAL;
            return NULL;
        }
        if (value < 128)
            name[used++] = value;
        else if (value < 2048)
        {
            name[used++] = 0xc0 | value >> 6;
            name[used++] = 0x80 | (value & 63);
        }
        else
        {
            name[used++] = 0xe0 | value >> 12;
            name[used++] = 0x80 | ((value >> 6) & 63);
            name[used++] = 0x80 | (value & 63);
        }
    }
    name[used] = 0;
    return name;
}

static int name_oem(HpfsVolume *d, unsigned index, const char *text, unsigned char name[255],
                    size_t *length)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t n = 0;
    while (*p)
    {
        unsigned value = *p++;
        if (value >= 128)
        {
            unsigned more = value >= 0xe0 && value < 0xf0   ? 2
                            : value >= 0xc2 && value < 0xe0 ? 1
                                                            : 0;
            if (!more)
                return hpfs_fail(EILSEQ);
            value &= more == 2 ? 15 : 31;
            for (unsigned i = 0; i < more; i++)
            {
                if ((*p & 0xc0) != 0x80)
                    return hpfs_fail(EILSEQ);
                value = value << 6 | (*p++ & 63);
            }
            if (value >= 0xd800 && value <= 0xdfff)
                return hpfs_fail(EILSEQ);
            value = ff_uni2oem(value, d->codepages ? d->codepages[index].codepage : d->codepage);
            if (!value || value > 255)
                return hpfs_fail(EILSEQ);
        }
        if (value < 32 || strchr("<>:\"/\\|?*", value) || n == 254)
            return hpfs_fail(EINVAL);
        name[n++] = value;
    }
    if (!n || name[n - 1] == ' ' || name[n - 1] == '.')
        return hpfs_fail(EINVAL);
    *length = n;
    return 1;
}

static int compare_name(HpfsVolume *d, const unsigned char *a, size_t na, unsigned ia,
                        const unsigned char *b, size_t nb, unsigned ib)
{
    const unsigned char *ma = d->codepages ? d->codepages[ia].map : d->case_map;
    const unsigned char *mb = d->codepages ? d->codepages[ib].map : d->case_map;
    size_t n = na < nb ? na : nb;
    for (size_t i = 0; i < n; i++)
        if (ma[a[i]] != mb[b[i]])
            return ma[a[i]] < mb[b[i]] ? -1 : 1;
    return na < nb ? -1 : na > nb;
}

static int find_entry(HpfsVolume *d, HpfsDirectory *list, const char *name, size_t *index)
{
    for (size_t i = 0; i < list->count; i++)
    {
        unsigned cp = list->entries[i].record[29] & 127;
        unsigned char oem[255];
        size_t n;
        if (!name_oem(d, cp, name, oem, &n))
            continue;
        if (!compare_name(d, oem, n, cp, list->entries[i].record + 31, list->entries[i].record[30],
                          cp))
        {
            *index = i;
            return 1;
        }
    }
    return hpfs_fail(ENOENT);
}

char *hpfs_member_path(const char *path)
{
    const char *member = tb_image_member(path);
    if (!member || strpbrk(member, "\\:"))
    {
        errno = EINVAL;
        return NULL;
    }
    char *copy = strdup(member), *out = malloc(strlen(member) + 1);
    size_t positions[256], depth = 0, used = 0;
    if (!copy || !out)
    {
        free(copy);
        free(out);
        return NULL;
    }
    char *cursor = copy;
    while (*cursor)
    {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char *part = cursor;
        while (*cursor && *cursor != '/')
            cursor++;
        if (*cursor)
            *cursor++ = 0;
        if (!strcmp(part, "."))
            continue;
        if (!strcmp(part, ".."))
        {
            if (!depth)
            {
                free(copy);
                free(out);
                errno = EACCES;
                return NULL;
            }
            used = positions[--depth];
            continue;
        }
        if (depth == 256)
        {
            free(copy);
            free(out);
            errno = ENAMETOOLONG;
            return NULL;
        }
        positions[depth++] = used;
        if (used)
            out[used++] = '/';
        size_t n = strlen(part);
        memcpy(out + used, part, n);
        used += n;
    }
    out[used] = 0;
    free(copy);
    return out;
}

int hpfs_locate(HpfsVolume *d, const char *path, HpfsDirectory *parent, size_t *index,
                int allow_missing)
{
    char *copy = hpfs_member_path(path);
    if (!copy)
        return 0;
    char *cursor = copy;
    uint32_t directory = d->root;
    if (!*cursor)
    {
        free(copy);
        return hpfs_fail(EISDIR);
    }
    for (;;)
    {
        char *slash = strchr(cursor, '/');
        if (slash)
            *slash = 0;
        if (!hpfs_directory_read(d, directory, parent))
        {
            free(copy);
            return 0;
        }
        if (!find_entry(d, parent, cursor, index))
        {
            int missing = allow_missing && !slash && errno == ENOENT;
            if (!missing)
                hpfs_directory_free(parent);
            else
                *index = SIZE_MAX;
            free(copy);
            return missing;
        }
        if (!slash)
        {
            free(copy);
            return 1;
        }
        const unsigned char *entry = parent->entries[*index].record;
        directory = tb_le32(entry + 4);
        int isdir = entry[3] & 16;
        hpfs_directory_free(parent);
        if (!isdir)
        {
            free(copy);
            return hpfs_fail(ENOTDIR);
        }
        cursor = slash + 1;
    }
}

static size_t tree_capacity(unsigned height, unsigned keys)
{
    size_t capacity = keys;
    while (height--)
    {
        if (capacity > (SIZE_MAX - keys) / (keys + 1))
            return SIZE_MAX;
        capacity = capacity * (keys + 1) + keys;
    }
    return capacity;
}

static uint32_t directory_build_node(HpfsVolume *d, HpfsEntry *entries, size_t count,
                                     uint32_t parent, unsigned height, unsigned keys, int root)
{
    uint32_t sector = hpfs_allocate_dnode(d);
    if (!sector)
        return 0;
    unsigned char block[2048] = {0};
    tb_put_le32(block, HPFS_DNODE_SIGNATURE);
    tb_put_le32(block + 8, root ? 1 : 0);
    tb_put_le32(block + 12, parent);
    tb_put_le32(block + 16, sector);
    size_t at = 20;
    if (!height)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (entries[i].size > 2048 - at - 32)
            {
                errno = EIO;
                return 0;
            }
            memcpy(block + at, entries[i].record, entries[i].size);
            at += entries[i].size;
        }
        tb_put_le16(block + at, 32);
        block[at + 2] = 8;
        block[at + 30] = 1;
        block[at + 31] = 255;
        at += 32;
    }
    else
    {
        size_t capacity = tree_capacity(height - 1, keys),
               children = (count + 1 + capacity) / (capacity + 1);
        if (children < 2)
            children = 2;
        if (children > keys + 1 || count < children - 1)
        {
            errno = EIO;
            return 0;
        }
        size_t payload = count - (children - 1), each = payload / children,
               extra = payload % children, start = 0;
        for (size_t i = 0; i < children; i++)
        {
            size_t n = each + (i < extra);
            uint32_t child =
                directory_build_node(d, entries + start, n, sector, height - 1, keys, 0);
            if (!child)
                return 0;
            start += n;
            if (i + 1 < children)
            {
                HpfsEntry *entry = entries + start++;
                if (entry->size + 4 > 2048 - at - 36)
                {
                    errno = EIO;
                    return 0;
                }
                memcpy(block + at, entry->record, entry->size);
                tb_put_le16(block + at, (unsigned)(entry->size + 4));
                block[at + 2] |= 4;
                tb_put_le32(block + at + entry->size, child);
                at += entry->size + 4;
            }
            else
            {
                tb_put_le16(block + at, 36);
                block[at + 2] = 12;
                block[at + 30] = 1;
                block[at + 31] = 255;
                tb_put_le32(block + at + 32, child);
                at += 36;
            }
        }
    }
    tb_put_le32(block + 4, (uint32_t)at);
    return hpfs_stage_sectors(d, sector, block, 4) ? sector : 0;
}

int hpfs_directory_save(HpfsVolume *d, uint32_t fnode, HpfsDirectory *list)
{
    for (size_t i = 0; i < list->node_count; i++)
        if (!hpfs_free_dnode(d, list->nodes[i]))
            return 0;
    size_t longest = 32;
    for (size_t i = 0; i < list->count; i++)
        if (list->entries[i].size > longest)
            longest = list->entries[i].size;
    unsigned keys = (2028 - 36) / (longest + 4);
    if (!keys)
        return hpfs_fail(ENOTSUP);
    if (keys > 64)
        keys = 64;
    unsigned height = 0;
    while (tree_capacity(height, keys) < list->count)
    {
        if (++height > 32)
            return hpfs_fail(EFBIG);
    }
    uint32_t root = directory_build_node(d, list->entries, list->count, fnode, height, keys, 1);
    if (!root)
        return 0;
    unsigned char file[512];
    if (!hpfs_read_sectors(d, fnode, file, 1) || tb_le32(file) != HPFS_FNODE_SIGNATURE)
        return hpfs_fail(EIO);
    memset(file + 56, 0, 104);
    file[60] = 7;
    file[61] = 1;
    tb_put_le16(file + 62, 20);
    tb_put_le32(file + 72, root);
    tb_put_le32(file + 76, UINT32_MAX);
    return hpfs_stage_sectors(d, fnode, file, 1);
}

int hpfs_entry_insert(HpfsVolume *d, HpfsDirectory *list, HpfsEntry entry)
{
    size_t at = 0;
    while (at < list->count &&
           compare_name(d, list->entries[at].record + 31, list->entries[at].record[30],
                        list->entries[at].record[29] & 127, entry.record + 31, entry.record[30],
                        entry.record[29] & 127) < 0)
        at++;
    HpfsEntry *next = realloc(list->entries, (list->count + 1) * sizeof *next);
    if (!next)
        return 0;
    list->entries = next;
    memmove(next + at + 1, next + at, (list->count - at) * sizeof *next);
    next[at] = entry;
    list->count++;
    return 1;
}

void hpfs_entry_remove(HpfsDirectory *list, size_t index)
{
    free(list->entries[index].record);
    memmove(list->entries + index, list->entries + index + 1,
            (list->count - index - 1) * sizeof *list->entries);
    list->count--;
}

int hpfs_entry_named(HpfsVolume *d, const char *path, const unsigned char *previous,
                     uint32_t parent, HpfsEntry *entry)
{
    char *name = hpfs_member_path(path);
    if (!name)
        return 0;
    const char *tail = strrchr(name, '/');
    tail = tail ? tail + 1 : name;
    unsigned char bytes[255];
    size_t n;
    int ok = name_oem(d, previous ? previous[29] & 127 : 0, tail, bytes, &n);
    if (!ok)
    {
        free(name);
        return 0;
    }
    size_t extra = previous ? tb_le16(previous) - ((31u + previous[30] + 3) & ~3u) : 0;
    size_t size = ((31 + n + 3) & ~3u) + extra;
    unsigned char *record = calloc(size, 1);
    if (!record)
    {
        free(name);
        return 0;
    }
    if (previous)
    {
        memcpy(record, previous, 31);
        if (extra)
            memcpy(record + ((31 + n + 3) & ~3u), previous + tb_le16(previous) - extra, extra);
    }
    else
    {
        record[3] = 32;
        uint32_t now = (uint32_t)time(NULL);
        tb_put_le32(record + 8, now);
        tb_put_le32(record + 16, now);
        tb_put_le32(record + 20, now);
    }
    tb_put_le16(record, (unsigned)size);
    record[30] = n;
    memcpy(record + 31, bytes, n);
    if (n > 12 || memchr(bytes, ' ', n))
        record[3] |= 64;
    *entry = (HpfsEntry){record, size, parent};
    free(name);
    return 1;
}
