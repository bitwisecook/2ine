/* Public domain. No rights reserved. Wholly AI-authored. */
/* Built-in volume access: opens ISO 9660 and FAT12/16 images, reads directories and
   copies members. FAT12/16 updates are in filesystems/fat/fat12_16.c. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/volume/volume_disk.h"
#include "storage/volume/volume_internal.h"
#include "storage/filesystems/fat/fat12_16.h"
#include "storage/filesystems/iso9660/iso9660.h"
#include "storage/common/host_stdio.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define DIRECTORY_LIMIT (64u * 1024u * 1024u)

int volume_failure(int error)
{
    errno = error;
    return 0;
}

int volume_read_at(VolumeDisk *d, uint64_t offset, void *data, size_t n)
{
    if (offset > d->bytes || n > d->bytes - offset || offset > INT64_MAX)
        return volume_failure(EIO);
    if (d->stream)
        return tb_stream_read_at(d->stream, offset, data, n);
    if (d->memory)
    {
        memcpy(data, d->memory + offset, n);
        return 1;
    }
    if (fseeko(d->file, (int64_t)offset, SEEK_SET) || fread(data, 1, n, d->file) != n)
        return volume_failure(EIO);
    return 1;
}

void volume_close(VolumeDisk *d)
{
    int error = errno;
    if (d->file)
        fclose(d->file);
    free(d->fat);
    free(d->memory);
    errno = error;
}

int volume_identify(VolumeDisk *d)
{
    return iso_present(d) ? iso_identify(d) : fat_identify(d);
}

int volume_open(const char *path, VolumeDisk *d, const char **member, int write)
{
    memset(d, 0, sizeof *d);
    int kind = 0, readonly = 0;
    char *source = tb_image_resolve_source(path, &kind, &readonly);
    if (!source)
        return 0;
    if (write && readonly)
    {
        free(source);
        return volume_failure(EROFS);
    }
    d->file = tb_image_open_source(source, write);
    free(source);
    if (!d->file)
        return 0;
    if (!tb_image_lock_source(d->file, write) || fseeko(d->file, 0, SEEK_END))
    {
        volume_close(d);
        return 0;
    }
    int64_t size = ftello(d->file);
    if (size < 0)
    {
        volume_close(d);
        return 0;
    }
    d->bytes = (uint64_t)size;
    if (!volume_identify(d))
    {
        volume_close(d);
        return 0;
    }
    d->readonly = readonly || !d->fat_bits;
    if (write && d->readonly)
    {
        volume_close(d);
        return volume_failure(EROFS);
    }
    *member = tb_image_member(path);
    return 1;
}

unsigned char *volume_directory_data(VolumeDisk *d, VolumeNode node, size_t *size)
{
    uint64_t length = node.size;
    if (d->fat_bits && node.cluster)
    {
        length = 0;
        uint32_t cluster = node.cluster;
        for (uint32_t steps = 0; cluster; steps++)
        {
            if (steps >= d->clusters || length + d->cluster_bytes > DIRECTORY_LIMIT)
            {
                errno = EIO;
                return NULL;
            }
            length += d->cluster_bytes;
            cluster = fat_next_cluster(d, cluster);
            if (cluster == UINT32_MAX)
                return NULL;
        }
    }
    if (length > DIRECTORY_LIMIT)
    {
        errno = EFBIG;
        return NULL;
    }
    unsigned char *data = malloc(length ? (size_t)length : 1);
    if (!data)
        return NULL;
    if (!node.cluster)
    {
        if (!volume_read_at(d, node.offset, data, (size_t)length))
        {
            free(data);
            return NULL;
        }
    }
    else
    {
        uint32_t cluster = node.cluster;
        for (size_t at = 0; at < length; at += d->cluster_bytes)
        {
            if (!volume_read_at(d, fat_cluster_offset(d, cluster), data + at, d->cluster_bytes))
            {
                free(data);
                return NULL;
            }
            cluster = fat_next_cluster(d, cluster);
            if (cluster == UINT32_MAX)
            {
                free(data);
                return NULL;
            }
        }
    }
    *size = (size_t)length;
    return data;
}

int volume_utf16_name(const uint16_t *text, size_t n, char *out, size_t capacity)
{
    size_t at = 0;
    /* Stops at a terminator or at FAT long-name padding. */
    for (size_t i = 0; i < n && text[i] && text[i] != 0xffff; i++)
    {
        unsigned v = text[i];
        if (v >= 0xd800 && v <= 0xdfff)
            return volume_failure(ENOTSUP);
        size_t bytes = v < 0x80 ? 1 : v < 0x800 ? 2 : 3;
        if (at + bytes >= capacity)
            return volume_failure(ENAMETOOLONG);
        if (bytes == 1)
            out[at++] = (char)v;
        else if (bytes == 2)
        {
            out[at++] = (char)(0xc0 | v >> 6);
            out[at++] = (char)(0x80 | (v & 63));
        }
        else
        {
            out[at++] = (char)(0xe0 | v >> 12);
            out[at++] = (char)(0x80 | ((v >> 6) & 63));
            out[at++] = (char)(0x80 | (v & 63));
        }
    }
    out[at] = 0;
    return 1;
}

