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
            info->directory = !!(list.entries[index].record[3] & 16);
            info->size = tb_le32(list.entries[index].record + 12);
            info->readonly = !!(list.entries[index].record[3] & 1);
            info->attributes = list.entries[index].record[3] & 0x26;
            info->modified = tb_le32(list.entries[index].record + 8);
            info->accessed = tb_le32(list.entries[index].record + 16);
            info->created = tb_le32(list.entries[index].record + 20);
        }
    }
    else
        info->directory = 1;
    if (ok)
    {
        info->readonly |= tb_image_protected(path) != 0;
        strcpy(info->filesystem, "HPFS");
        info->capacity = (uint64_t)disk.sectors * 512;
        info->sector_bytes = info->cluster_bytes = 512;
        info->serial = tb_le32(disk.boot + 39);
        memcpy(info->label, disk.boot + 43, 11);
        for (int i = 10; i >= 0 && info->label[i] == ' '; i--)
            info->label[i] = 0;
        if (!info->readonly && name && !*name)
        {
            ok = hpfs_load_bitmaps(&disk);
            for (uint32_t i = 0; ok && i < disk.sectors; i++)
                if (hpfs_sector_is_free(&disk, i))
                    info->available += 512;
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
            fnode = tb_le32(parent.entries[index].record + 4);
    }
    if (ok)
        ok = hpfs_directory_read(&disk, fnode, &list);
    for (size_t i = 0; ok && i < list.count; i++)
    {
        if (list.entries[i].record[2] & 1)
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
    int ok = hpfs_locate(&disk, path, &parent, &index, 0) &&
             hpfs_file_load(&disk, tb_le32(parent.entries[index].record + 4), &file);
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
    unsigned char bytes[512] = {0};
    tb_put_le32(bytes, HPFS_FNODE_SIGNATURE);
    bytes[12] = entry->record[30];
    unsigned n = bytes[12] > 15 ? 15 : bytes[12];
    memcpy(bytes + 13, entry->record + 31, n);
    tb_put_le32(bytes + 28, entry->parent);
    bytes[55] = directory;
    bytes[60] = 8;
    tb_put_le16(bytes + 62, 8);
    tb_put_le16(bytes + 184, 196);
    tb_put_le32(entry->record + 4, sector);
    if (directory)
        entry->record[3] = 16 | (entry->record[3] & 64);
    if (!hpfs_stage_sectors(d, sector, bytes, 1))
        return 0;
    if (directory)
    {
        unsigned char *dot = calloc(36, 1);
        if (!dot)
            return 0;
        tb_put_le16(dot, 36);
        dot[2] = 1;
        dot[3] = 16;
        tb_put_le32(dot + 4, sector);
        tb_put_le32(dot + 8, tb_le32(entry->record + 8));
        tb_put_le32(dot + 16, tb_le32(entry->record + 16));
        tb_put_le32(dot + 20, tb_le32(entry->record + 20));
        dot[30] = 2;
        dot[31] = dot[32] = 1;
        HpfsEntry special = {dot, 36, sector};
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
    uint32_t size = tb_le32(fnode + offset), sector = tb_le32(fnode + offset + 4);
    if (!size)
        return 1;
    if (!fnode[offset + 10])
        return hpfs_mark_sectors(d, sector, (uint32_t)(((uint64_t)size + 511u) / 512u), 1);
    HpfsFile tree = {0};
    unsigned char node[512];
    int ok = hpfs_read_sectors(d, sector, node, 1) && tb_le32(node) == HPFS_ANODE_SIGNATURE &&
             hpfs_add_unique_node(&tree.nodes, &tree.node_count, sector) &&
             hpfs_extent_tree(d, &tree, node + 12, node[12] & 128 ? 60 : 40, 0);
    for (size_t i = 0; ok && i < tree.count; i++)
        ok = hpfs_mark_sectors(d, tree.extents[i].sector, tree.extents[i].count, 1);
    for (size_t i = 0; ok && i < tree.node_count; i++)
        ok = hpfs_mark_sectors(d, tree.nodes[i], 1, 1);
    hpfs_file_free(&tree);
    return ok;
}

static int file_delete(HpfsVolume *d, HpfsEntry *entry, int directory)
{
    uint32_t sector = tb_le32(entry->record + 4);
    unsigned char fnode[512];
    if (!hpfs_read_sectors(d, sector, fnode, 1) || tb_le32(fnode) != HPFS_FNODE_SIGNATURE)
        return hpfs_fail(EIO);
    if (directory)
    {
        HpfsDirectory list;
        if (!hpfs_directory_read(d, sector, &list))
            return 0;
        int ok = 1;
        for (size_t i = 0; i < list.count; i++)
            if (!(list.entries[i].record[2] & 1))
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
    return hpfs_ea_values_free(d, fnode) && hpfs_release_external(d, fnode, 32) &&
           hpfs_release_external(d, fnode, 44) && hpfs_mark_sectors(d, sector, 1, 1);
}

static int write_file(HpfsVolume *d, HpfsEntry *entry, int action, uint64_t offset,
                      const void *data, size_t size)
{
    HpfsFile file;
    if (!hpfs_file_load(d, tb_le32(entry->record + 4), &file))
        return 0;
    uint64_t length = action == TB_FS_RESIZE        ? offset
                      : offset <= UINT64_MAX - size ? offset + size
                                                    : UINT64_MAX;
    if (action != TB_FS_RESIZE && length < tb_le32(file.data + 160))
        length = tb_le32(file.data + 160);
    int ok = hpfs_file_resize(d, &file, length);
    for (size_t at = 0; ok && action == TB_FS_WRITE && at < size;)
    {
        unsigned char bytes[512];
        uint64_t location = offset + at;
        uint32_t sector = hpfs_file_sector(&file, (uint32_t)(location / 512));
        unsigned skip = location % 512;
        size_t n = 512 - skip;
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
        tb_put_le32(entry->record + 12, (uint32_t)length);
        tb_put_le32(entry->record + 8, (uint32_t)time(NULL));
        entry->record[3] |= 32;
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
        else if (entry->record[3] & 1 && action != TB_FS_READONLY && action != TB_FS_WRITABLE &&
                 action != TB_FS_CREATE && action != TB_FS_ATTRIBUTES && action != TB_FS_TOUCH)
            ok = hpfs_fail(EACCES);
        else if (action == TB_FS_CREATE && entry->record[3] & 16)
            ok = hpfs_fail(EISDIR);
        else if (action == TB_FS_READONLY || action == TB_FS_WRITABLE)
        {
            entry->record[3] =
                action == TB_FS_READONLY ? entry->record[3] | 1 : entry->record[3] & ~1u;
            ok = hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_TOUCH)
        {
            const TbImageStamps *stamps = data;
            if (stamps->modified != INT64_MIN)
                tb_put_le32(entry->record + 8, (uint32_t)stamps->modified);
            if (stamps->accessed != INT64_MIN)
                tb_put_le32(entry->record + 16, (uint32_t)stamps->accessed);
            if (stamps->created != INT64_MIN)
                tb_put_le32(entry->record + 20, (uint32_t)stamps->created);
            ok = hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_ATTRIBUTES)
        {
            entry->record[3] = (unsigned char)((entry->record[3] & ~0x26u) | (offset & 0x26));
            ok = hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_DELETE || action == TB_FS_RMDIR)
        {
            int directory = !!(entry->record[3] & 16);
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
            entry->record[3] |= 0x20;
            ok = write_file(&disk, entry, action, offset, data, size) &&
                 hpfs_directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_RENAME)
        {
            ok = target && hpfs_locate(&disk, target, &destination, &to, 1);
            uint32_t target_parent = destination.count ? destination.entries[0].parent : 0;
            uint32_t fnode = tb_le32(entry->record + 4);
            if (ok && to != SIZE_MAX && tb_le32(destination.entries[to].record + 4) != fnode)
                ok = hpfs_fail(EEXIST);
            if (ok && entry->record[3] & 16)
            {
                uint32_t ancestor = target_parent;
                for (unsigned depth = 0; ancestor != disk.root; depth++)
                {
                    unsigned char bytes[512];
                    if (depth > 256 || ancestor == fnode ||
                        !hpfs_read_sectors(&disk, ancestor, bytes, 1) ||
                        tb_le32(bytes) != HPFS_FNODE_SIGNATURE)
                    {
                        ok = hpfs_fail(EINVAL);
                        break;
                    }
                    uint32_t next = tb_le32(bytes + 28);
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
            unsigned char bytes[512];
            if (ok)
                ok = hpfs_read_sectors(&disk, fnode, bytes, 1);
            if (ok)
            {
                tb_put_le32(bytes + 28, target_parent);
                bytes[12] = moved.record[30];
                memset(bytes + 13, 0, 15);
                unsigned n = bytes[12] > 15 ? 15 : bytes[12];
                memcpy(bytes + 13, moved.record + 31, n);
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
