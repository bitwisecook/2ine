/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS volume operations: info, listing, reads, and create/delete/write/rename. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/volume/volume_internal.h"
#include "storage/common/host_stdio.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Size of a directory's ".." entry: the fixed fields and a two-byte name, rounded to 4. */
enum
{
    DOT_ENTRY_BYTES = 36
};

int tb_hpfs_info(const char *path, TbImageInfo *info)

{
    HpfsVolume disk;
    if (!hpfs_open(path, 0, NULL, &disk))
        return 0;
    char *name = hpfs_member_path(path);
    HpfsDirectory list = {0};
    size_t index = 0;
    int ok = name != NULL;
    memset(info, 0, sizeof *info);
    info->modified = INT64_MIN;
    if (ok && *name)
    {
        ok = hpfs_locate(&disk, path, &list, &index, 0);
        if (ok)
        {
            info->directory =
                !!(list.entries[index].record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_DIRECTORY);
            info->size = tb_le32(list.entries[index].record + HPFS_DIRENT_FILE_SIZE);
            info->readonly =
                !!(list.entries[index].record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_READONLY);
            info->attributes =
                list.entries[index].record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_CHANGEABLE;
            info->modified = tb_le32(list.entries[index].record + HPFS_DIRENT_MODIFIED);
            info->accessed = tb_le32(list.entries[index].record + HPFS_DIRENT_ACCESSED);
            info->created = tb_le32(list.entries[index].record + HPFS_DIRENT_CREATED);
        }
    }
    else
        info->directory = 1;
    if (ok)
    {
        info->readonly |= tb_image_protected(path) != 0;
        strcpy(info->filesystem, "HPFS");
        info->capacity = (uint64_t)disk.sectors * HPFS_SECTOR_BYTES;
        info->sector_bytes = info->cluster_bytes = HPFS_SECTOR_BYTES;
        info->serial = tb_le32(disk.boot + HPFS_BOOT_SERIAL);
        memcpy(info->label, disk.boot + HPFS_BOOT_LABEL, HPFS_BOOT_LABEL_BYTES);
        for (int i = HPFS_BOOT_LABEL_BYTES - 1; i >= 0 && info->label[i] == ' '; i--)
            info->label[i] = 0;
        if (!info->readonly && name && !*name)
        {
            ok = hpfs_load_bitmaps(&disk);
            for (uint32_t i = 0; ok && i < disk.sectors; i++)
                if (hpfs_sector_is_free(&disk, i))
                    info->available += HPFS_SECTOR_BYTES;
        }
    }
    free(name);
    hpfs_directory_free(&list);
    hpfs_close(&disk);
    return ok;
}

char **tb_hpfs_names(const char *path, size_t *count)
{
    *count = (size_t)-1;
    HpfsVolume disk;
    if (!hpfs_open(path, 0, NULL, &disk))
        return NULL;
    char *name = hpfs_member_path(path);
    HpfsDirectory parent = {0}, list = {0};
    size_t index = 0, n = 0;
    char **names = NULL;
    uint32_t fnode = disk.root;
    int ok = name != NULL;
    if (ok && *name)
    {
        ok = hpfs_locate(&disk, path, &parent, &index, 0);
        if (ok)
            fnode = tb_le32(parent.entries[index].record + HPFS_DIRENT_FNODE);
    }
    if (ok)
        ok = hpfs_directory_read(&disk, fnode, &list);
    for (size_t i = 0; ok && i < list.count; i++)
    {
        if (list.entries[i].record[HPFS_DIRENT_FLAGS] & HPFS_DIRENT_SPECIAL)
            continue;
        char *text = hpfs_name_utf8(&disk, list.entries[i].record);
        char **next = text ? realloc(names, (n + 1) * sizeof *next) : NULL;
        if (!next)
        {
            free(text);
            ok = 0;
            break;
        }
        names = next;
        names[n++] = text;
    }
    free(name);
    hpfs_directory_free(&parent);
    hpfs_directory_free(&list);
    hpfs_close(&disk);
    if (!ok)
    {
        for (size_t i = 0; i < n; i++)
            free(names[i]);
        free(names);
        return NULL;
    }
    *count = n;
    return names;
}