int volume_safe_name(const char *name)
{
    return *name && strcmp(name, ".") && strcmp(name, "..") && !strpbrk(name, "/\\:");
}

int volume_read_entries(VolumeDisk *d, VolumeNode directory, VolumeEntryCallback callback,
                        void *context)
{
    if (!directory.directory)
        return volume_failure(ENOTDIR);
    size_t size;
    unsigned char *data = volume_directory_data(d, directory, &size);
    if (!data)
        return 0;
    int ok = d->fat_bits ? fat_read_entries(d, directory, data, size, callback, context)
                         : iso_read_entries(d, data, size, callback, context);
    free(data);
    return ok;
}

typedef struct Find
{
    const char *name;
    VolumeNode node;
    int found;
} Find;

static int same_name(const char *a, const char *b)
{
    while (*a && *b)
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++))
            return 0;
    return !*a && !*b;
}

static int find_entry(void *context, const char *name, const VolumeNode *node)
{
    Find *find = context;
    if (!same_name(find->name, name))
        return 1;
    find->node = *node;
    find->found = 1;
    return 0;
}

int volume_lookup(VolumeDisk *d, const char *member, VolumeNode *node)
{
    char *copy = strdup(member);
    if (!copy)
        return 0;
    /* Normalize before traversal; never let '..' escape the virtual root. */
    char *parts[256];
    size_t count = 0;
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
            if (!count)
            {
                free(copy);
                return volume_failure(EACCES);
            }
            count--;
            continue;
        }
        if (count == 256 || strpbrk(part, "\\:"))
        {
            free(copy);
            return volume_failure(EINVAL);
        }
        parts[count++] = part;
    }
    *node = d->root;
    for (size_t i = 0; i < count; i++)
    {
        Find find = {parts[i], {0}, 0};
        if (!volume_entries(d, *node, find_entry, &find))
        {
            free(copy);
            return 0;
        }
        if (!find.found)
        {
            free(copy);
            return volume_failure(ENOENT);
        }
        *node = find.node;
    }
    free(copy);
    return 1;
}

static int root_member(const char *path)
{
    const char *member = tb_image_member(path);
    if (!member)
        return 0;
    while (*member)
    {
        if (*member == '/')
            member++;
        else if (*member == '.' && (!member[1] || member[1] == '/'))
            member++;
        else
            return 0;
    }
    return 1;
}

int volume_disk_info(const char *path, TbImageInfo *info)
{
    VolumeDisk disk;
    const char *member;
    if (!volume_open(path, &disk, &member, 0))
    {
        /* An empty device still occupies a drive letter and has a root. */
        if (tb_image_device(path) && errno == ENXIO && root_member(path))
        {
            int kind = tb_image_device_kind(path);
            if (errno != ENXIO)
                return 0;
            *info = (TbImageInfo){.modified = INT64_MIN};
            info->directory = 1;
            info->fat = kind == TB_DEVICE_FLOPPY;
            strcpy(info->filesystem, kind == TB_DEVICE_FLOPPY ? "FAT"
                                     : kind == TB_DEVICE_CD   ? "CDFS"
                                                              : "UNKNOWN");
            info->sector_bytes = info->cluster_bytes =
                kind == TB_DEVICE_FLOPPY ? FAT_BOOT_SECTOR_BYTES : ISO_SECTOR_BYTES;
            return 1;
        }
        return 0;
    }
    VolumeNode node;
    int ok = volume_lookup(&disk, member, &node);
    if (ok)
    {
        *info = (TbImageInfo){.modified = node.modified,
                              .accessed = node.accessed,
                              .created = node.created,
                              .attributes = node.attributes & FAT_ATTRIBUTES_SETTABLE};
        info->size = node.size;
        info->readonly = disk.readonly || !!(node.attributes & FAT_ATTRIBUTE_READONLY);
        if (disk.fat_bits && !disk.readonly)
            info->available = fat_available_bytes(&disk);
        info->capacity = disk.bytes;
        info->directory = node.directory;
        info->fat = !!disk.fat_bits;
        strcpy(info->filesystem, disk.fat_bits ? "FAT" : "CDFS");
        info->serial = disk.serial;
        info->reserved_sectors = disk.reserved_sectors;
        info->fat_count = disk.fat_count;
        info->root_entries = disk.root_entries;
        info->fat_sectors = disk.fat_sectors;
        info->sectors_per_track = disk.sectors_per_track;
        info->heads = disk.heads;
        info->hidden_sectors = disk.hidden_sectors;
        info->media = disk.media;
        info->sector_bytes = disk.fat_bits ? disk.sector_bytes : ISO_SECTOR_BYTES;
        info->cluster_bytes = disk.fat_bits ? disk.cluster_bytes : ISO_SECTOR_BYTES;
        memcpy(info->label, disk.label, sizeof info->label);
    }
    volume_close(&disk);
    return ok;
}

