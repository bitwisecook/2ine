/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS directories: dnode B-tree read and rebuild, entry names, path lookup. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/volume/volume_internal.h"
#include "storage/volume/member_path.h"
#include "storage/common/host_stdio.h"
#include "storage/vendor/fatfs/ff.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
    END_ENTRY_BYTES = HPFS_DIRENT_MIN_BYTES,
    DOWN_END_ENTRY_BYTES = HPFS_DIRENT_MIN_BYTES + HPFS_DIRENT_DOWN_BYTES,
    /* The end entry's one-byte name, which sorts after every name. */
    END_ENTRY_NAME = 0xff,
    /* Upper bound on the keys placed in one dnode when a tree is rebuilt. */
    MAX_KEYS = 64
};

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
    if (depth > HPFS_MAX_TREE_DEPTH ||
        !hpfs_add_unique_node(&list->nodes, &list->node_count, sector))
        return hpfs_fail(EIO);
    unsigned char block[HPFS_DNODE_BYTES];
    if (sector % HPFS_DNODE_SECTORS || !hpfs_read_sectors(d, sector, block, HPFS_DNODE_SECTORS) ||
        tb_le32(block) != HPFS_DNODE_SIGNATURE || tb_le32(block + HPFS_DNODE_SELF) != sector)
        return hpfs_fail(EIO);
    unsigned end = tb_le32(block + HPFS_DNODE_USED);
    if (end > HPFS_DNODE_BYTES || end < HPFS_DNODE_ENTRIES + END_ENTRY_BYTES)
        return hpfs_fail(EIO);
    for (unsigned at = HPFS_DNODE_ENTRIES; at < end;)
    {
        const unsigned char *entry = block + at;
        unsigned size = tb_le16(entry + HPFS_DIRENT_SIZE), flags = entry[HPFS_DIRENT_FLAGS];
        if (size < HPFS_DIRENT_MIN_BYTES || size % 4 || size > end - at ||
            HPFS_DIRENT_NAME + entry[HPFS_DIRENT_NAME_LENGTH] +
                    ((flags & HPFS_DIRENT_DOWN) ? (unsigned)HPFS_DIRENT_DOWN_BYTES : 0u) >
                size)
            return hpfs_fail(EIO);
        if ((flags & HPFS_DIRENT_DOWN) &&
            !directory_read_node(d, tb_le32(entry + size - HPFS_DIRENT_DOWN_BYTES), list, parent,
                                 depth + 1))
            return 0;
        if (flags & HPFS_DIRENT_END)
            return at + size == end ? 1 : hpfs_fail(EIO);
        if ((entry[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_HIGH) ||
            ((entry[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_INDEX) &&
             (!d->codepages ||
              (entry[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_INDEX) >= d->codepage_count)))
            return hpfs_fail(ENOTSUP);
        HpfsEntry *next = realloc(list->entries, (list->count + 1) * sizeof *next);
        if (!next)
            return 0;
        list->entries = next;
        size -= (flags & HPFS_DIRENT_DOWN) ? HPFS_DIRENT_DOWN_BYTES : 0;
        unsigned char *copy = malloc(size);
        if (!copy)
            return 0;
        memcpy(copy, entry, size);
        copy[HPFS_DIRENT_FLAGS] &= ~(unsigned)HPFS_DIRENT_DOWN;
        tb_put_le16(copy + HPFS_DIRENT_SIZE, size);
        list->entries[list->count++] = (HpfsEntry){copy, size, parent};
        at += tb_le16(entry + HPFS_DIRENT_SIZE);
    }
    return hpfs_fail(EIO);
}

int hpfs_directory_read(HpfsVolume *d, uint32_t fnode, HpfsDirectory *list)
{
    memset(list, 0, sizeof *list);
    unsigned char file[HPFS_SECTOR_BYTES];
    if (!hpfs_read_sectors(d, fnode, file, 1) || tb_le32(file) != HPFS_FNODE_SIGNATURE ||
        !(file[HPFS_FNODE_FLAGS] & HPFS_FNODE_DIRECTORY))
        return hpfs_fail(ENOTDIR);
    /* A directory fnode's tree has one entry, whose sector is the root dnode. */
    uint32_t root = tb_le32(file + HPFS_FNODE_TREE + HPFS_TREE_ENTRIES +
                            ((file[HPFS_FNODE_TREE + HPFS_TREE_FLAGS] & HPFS_TREE_INTERNAL)
                                 ? HPFS_BRANCH_SECTOR
                                 : HPFS_LEAF_SECTOR));
    if (!directory_read_node(d, root, list, fnode, 0))
    {
        hpfs_directory_free(list);
        return 0;
    }
    return 1;
}

char *hpfs_name_utf8(HpfsVolume *d, const unsigned char *entry)
{
    size_t n = entry[HPFS_DIRENT_NAME_LENGTH], used = 0;
    char *name = malloc(n * 3 + 1);
    if (!name)
        return NULL;
    for (size_t i = 0; i < n; i++)
    {
        unsigned cp = entry[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_INDEX;
        unsigned c = entry[HPFS_DIRENT_NAME + i],
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
        unsigned cp = list->entries[i].record[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_INDEX;
        unsigned char oem[255];
        size_t n;
        if (!name_oem(d, cp, name, oem, &n))
            continue;
        if (!compare_name(d, oem, n, cp, list->entries[i].record + HPFS_DIRENT_NAME,
                          list->entries[i].record[HPFS_DIRENT_NAME_LENGTH], cp))
        {
            *index = i;
            return 1;
        }
    }
    return hpfs_fail(ENOENT);
}

char *hpfs_member_path(const char *path)
{
    return tb_member_path(tb_image_member(path));
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
        directory = tb_le32(entry + HPFS_DIRENT_FNODE);
        int isdir = entry[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_DIRECTORY;
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
    unsigned char block[HPFS_DNODE_BYTES] = {0};
    tb_put_le32(block, HPFS_DNODE_SIGNATURE);
    tb_put_le32(block + HPFS_DNODE_TOP, root ? 1 : 0);
    tb_put_le32(block + HPFS_DNODE_PARENT, parent);
    tb_put_le32(block + HPFS_DNODE_SELF, sector);
    size_t at = HPFS_DNODE_ENTRIES;
    if (!height)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (entries[i].size > HPFS_DNODE_BYTES - at - END_ENTRY_BYTES)
            {
                errno = EIO;
                return 0;
            }
            memcpy(block + at, entries[i].record, entries[i].size);
            at += entries[i].size;
        }
        tb_put_le16(block + at + HPFS_DIRENT_SIZE, END_ENTRY_BYTES);
        block[at + HPFS_DIRENT_FLAGS] = HPFS_DIRENT_END;
        block[at + HPFS_DIRENT_NAME_LENGTH] = 1;
        block[at + HPFS_DIRENT_NAME] = END_ENTRY_NAME;
        at += END_ENTRY_BYTES;
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
                if (entry->size + HPFS_DIRENT_DOWN_BYTES >
                    HPFS_DNODE_BYTES - at - DOWN_END_ENTRY_BYTES)
                {
                    errno = EIO;
                    return 0;
                }
                memcpy(block + at, entry->record, entry->size);
                tb_put_le16(block + at + HPFS_DIRENT_SIZE,
                            (unsigned)(entry->size + HPFS_DIRENT_DOWN_BYTES));
                block[at + HPFS_DIRENT_FLAGS] |= HPFS_DIRENT_DOWN;
                tb_put_le32(block + at + entry->size, child);
                at += entry->size + HPFS_DIRENT_DOWN_BYTES;
            }
            else
            {
                tb_put_le16(block + at + HPFS_DIRENT_SIZE, DOWN_END_ENTRY_BYTES);
                block[at + HPFS_DIRENT_FLAGS] = HPFS_DIRENT_END | HPFS_DIRENT_DOWN;
                block[at + HPFS_DIRENT_NAME_LENGTH] = 1;
                block[at + HPFS_DIRENT_NAME] = END_ENTRY_NAME;
                tb_put_le32(block + at + END_ENTRY_BYTES, child);
                at += DOWN_END_ENTRY_BYTES;
            }
        }
    }
    tb_put_le32(block + HPFS_DNODE_USED, (uint32_t)at);
    return hpfs_stage_sectors(d, sector, block, HPFS_DNODE_SECTORS) ? sector : 0;
}

int hpfs_directory_save(HpfsVolume *d, uint32_t fnode, HpfsDirectory *list)
{
    for (size_t i = 0; i < list->node_count; i++)
        if (!hpfs_free_dnode(d, list->nodes[i]))
            return 0;
    size_t longest = HPFS_DIRENT_MIN_BYTES;
    for (size_t i = 0; i < list->count; i++)
        if (list->entries[i].size > longest)
            longest = list->entries[i].size;
    unsigned keys = (HPFS_DNODE_BYTES - HPFS_DNODE_ENTRIES - DOWN_END_ENTRY_BYTES) /
                    (longest + HPFS_DIRENT_DOWN_BYTES);
    if (!keys)
        return hpfs_fail(ENOTSUP);
    if (keys > MAX_KEYS)
        keys = MAX_KEYS;
    unsigned height = 0;
    while (tree_capacity(height, keys) < list->count)
    {
        if (++height > HPFS_MAX_TREE_DEPTH)
            return hpfs_fail(EFBIG);
    }
    uint32_t root = directory_build_node(d, list->entries, list->count, fnode, height, keys, 1);
    if (!root)
        return 0;
    unsigned char file[HPFS_SECTOR_BYTES];
    if (!hpfs_read_sectors(d, fnode, file, 1) || tb_le32(file) != HPFS_FNODE_SIGNATURE)
        return hpfs_fail(EIO);
    unsigned char *tree = file + HPFS_FNODE_TREE, *leaf = tree + HPFS_TREE_ENTRIES;
    memset(tree, 0, HPFS_FNODE_TREE_BYTES);
    tree[HPFS_TREE_FREE] = HPFS_FNODE_LEAVES - 1;
    tree[HPFS_TREE_USED] = 1;
    tb_put_le16(tree + HPFS_TREE_FIRST_FREE, HPFS_TREE_ENTRIES + HPFS_LEAF_BYTES);
    tb_put_le32(leaf + HPFS_LEAF_SECTOR, root);
    tb_put_le32(leaf + HPFS_LEAF_BYTES + HPFS_LEAF_LOGICAL, UINT32_MAX);

    return hpfs_stage_sectors(d, fnode, file, 1);
}

int hpfs_entry_insert(HpfsVolume *d, HpfsDirectory *list, HpfsEntry entry)
{
    size_t at = 0;
    while (at < list->count &&
           compare_name(d, list->entries[at].record + HPFS_DIRENT_NAME,
                        list->entries[at].record[HPFS_DIRENT_NAME_LENGTH],
                        list->entries[at].record[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_INDEX,
                        entry.record + HPFS_DIRENT_NAME, entry.record[HPFS_DIRENT_NAME_LENGTH],
                        entry.record[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_INDEX) < 0)
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
    int ok = name_oem(d, previous ? previous[HPFS_DIRENT_CODEPAGE] & HPFS_DIRENT_CODEPAGE_INDEX : 0,
                      tail, bytes, &n);
    if (!ok)
    {
        free(name);
        return 0;
    }
    size_t extra = previous ? tb_le16(previous + HPFS_DIRENT_SIZE) -
                                  ((HPFS_DIRENT_NAME + previous[HPFS_DIRENT_NAME_LENGTH] + 3) & ~3u)
                            : 0;
    size_t size = ((HPFS_DIRENT_NAME + n + 3) & ~3u) + extra;
    unsigned char *record = calloc(size, 1);
    if (!record)
    {
        free(name);
        return 0;
    }
    if (previous)
    {
        memcpy(record, previous, HPFS_DIRENT_NAME);
        if (extra)
            memcpy(record + ((HPFS_DIRENT_NAME + n + 3) & ~3u),
                   previous + tb_le16(previous + HPFS_DIRENT_SIZE) - extra, extra);
    }
    else
    {
        record[HPFS_DIRENT_ATTRIBUTES] = HPFS_ATTR_ARCHIVED;
        uint32_t now = (uint32_t)time(NULL);
        tb_put_le32(record + HPFS_DIRENT_MODIFIED, now);
        tb_put_le32(record + HPFS_DIRENT_ACCESSED, now);
        tb_put_le32(record + HPFS_DIRENT_CREATED, now);
    }
    tb_put_le16(record + HPFS_DIRENT_SIZE, (unsigned)size);
    record[HPFS_DIRENT_NAME_LENGTH] = n;
    memcpy(record + HPFS_DIRENT_NAME, bytes, n);
    /* Names that are not valid 8.3 names (longer than 8.3's 12 bytes, or with a space). */
    if (n > 12 || memchr(bytes, ' ', n))
        record[HPFS_DIRENT_ATTRIBUTES] |= HPFS_ATTR_LONG_NAME;
    *entry = (HpfsEntry){record, size, parent};
    free(name);
    return 1;
}