FILE *tb_hpfs_fopen(const char *path)
{
    HpfsVolume disk;
    if (!hpfs_open(path, 0, NULL, &disk))
        return NULL;
    HpfsDirectory parent = {0};
    HpfsFile file = {0};
    size_t index;
    FILE *snapshot = NULL;
    int ok =
        hpfs_locate(&disk, path, &parent, &index, 0) &&
        hpfs_file_load(&disk, tb_le32(parent.entries[index].record + HPFS_DIRENT_FNODE), &file);
    if (ok)
    {
        snapshot = tmpfile();
        ok = snapshot != NULL && hpfs_file_copy(&disk, &file, snapshot);
    }
    if (ok)
        ok = !fflush(snapshot) && !fseeko(snapshot, 0, SEEK_SET);
    if (!ok && snapshot)
    {
        fclose(snapshot);
        snapshot = NULL;
    }
    hpfs_file_free(&file);
    hpfs_directory_free(&parent);
    hpfs_close(&disk);
    return snapshot;
}

int hpfs_create_fnode(HpfsVolume *d, HpfsEntry *entry, int directory)
{
    uint32_t sector = hpfs_allocate(d, 1, 1);
    if (!sector)
        return 0;
    unsigned char bytes[HPFS_SECTOR_BYTES] = {0};
    tb_put_le32(bytes, HPFS_FNODE_SIGNATURE);
    bytes[HPFS_FNODE_NAME_LENGTH] = entry->record[HPFS_DIRENT_NAME_LENGTH];
    unsigned n = bytes[HPFS_FNODE_NAME_LENGTH] > HPFS_FNODE_NAME_BYTES
                     ? HPFS_FNODE_NAME_BYTES
                     : bytes[HPFS_FNODE_NAME_LENGTH];
    memcpy(bytes + HPFS_FNODE_NAME, entry->record + HPFS_DIRENT_NAME, n);
    tb_put_le32(bytes + HPFS_FNODE_PARENT, entry->parent);
    bytes[HPFS_FNODE_FLAGS] = directory;
    bytes[HPFS_FNODE_TREE + HPFS_TREE_FREE] = HPFS_FNODE_LEAVES;
    tb_put_le16(bytes + HPFS_FNODE_TREE + HPFS_TREE_FIRST_FREE, HPFS_TREE_ENTRIES);
    tb_put_le16(bytes + HPFS_FNODE_INTERNAL_OFFSET, HPFS_FNODE_FREE_AREA);
    tb_put_le32(entry->record + HPFS_DIRENT_FNODE, sector);
    if (directory)
        entry->record[HPFS_DIRENT_ATTRIBUTES] =
            HPFS_ATTR_DIRECTORY | (entry->record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_LONG_NAME);
    if (!hpfs_stage_sectors(d, sector, bytes, 1))
        return 0;
    if (directory)
    {
        /* The ".." entry: name bytes 01 01, pointing at the directory's own fnode. */
        unsigned char *dot = calloc(DOT_ENTRY_BYTES, 1);
        if (!dot)
            return 0;
        tb_put_le16(dot + HPFS_DIRENT_SIZE, DOT_ENTRY_BYTES);
        dot[HPFS_DIRENT_FLAGS] = HPFS_DIRENT_SPECIAL;
        dot[HPFS_DIRENT_ATTRIBUTES] = HPFS_ATTR_DIRECTORY;
        tb_put_le32(dot + HPFS_DIRENT_FNODE, sector);
        tb_put_le32(dot + HPFS_DIRENT_MODIFIED, tb_le32(entry->record + HPFS_DIRENT_MODIFIED));
        tb_put_le32(dot + HPFS_DIRENT_ACCESSED, tb_le32(entry->record + HPFS_DIRENT_ACCESSED));
        tb_put_le32(dot + HPFS_DIRENT_CREATED, tb_le32(entry->record + HPFS_DIRENT_CREATED));
        dot[HPFS_DIRENT_NAME_LENGTH] = 2;
        dot[HPFS_DIRENT_NAME] = dot[HPFS_DIRENT_NAME + 1] = 1;
        HpfsEntry special = {dot, DOT_ENTRY_BYTES, sector};
        HpfsDirectory list = {0};
        int ok = hpfs_entry_insert(d, &list, special);
        if (!ok)
            free(dot);
        else
            ok = hpfs_directory_save(d, sector, &list);
        hpfs_directory_free(&list);
        return ok;
    }
    return 1;
}