typedef struct Names
{
    char **items;
    size_t count;
} Names;

static int add_name(void *context, const char *name, const VolumeNode *node)
{
    (void)node;
    Names *list = context;
    char *copy = strdup(name);
    if (!copy)
        return -1;
    char **items = realloc(list->items, (list->count + 1) * sizeof *items);
    if (!items)
    {
        free(copy);
        return -1;
    }
    list->items = items;
    list->items[list->count++] = copy;
    return 1;
}

char **volume_disk_names(const char *path, size_t *count)
{
    VolumeDisk disk;
    const char *member;
    *count = (size_t)-1;
    if (!volume_open(path, &disk, &member, 0))
        return NULL;
    VolumeNode node;
    Names names = {0};
    int ok = volume_lookup(&disk, member, &node) && volume_entries(&disk, node, add_name, &names);
    volume_close(&disk);
    if (!ok)
    {
        for (size_t i = 0; i < names.count; i++)
            free(names.items[i]);
        free(names.items);
        return NULL;
    }
    *count = names.count;
    return names.items;
}

FILE *volume_snapshot(VolumeDisk *disk, const char *member)
{
    VolumeNode node;
    FILE *output = NULL;
    unsigned char *buffer = NULL, *visited = NULL;

    enum
    {
        BUFFER_SIZE = 65536
    };

    if (!volume_lookup(disk, member, &node))
        goto done;
    if (node.directory)
    {
        errno = EISDIR;
        goto done;
    }
    output = tmpfile();
    if (!output)
        goto done;
    uint64_t remaining = node.size, offset = node.offset;
    uint32_t cluster = node.cluster, steps = 0, within = 0;
    buffer = malloc(BUFFER_SIZE);
    if (!buffer)
        goto failed;
    visited = disk->fat_bits ? calloc((disk->clusters + 9u) / 8u, 1) : NULL;
    if (disk->fat_bits && !visited)
        goto failed;
    while (remaining)
    {
        size_t chunk = remaining > BUFFER_SIZE ? BUFFER_SIZE : (size_t)remaining;
        if (disk->fat_bits)
        {
            if (!fat_data_cluster(disk, cluster) || steps >= disk->clusters)
            {
                errno = EIO;
                goto failed;
            }
            if (!within)
            {
                if (visited[cluster / 8] & (1u << (cluster % 8)))
                {
                    errno = EIO;
                    goto failed;
                }
                visited[cluster / 8] |= (unsigned char)(1u << (cluster % 8));
            }
            offset = fat_cluster_offset(disk, cluster) + within;
            if (chunk > disk->cluster_bytes - within)
                chunk = disk->cluster_bytes - within;
        }
        if (!volume_read_at(disk, offset, buffer, chunk) ||
            fwrite(buffer, 1, chunk, output) != chunk)
            goto failed;
        remaining -= chunk;
        offset += chunk;
        if (disk->fat_bits)
        {
            within += (uint32_t)chunk;
            if (within == disk->cluster_bytes && remaining)
            {
                cluster = fat_next_cluster(disk, cluster);
                within = 0;
                steps++;
                if (!cluster || cluster == UINT32_MAX)
                {
                    errno = EIO;
                    goto failed;
                }
            }
        }
    }
    if (fflush(output) || fseeko(output, 0, SEEK_SET))
        goto failed;
    goto done;
failed:
{
    int error = errno;
    fclose(output);
    output = NULL;
    errno = error;
}

done:
    free(visited);
    free(buffer);
    return output;
}

FILE *volume_disk_fopen(const char *path)
{
    VolumeDisk disk;
    const char *member;
    if (!volume_open(path, &disk, &member, 0))
        return NULL;
    FILE *output = volume_snapshot(&disk, member);
    volume_close(&disk);
    return output;
}