int hpfs_release_external(HpfsVolume *d, const unsigned char *fnode, unsigned offset)
{
    uint32_t size = tb_le32(fnode + offset + HPFS_RUN_SIZE),
             sector = tb_le32(fnode + offset + HPFS_RUN_SECTOR);
    if (!size)
        return 1;
    if (!fnode[offset + HPFS_RUN_IN_ANODE])
        return hpfs_mark_sectors(
            d, sector, (uint32_t)(((uint64_t)size + HPFS_SECTOR_BYTES - 1) / HPFS_SECTOR_BYTES), 1);
    HpfsFile tree = {0};
    unsigned char node[HPFS_SECTOR_BYTES];
    int ok = hpfs_read_sectors(d, sector, node, 1) && tb_le32(node) == HPFS_ANODE_SIGNATURE &&
             hpfs_add_unique_node(&tree.nodes, &tree.node_count, sector) &&
             hpfs_extent_tree(d, &tree, node + HPFS_ANODE_TREE, hpfs_anode_capacity(node), 0);
    for (size_t i = 0; ok && i < tree.count; i++)
        ok = hpfs_mark_sectors(d, tree.extents[i].sector, tree.extents[i].count, 1);
    for (size_t i = 0; ok && i < tree.node_count; i++)
        ok = hpfs_mark_sectors(d, tree.nodes[i], 1, 1);
    hpfs_file_free(&tree);
    return ok;
}

static int file_delete(HpfsVolume *d, HpfsEntry *entry, int directory)
{
    uint32_t sector = tb_le32(entry->record + HPFS_DIRENT_FNODE);
    unsigned char fnode[HPFS_SECTOR_BYTES];
    if (!hpfs_read_sectors(d, sector, fnode, 1) || tb_le32(fnode) != HPFS_FNODE_SIGNATURE)
        return hpfs_fail(EIO);
    if (directory)
    {
        HpfsDirectory list;
        if (!hpfs_directory_read(d, sector, &list))
            return 0;
        int ok = 1;
        for (size_t i = 0; i < list.count; i++)
            if (!(list.entries[i].record[HPFS_DIRENT_FLAGS] & HPFS_DIRENT_SPECIAL))
                ok = hpfs_fail(ENOTEMPTY);
        for (size_t i = 0; ok && i < list.node_count; i++)
            ok = hpfs_free_dnode(d, list.nodes[i]);
        hpfs_directory_free(&list);
        if (!ok)
            return 0;
    }
    else
    {
        HpfsFile file;
        if (!hpfs_file_load(d, sector, &file))
            return 0;
        int ok = 1;
        for (size_t i = 0; ok && i < file.count; i++)
            ok = hpfs_mark_sectors(d, file.extents[i].sector, file.extents[i].count, 1);
        for (size_t i = 0; ok && i < file.node_count; i++)
            ok = hpfs_mark_sectors(d, file.nodes[i], 1, 1);
        hpfs_file_free(&file);
        if (!ok)
            return 0;
    }
    return hpfs_ea_values_free(d, fnode) && hpfs_release_external(d, fnode, HPFS_FNODE_ACL) &&
           hpfs_release_external(d, fnode, HPFS_FNODE_EAS) && hpfs_mark_sectors(d, sector, 1, 1);
}

static int write_file(HpfsVolume *d, HpfsEntry *entry, int action, uint64_t offset,
                      const void *data, size_t size)
{
    HpfsFile file;
    if (!hpfs_file_load(d, tb_le32(entry->record + HPFS_DIRENT_FNODE), &file))
        return 0;
    uint64_t length = action == TB_FS_RESIZE        ? offset
                      : offset <= UINT64_MAX - size ? offset + size
                                                    : UINT64_MAX;
    if (action != TB_FS_RESIZE && length < tb_le32(file.data + HPFS_FNODE_FILE_SIZE))
        length = tb_le32(file.data + HPFS_FNODE_FILE_SIZE);
    int ok = hpfs_file_resize(d, &file, length);
    for (size_t at = 0; ok && action == TB_FS_WRITE && at < size;)
    {
        unsigned char bytes[HPFS_SECTOR_BYTES];
        uint64_t location = offset + at;
        uint32_t sector = hpfs_file_sector(&file, (uint32_t)(location / HPFS_SECTOR_BYTES));
        unsigned skip = location % HPFS_SECTOR_BYTES;
        size_t n = HPFS_SECTOR_BYTES - skip;
        if (n > size - at)
            n = size - at;
        ok = sector && hpfs_read_sectors(d, sector, bytes, 1);
        if (ok)
        {
            memcpy(bytes + skip, (const unsigned char *)data + at, n);
            ok = hpfs_stage_sectors(d, sector, bytes, 1);
        }
        at += n;
    }
    if (ok)
    {
        tb_put_le32(entry->record + HPFS_DIRENT_FILE_SIZE, (uint32_t)length);
        tb_put_le32(entry->record + HPFS_DIRENT_MODIFIED, (uint32_t)time(NULL));
        entry->record[HPFS_DIRENT_ATTRIBUTES] |= HPFS_ATTR_ARCHIVED;
    }
    hpfs_file_free(&file);
    return ok;
}

int tb_hpfs_modify(const char *path, const char *expected, int action, uint64_t offset,
                   const void *data, size_t size, const char *target)
{
    if (target)
    {
        char *a = tb_image_source(path), *b = tb_image_source(target);
        int same = a && b && !strcmp(a, b);
        free(a);
        free(b);
        if (!same || tb_image_protected(target))
            return hpfs_fail(same ? EROFS : EXDEV);
    }
    HpfsVolume disk;
    if (!hpfs_open(path, 1, expected, &disk))
        return 0;
    HpfsDirectory parent = {0}, destination = {0};
    size_t index = 0, to = 0;
    int create = action == TB_FS_CREATE || action == TB_FS_EXCLUSIVE || action == TB_FS_MKDIR;
    int ok = hpfs_load_bitmaps(&disk) && hpfs_locate(&disk, path, &parent, &index, create);
    uint32_t parent_sector = parent.count ? parent.entries[0].parent : 0;
    if (ok && index == SIZE_MAX)
    {
        HpfsEntry entry = {0};
        ok = hpfs_entry_named(&disk, path, NULL, parent_sector, &entry);
        if (ok)
            ok = hpfs_create_fnode(&disk, &entry, action == TB_FS_MKDIR);
        if (ok)
            ok = hpfs_entry_insert(&disk, &parent, entry);
        if (!ok)
            free(entry.record);
        if (ok)
            ok = hpfs_directory_save(&disk, parent_sector, &parent);
    }
    else if (ok)
    {
        HpfsEntry *entry = parent.entries + index;
        if (action == TB_FS_EXCLUSIVE || action == TB_FS_MKDIR)
            ok = hpfs_fail(EEXIST);
        else if (entry->record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_READONLY &&
                 action != TB_FS_READONLY && action != TB_FS_WRITABLE && action != TB_FS_CREATE &&
                 action != TB_FS_ATTRIBUTES && action != TB_FS_TOUCH)
            ok = hpfs_fail(EACCES);
        else if (action == TB_FS_CREATE &&
                 entry->record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_DIRECTORY)
            ok = hpfs_fail(EISDIR);
        else if (action == TB_FS_READONLY || action == TB_FS_WRITABLE)
        {
            entry->record[HPFS_DIRENT_ATTRIBUTES] =
                action == TB_FS_READONLY
                    ? entry->record[HPFS_DIRENT_ATTRIBUTES] | HPFS_ATTR_READONLY
                    : entry->record[HPFS_DIRENT_ATTRIBUTES] & ~HPFS_ATTR_READONLY;
            ok = hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_TOUCH)
        {
            const TbImageStamps *stamps = data;
            if (stamps->modified != INT64_MIN)
                tb_put_le32(entry->record + HPFS_DIRENT_MODIFIED, (uint32_t)stamps->modified);
            if (stamps->accessed != INT64_MIN)
                tb_put_le32(entry->record + HPFS_DIRENT_ACCESSED, (uint32_t)stamps->accessed);
            if (stamps->created != INT64_MIN)
                tb_put_le32(entry->record + HPFS_DIRENT_CREATED, (uint32_t)stamps->created);
            ok = hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_ATTRIBUTES)
        {
            entry->record[HPFS_DIRENT_ATTRIBUTES] =
                (unsigned char)((entry->record[HPFS_DIRENT_ATTRIBUTES] & ~HPFS_ATTR_CHANGEABLE) |
                                (offset & HPFS_ATTR_CHANGEABLE));
            ok = hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_DELETE || action == TB_FS_RMDIR)
        {
            int directory = !!(entry->record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_DIRECTORY);
            if (directory != (action == TB_FS_RMDIR))
                ok = hpfs_fail(directory ? EISDIR : ENOTDIR);
            else
                ok = file_delete(&disk, entry, directory);
            if (ok)
            {
                hpfs_entry_remove(&parent, index);
                ok = hpfs_directory_save(&disk, parent_sector, &parent);
            }
        }
        else if (action == TB_FS_WRITE || action == TB_FS_RESIZE)
        {
            entry->record[HPFS_DIRENT_ATTRIBUTES] |= HPFS_ATTR_ARCHIVED;
            ok = write_file(&disk, entry, action, offset, data, size) &&
                 hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_RENAME)
        {
            ok = target && hpfs_locate(&disk, target, &destination, &to, 1);
            uint32_t target_parent = destination.count ? destination.entries[0].parent : 0;
            uint32_t fnode = tb_le32(entry->record + HPFS_DIRENT_FNODE);
            if (ok && to != SIZE_MAX &&
                tb_le32(destination.entries[to].record + HPFS_DIRENT_FNODE) != fnode)
                ok = hpfs_fail(EEXIST);
            if (ok && entry->record[HPFS_DIRENT_ATTRIBUTES] & HPFS_ATTR_DIRECTORY)
            {
                uint32_t ancestor = target_parent;
                for (unsigned depth = 0; ancestor != disk.root; depth++)
                {
                    unsigned char bytes[HPFS_SECTOR_BYTES];
                    if (depth > 256 || ancestor == fnode ||
                        !hpfs_read_sectors(&disk, ancestor, bytes, 1) ||
                        tb_le32(bytes) != HPFS_FNODE_SIGNATURE)
                    {
                        ok = hpfs_fail(EINVAL);
                        break;
                    }
                    uint32_t next = tb_le32(bytes + HPFS_FNODE_PARENT);
                    if (next == ancestor)
                    {
                        ok = hpfs_fail(EIO);
                        break;
                    }
                    ancestor = next;
                }
                if (target_parent == fnode)
                    ok = hpfs_fail(EINVAL);
            }
            HpfsEntry moved = {0};
            if (ok)
                ok = hpfs_entry_named(&disk, target, entry->record, target_parent, &moved);
            unsigned char bytes[HPFS_SECTOR_BYTES];
            if (ok)
                ok = hpfs_read_sectors(&disk, fnode, bytes, 1);
            if (ok)
            {
                tb_put_le32(bytes + HPFS_FNODE_PARENT, target_parent);
                bytes[HPFS_FNODE_NAME_LENGTH] = moved.record[HPFS_DIRENT_NAME_LENGTH];
                memset(bytes + HPFS_FNODE_NAME, 0, HPFS_FNODE_NAME_BYTES);
                unsigned n = bytes[HPFS_FNODE_NAME_LENGTH] > HPFS_FNODE_NAME_BYTES
                                 ? HPFS_FNODE_NAME_BYTES
                                 : bytes[HPFS_FNODE_NAME_LENGTH];
                memcpy(bytes + HPFS_FNODE_NAME, moved.record + HPFS_DIRENT_NAME, n);
                ok = hpfs_stage_sectors(&disk, fnode, bytes, 1);
            }
            if (ok)
            {
                hpfs_entry_remove(&parent, index);
                if (parent_sector == target_parent)
                    ok = hpfs_entry_insert(&disk, &parent, moved);
                else
                    ok = hpfs_entry_insert(&disk, &destination, moved);
                if (ok)
                {
                    moved.record = NULL;
                    ok = hpfs_directory_save(&disk, parent_sector, &parent);
                }
                if (ok && parent_sector != target_parent)
                    ok = hpfs_directory_save(&disk, target_parent, &destination);
            }
            free(moved.record);
        }
    }
    if (ok)
        ok = hpfs_commit(&disk);
    hpfs_directory_free(&parent);
    hpfs_directory_free(&destination);
    hpfs_close(&disk);
    return ok;
}
