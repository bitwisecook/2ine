/* Public domain. No rights reserved. Wholly AI-authored. */
/* Built-in FAT12/16: BPB, cluster chains, 8.3 and long-name entries, and in-place updates. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/fat/fat12_16.h"
#include "storage/common/file_time.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "formats/endian.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int fat_identify(VolumeDisk *d)
{
    unsigned char b[512];
    if (!volume_read_at(d, 0, b, 512))
        return 0;
    if (b[510] != 0x55 || b[511] != 0xaa)
        return volume_failure(EINVAL);
    unsigned sector = tb_le16(b + 11), spc = b[13], reserved = tb_le16(b + 14), fats = b[16],
             entries = tb_le16(b + 17), fat_sectors = tb_le16(b + 22);
    uint32_t total = tb_le16(b + 19);
    if (!total)
        total = tb_le32(b + 32);
    if ((sector != 512 && sector != 1024 && sector != 2048 && sector != 4096) || !spc ||
        (spc & (spc - 1)) || spc > 128 || !reserved || !fats || fats > 2 || !entries ||
        !fat_sectors)
        return volume_failure(ENOTSUP);
    uint64_t root_sectors = ((uint64_t)entries * 32 + sector - 1) / sector;
    uint64_t data_sector = reserved + (uint64_t)fats * fat_sectors + root_sectors;
    if (data_sector >= total || (uint64_t)total * sector > d->bytes)
        return volume_failure(EINVAL);
    uint64_t clusters = (total - data_sector) / spc;
    if (!clusters || clusters >= 65525)
        return volume_failure(ENOTSUP);
    d->reserved_sectors = reserved;
    d->fat_count = fats;
    d->root_entries = entries;
    d->fat_sectors = fat_sectors;
    d->sectors_per_track = tb_le16(b + 24);
    d->heads = tb_le16(b + 26);
    d->hidden_sectors = tb_le32(b + 28);
    d->media = b[21];
    d->bytes = (uint64_t)total * sector;
    d->fat_bits = clusters < 4085 ? 12 : 16;
    d->clusters = (uint32_t)clusters;
    d->sector_bytes = sector;
    d->cluster_bytes = sector * spc;
    d->data_offset = data_sector * sector;
    d->fat_size = (size_t)fat_sectors * sector;
    if (((clusters + 2) * d->fat_bits + 7) / 8 > d->fat_size)
        return volume_failure(EINVAL);
    d->fat = malloc(d->fat_size);
    if (!d->fat)
        return 0;
    if (!volume_read_at(d, (uint64_t)reserved * sector, d->fat, d->fat_size))
        return 0;
    d->root = (VolumeNode){.offset = ((uint64_t)reserved + (uint64_t)fats * fat_sectors) * sector,
                           .size = (uint64_t)entries * 32,
                           .modified = INT64_MIN,
                           .directory = 1};
    if (b[38] == 0x29)
    {
        d->serial = tb_le32(b + 39);
        memcpy(d->label, b + 43, 11);
    }
    unsigned char record[32];
    for (unsigned i = 0; i < entries; i++)
    {
        if (!volume_read_at(d, d->root.offset + i * 32u, record, sizeof record))
            return 0;
        if (!record[0])
            break;
        if (record[0] != 0xe5 && record[11] == 8)
        {
            memcpy(d->label, record, 11);
            break;
        }
    }
    d->label[11] = 0;
    for (int i = 10; i >= 0 && d->label[i] == ' '; i--)
        d->label[i] = 0;

    return 1;
}

uint32_t fat_next_cluster(VolumeDisk *d, uint32_t cluster)
{
    if (cluster < 2 || cluster >= d->clusters + 2 ||
        cluster >= (d->fat_bits == 12 ? 0xff0u : 0xfff0u))
    {
        errno = EIO;
        return UINT32_MAX;
    }
    size_t at = d->fat_bits == 12 ? cluster + cluster / 2 : cluster * 2;
    uint32_t next = tb_le16(d->fat + at);
    if (d->fat_bits == 12)
        next = cluster & 1 ? next >> 4 : next & 0xfff;
    if (next >= (d->fat_bits == 12 ? 0xff8u : 0xfff8u))
        return 0;
    if (next < 2 || next >= d->clusters + 2 || next >= (d->fat_bits == 12 ? 0xff0u : 0xfff0u))
    {
        errno = EIO;
        return UINT32_MAX;
    }
    return next;
}

uint64_t fat_directory_offset(VolumeDisk *d, VolumeNode directory, size_t at)
{
    if (!directory.cluster)
        return directory.offset + at;
    uint32_t cluster = directory.cluster;
    for (size_t i = 0; i < at / d->cluster_bytes; i++)
    {
        cluster = fat_next_cluster(d, cluster);
        if (!cluster || cluster == UINT32_MAX || i >= d->clusters)
            return UINT64_MAX;
    }
    return d->data_offset + (uint64_t)(cluster - 2) * d->cluster_bytes + at % d->cluster_bytes;
}

int fat_read_entries(VolumeDisk *d, VolumeNode directory, const unsigned char *data, size_t size,
                     VolumeEntryCallback callback, void *context)
{
    int ok = 1;
    uint16_t lfn[260];
    unsigned expected = 0, checksum = 0, has_lfn = 0;
    memset(lfn, 0xff, sizeof lfn);
    for (size_t at = 0; at < size;)
    {
        char name[1024];
        VolumeNode node = {0};
        if (size - at < 32)
        {
            ok = volume_failure(EINVAL);
            break;
        }
        const unsigned char *r = data + at;
        at += 32;
        if (!r[0])
            break;
        if (r[0] == 0xe5)
        {
            expected = 0;
            has_lfn = 0;
            continue;
        }
        if (r[11] == 0x0f)
        {
            unsigned ordinal = r[0] & 31;
            if ((r[0] & 0x40) && ordinal && ordinal <= 20)
            {
                memset(lfn, 0xff, sizeof lfn);
                expected = ordinal;
                checksum = r[13];
                has_lfn = 1;
            }
            if (!expected || ordinal != expected || r[13] != checksum || tb_le16(r + 26))
            {
                expected = 0;
                has_lfn = 0;
                continue;
            }
            static const unsigned offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (unsigned i = 0; i < 13; i++)
                lfn[(ordinal - 1) * 13 + i] = (uint16_t)tb_le16(r + offsets[i]);
            expected--;
            continue;
        }
        if (r[11] & 8)
        {
            expected = 0;
            has_lfn = 0;
            continue;
        }
        unsigned sum = 0;
        for (unsigned i = 0; i < 11; i++)
            sum = (((sum & 1) << 7) | (sum >> 1)) + r[i], sum &= 255;
        int long_name = has_lfn && !expected && sum == checksum && lfn[0] != 0xffff;
        if (long_name && !volume_utf16_name(lfn, 260, name, sizeof name))
        {
            ok = 0;
            break;
        }
        if (!long_name)
        {
            size_t n = 0;
            for (unsigned i = 0; i < 8 && r[i] != ' '; i++)
                name[n++] = (char)r[i];
            if (r[8] != ' ')
            {
                name[n++] = '.';
                for (unsigned i = 8; i < 11 && r[i] != ' '; i++)
                    name[n++] = (char)r[i];
            }
            name[n] = 0;
        }
        expected = 0;
        has_lfn = 0;
        memset(lfn, 0xff, sizeof lfn);
        node.record = fat_directory_offset(d, directory, at - 32);
        node.attributes = r[11];
        node.modified = tb_dos_time(tb_le16(r + 24), tb_le16(r + 22));
        node.accessed = tb_dos_time(tb_le16(r + 18), 0);
        node.created = tb_dos_time(tb_le16(r + 16), tb_le16(r + 14));
        node.accessed = node.accessed == INT64_MIN ? 0 : node.accessed;
        node.created = node.created == INT64_MIN ? 0 : node.created;
        node.cluster = tb_le16(r + 26);
        node.size = tb_le32(r + 28);
        node.directory = !!(r[11] & 16);
        if (node.directory && node.cluster < 2 && strcmp(name, ".") && strcmp(name, ".."))
        {
            ok = volume_failure(EIO);
            break;
        }
        if (!volume_safe_name(name))
            continue;
        int result = callback(context, name, &node);
        if (result <= 0)
        {
            ok = result == 0;
            break;
        }
    }
    return ok;
}

uint64_t fat_available_bytes(VolumeDisk *d)
{
    uint64_t available = 0;
    for (uint32_t i = 2; i < d->clusters + 2; i++)
    {
        size_t at = d->fat_bits == 12 ? i + i / 2 : i * 2;
        unsigned value = tb_le16(d->fat + at);
        if (d->fat_bits == 12)
            value = i & 1 ? value >> 4 : value & 0xfff;
        if (!value)
            available += d->cluster_bytes;
    }
    return available;
}

/* FAT updates stage a complete filesystem in memory before touching the source.
   The source is exclusively locked through validation and commit. */
static unsigned fat_value(VolumeDisk *d, uint32_t cluster)
{
    size_t at = d->fat_bits == 12 ? cluster + cluster / 2 : cluster * 2;
    unsigned value = tb_le16(d->fat + at);
    return d->fat_bits == 12 ? (cluster & 1 ? value >> 4 : value & 0xfff) : value;
}

static void fat_set(VolumeDisk *d, uint32_t cluster, unsigned value)
{
    size_t at = d->fat_bits == 12 ? cluster + cluster / 2 : cluster * 2;
    if (d->fat_bits == 12)
    {
        unsigned old = tb_le16(d->fat + at);
        value = cluster & 1 ? (old & 15) | (value << 4) : (old & 0xf000) | value;
    }
    tb_put_le16(d->fat + at, value);
}

static uint64_t cluster_offset(VolumeDisk *d, uint32_t cluster)
{
    return d->data_offset + (uint64_t)(cluster - 2) * d->cluster_bytes;
}

static uint32_t allocate_cluster(VolumeDisk *d)
{
    for (uint32_t i = 2; i < d->clusters + 2; i++)
        if (!fat_value(d, i))
        {
            fat_set(d, i, d->fat_bits == 12 ? 0xfff : 0xffff);
            memset(d->memory + cluster_offset(d, i), 0, d->cluster_bytes);
            return i;
        }
    errno = ENOSPC;
    return 0;
}

static int release_chain(VolumeDisk *d, uint32_t cluster)
{
    for (uint32_t steps = 0; cluster; steps++)
    {
        if (steps >= d->clusters)
            return volume_failure(EIO);
        uint32_t next = fat_next_cluster(d, cluster);
        if (next == UINT32_MAX)
            return 0;
        fat_set(d, cluster, 0);
        cluster = next;
    }
    return 1;
}

static int stage_disk(const char *path, const char *expected, VolumeDisk *d, const char **member)
{
    if (expected)
    {
        char *current = tb_image_source(path);
        int same = current && !strcmp(current, expected);
        free(current);
        if (!same)
            return volume_failure(ENXIO);
    }
    if (!volume_open(path, d, member, 1))
        return 0;
    if (d->bytes > SIZE_MAX)
    {
        volume_close(d);
        return volume_failure(ENOMEM);
    }
    unsigned char *memory = malloc((size_t)d->bytes);
    if (!memory)
    {
        volume_close(d);
        return 0;
    }
    if (!volume_read_at(d, 0, memory, (size_t)d->bytes))
    {
        free(memory);
        volume_close(d);
        return 0;
    }
    d->memory = memory;
    return 1;
}

static int commit_disk(VolumeDisk *d)
{
    for (unsigned i = 0; i < d->fat_count; i++)
        memcpy(d->memory + (uint64_t)d->reserved_sectors * d->sector_bytes + i * d->fat_size,
               d->fat, d->fat_size);
    if (fseeko(d->file, 0, SEEK_SET) ||
        fwrite(d->memory, 1, (size_t)d->bytes, d->file) != d->bytes || fflush(d->file))
        return volume_failure(errno ? errno : EIO);
    return tb_host_sync(d->file) ? 1 : volume_failure(errno);
}

static int parent_node(VolumeDisk *d, const char *member, VolumeNode *parent, char **name)
{
    char *copy = strdup(member);
    if (!copy)
        return 0;
    size_t n = strlen(copy);
    while (n && copy[n - 1] == '/')
        copy[--n] = 0;
    char *slash = strrchr(copy, '/');
    *name = strdup(slash ? slash + 1 : copy);
    if (!*name)
    {
        free(copy);
        return 0;
    }
    if (slash)
        *slash = 0;
    else
        *copy = 0;
    int ok =
        volume_safe_name(*name) && !strpbrk(*name, "<>\"|?*") && volume_lookup(d, copy, parent);
    free(copy);
    if (!ok)
    {
        free(*name);
        *name = NULL;
        if (!errno)
            errno = EINVAL;
        return 0;
    }
    return parent->directory ? 1 : volume_failure(ENOTDIR);
}

static void record_time(unsigned char *entry)
{
    struct tm value;
    tb_host_localtime((int64_t)time(NULL), &value);
    unsigned year = value.tm_year + 1900;
    if (year < 1980)
        year = 1980;
    if (year > 2107)
        year = 2107;
    tb_put_le16(entry + 22, value.tm_hour << 11 | value.tm_min << 5 | value.tm_sec / 2);
    tb_put_le16(entry + 24, (year - 1980) << 9 | (value.tm_mon + 1) << 5 | value.tm_mday);
}

static int short_exists(VolumeDisk *d, VolumeNode parent, const unsigned char name[11])
{
    size_t size;
    unsigned char *data = volume_directory_data(d, parent, &size);
    if (!data)
        return -1;
    int found = 0;
    for (size_t i = 0; i + 32 <= size && data[i]; i += 32)
        if (data[i] != 0xe5 && data[i + 11] != 15 && !memcmp(data + i, name, 11))
        {
            found = 1;
            break;
        }
    free(data);
    return found;
}

static int name_units(const char *name, uint16_t units[260], size_t *length)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)name;
    while (*p)
    {
        unsigned value = *p++;
        if (value >= 0x80)
        {
            unsigned more = value >= 0xe0 && value < 0xf0   ? 2
                            : value >= 0xc2 && value < 0xe0 ? 1
                                                            : 0;
            if (!more)
                return volume_failure(ENOTSUP);
            value &= more == 2 ? 15 : 31;
            for (unsigned i = 0; i < more; i++)
            {
                if ((*p & 0xc0) != 0x80)
                    return volume_failure(EINVAL);
                value = value << 6 | (*p++ & 63);
            }
            if ((more == 2 && value < 0x800) || (value >= 0xd800 && value <= 0xdfff))
                return volume_failure(EINVAL);
        }
        if (value < 32 || n == 255)
            return volume_failure(ENAMETOOLONG);
        units[n++] = value;
    }
    if (!n || units[n - 1] == ' ' || units[n - 1] == '.')
        return volume_failure(EINVAL);
    *length = n;
    while (n < 260)
        units[n++] = 0xffff;
    return 1;
}

static int short_character(unsigned char c)
{
    return c < 128 && (isalnum(c) || strchr("!#$%&'()-@^_`{}~", c));
}

static int make_short(VolumeDisk *d, VolumeNode parent, const char *name, unsigned char alias[11],
                      int *lfn)
{
    const char *dot = strrchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name), ext = dot ? strlen(dot + 1) : 0;
    memset(alias, ' ', 11);
    *lfn = base > 8 || ext > 3 || !base;
    for (size_t i = 0; i < base; i++)
    {
        unsigned char c = name[i];
        if (!short_character(c) || islower(c))
            *lfn = 1;
        if (i < 8)
            alias[i] = short_character(c) ? toupper(c) : '_';
    }
    for (size_t i = 0; i < ext && i < 3; i++)
    {
        unsigned char c = dot[i + 1];
        if (!short_character(c) || islower(c))
            *lfn = 1;
        alias[8 + i] = short_character(c) ? toupper(c) : '_';
    }
    if (!*lfn)
    {
        int found = short_exists(d, parent, alias);
        return found < 0 ? 0 : found ? volume_failure(EEXIST) : 1;
    }
    unsigned char stem[8];
    memcpy(stem, alias, 8);
    for (unsigned i = 1; i < 1000000; i++)
    {
        char number[9];
        snprintf(number, sizeof number, "~%u", i);
        size_t digits = strlen(number), prefix = 8 - digits;
        while (prefix && stem[prefix - 1] == ' ')
            prefix--;
        memset(alias, ' ', 8);
        memcpy(alias, stem, prefix);
        memcpy(alias + prefix, number, digits);
        int exists = short_exists(d, parent, alias);
        if (exists < 0)
            return 0;
        if (!exists)
            return 1;
    }
    return volume_failure(ENOSPC);
}

static int directory_slots(VolumeDisk *d, VolumeNode parent, size_t required, size_t *slot)
{
    for (;;)
    {
        size_t size;
        unsigned char *data = volume_directory_data(d, parent, &size);
        if (!data)
            return 0;
        size_t run = 0;
        int end = 0;
        for (size_t at = 0; at + 32 <= size; at += 32)
        {
            if (!data[at])
                end = 1;
            run = end || data[at] == 0xe5 ? run + 1 : 0;
            if (run == required)
            {
                *slot = at / 32 + 1 - required;
                free(data);
                return 1;
            }
        }
        free(data);
        if (!parent.cluster)
            return volume_failure(ENOSPC);
        uint32_t last = parent.cluster, next;
        for (uint32_t steps = 0;; steps++)
        {
            if (steps >= d->clusters)
                return volume_failure(EIO);
            next = fat_next_cluster(d, last);
            if (next == UINT32_MAX)
                return 0;
            if (!next)
                break;
            last = next;
        }
        uint32_t added = allocate_cluster(d);
        if (!added)
            return 0;
        fat_set(d, last, added);
    }
}

static int new_record(VolumeDisk *d, VolumeNode parent, const char *name,
                      const unsigned char *previous, VolumeNode *result)
{
    unsigned char alias[11];
    int lfn;
    uint16_t units[260];
    size_t n;
    if (!name_units(name, units, &n) || !make_short(d, parent, name, alias, &lfn))
        return 0;
    size_t count = lfn ? (n + 12) / 13 : 0, slot;
    if (!directory_slots(d, parent, count + 1, &slot))
        return 0;
    unsigned checksum = 0;
    for (unsigned i = 0; i < 11; i++)
        checksum = ((((checksum & 1) << 7) | checksum >> 1) + alias[i]) & 255;
    if (n < 260)
        units[n] = 0;
    static const unsigned offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for (size_t i = 0; i < count; i++)
    {
        uint64_t offset = fat_directory_offset(d, parent, (slot + i) * 32);
        if (offset == UINT64_MAX)
            return volume_failure(EIO);
        unsigned char *entry = d->memory + offset;
        memset(entry, 0, 32);
        unsigned ordinal = (unsigned)(count - i);
        entry[0] = ordinal | (!i ? 0x40 : 0);
        entry[11] = 15;
        entry[13] = checksum;
        for (unsigned j = 0; j < 13; j++)
            tb_put_le16(entry + offsets[j], units[(ordinal - 1) * 13 + j]);
    }
    uint64_t offset = fat_directory_offset(d, parent, (slot + count) * 32);
    if (offset == UINT64_MAX)
        return volume_failure(EIO);
    unsigned char *entry = d->memory + offset;
    if (previous)
        memcpy(entry, previous, 32);
    else
    {
        memset(entry, 0, 32);
        entry[11] = 32;
        record_time(entry);
    }
    memcpy(entry, alias, 11);
    *result = (VolumeNode){0};
    result->record = offset;
    result->cluster = tb_le16(entry + 26);
    result->size = tb_le32(entry + 28);
    result->attributes = entry[11];
    result->directory = !!(entry[11] & 16);
    return 1;
}

static int erase_record(VolumeDisk *d, VolumeNode parent, VolumeNode node)
{
    size_t size;
    unsigned char *data = volume_directory_data(d, parent, &size);
    if (!data)
        return 0;
    size_t slot = SIZE_MAX;
    for (size_t at = 0; at + 32 <= size; at += 32)
        if (fat_directory_offset(d, parent, at) == node.record)
        {
            slot = at;
            break;
        }
    if (slot == SIZE_MAX)
    {
        free(data);
        return volume_failure(EIO);
    }
    d->memory[node.record] = 0xe5;
    while (slot >= 32 && data[slot - 32 + 11] == 15 && data[slot - 32] != 0xe5)
    {
        slot -= 32;
        uint64_t offset = fat_directory_offset(d, parent, slot);
        if (offset == UINT64_MAX)
        {
            free(data);
            return volume_failure(EIO);
        }
        d->memory[offset] = 0xe5;
    }
    free(data);
    return 1;
}

static int file_contents(VolumeDisk *d, VolumeNode node, unsigned char *out, size_t size)
{
    uint32_t cluster = node.cluster;
    for (size_t at = 0, steps = 0; at < size; steps++)
    {
        if (!cluster || cluster == UINT32_MAX || steps >= d->clusters)
            return volume_failure(EIO);
        size_t n = size - at < d->cluster_bytes ? size - at : d->cluster_bytes;
        if (cluster < 2 || cluster >= d->clusters + 2)
            return volume_failure(EIO);
        memcpy(out + at, d->memory + cluster_offset(d, cluster), n);
        at += n;
        cluster = fat_next_cluster(d, cluster);
    }
    return 1;
}

static int replace_contents(VolumeDisk *d, VolumeNode node, const void *contents, uint64_t size)
{
    if (node.directory)
        return volume_failure(EISDIR);
    if (node.attributes & 1)
        return volume_failure(EACCES);
    if (size > UINT32_MAX || size > d->bytes)
        return volume_failure(ENOSPC);
    if (!release_chain(d, node.cluster))
        return 0;
    uint32_t first = 0, last = 0;
    for (uint64_t at = 0; at < size; at += d->cluster_bytes)
    {
        uint32_t cluster = allocate_cluster(d);
        if (!cluster)
            return 0;
        if (!first)
            first = cluster;
        if (last)
            fat_set(d, last, cluster);
        last = cluster;
        size_t n = size - at < d->cluster_bytes ? (size_t)(size - at) : d->cluster_bytes;
        memcpy(d->memory + cluster_offset(d, cluster), (const unsigned char *)contents + at, n);
    }
    unsigned char *entry = d->memory + node.record;
    tb_put_le16(entry + 26, first);
    tb_put_le32(entry + 28, (uint32_t)size);
    entry[11] |= 32;
    record_time(entry);
    return 1;
}

static int change_contents(const char *path, const char *expected, uint64_t offset,
                           const void *bytes, size_t length, uint64_t resized, int resize)
{
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(path, expected, &disk, &member))
        return 0;
    VolumeNode node;
    unsigned char *contents = NULL;
    int ok = volume_lookup(&disk, member, &node);
    uint64_t size = resize ? resized : offset <= UINT64_MAX - length ? offset + length : UINT64_MAX;
    if (ok && !resize && size < node.size)
        size = node.size;
    if (ok && (size > UINT32_MAX || size > disk.bytes))
        ok = volume_failure(ENOSPC);
    if (ok)
    {
        contents = calloc(size ? (size_t)size : 1, 1);
        if (!contents)
            ok = 0;
    }
    if (ok)
        ok = file_contents(&disk, node, contents, (size_t)(node.size < size ? node.size : size));
    if (ok && length)
        memcpy(contents + offset, bytes, length);
    if (ok)
        ok = replace_contents(&disk, node, contents, size) && commit_disk(&disk);
    free(contents);
    volume_close(&disk);
    return ok;
}

int fat12_16_write(const char *path, const char *expected, uint64_t offset, const void *bytes,
                   size_t size)
{
    return change_contents(path, expected, offset, bytes, size, 0, 0);
}

int fat12_16_resize(const char *path, const char *expected, uint64_t size)
{
    return change_contents(path, expected, 0, NULL, 0, size, 1);
}

int fat12_16_create_file(const char *path, int exclusive)
{
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    VolumeNode node, parent;
    char *name = NULL;
    int ok = volume_lookup(&disk, member, &node);
    if (ok)
        ok = exclusive ? volume_failure(EEXIST) : node.directory ? volume_failure(EISDIR) : 1;
    else if (errno == ENOENT)
        ok = parent_node(&disk, member, &parent, &name) &&
             new_record(&disk, parent, name, NULL, &node) && commit_disk(&disk);
    free(name);
    volume_close(&disk);
    return ok;
}

static int count_entry(void *value, const char *name, const VolumeNode *node)
{
    (void)name;
    (void)node;
    (*(unsigned *)value)++;
    return 1;
}

int fat12_16_remove(const char *path, int directory)
{
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    VolumeNode node, parent;
    char *name = NULL;
    unsigned children = 0;
    int ok = volume_lookup(&disk, member, &node) && parent_node(&disk, member, &parent, &name);
    if (ok && node.directory != !!directory)
        ok = volume_failure(node.directory ? EISDIR : ENOTDIR);
    if (ok && node.attributes & 1)
        ok = volume_failure(EACCES);
    if (ok && directory)
        ok = volume_entries(&disk, node, count_entry, &children) &&
             (!children || volume_failure(ENOTEMPTY));
    if (ok)
        ok = release_chain(&disk, node.cluster) && erase_record(&disk, parent, node) &&
             commit_disk(&disk);
    free(name);
    volume_close(&disk);
    return ok;
}

int fat12_16_mkdir(const char *path)
{
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    VolumeNode node, parent;
    char *name = NULL;
    int ok = volume_lookup(&disk, member, &node);
    if (ok)
        ok = volume_failure(EEXIST);
    else if (errno == ENOENT)
    {
        ok = parent_node(&disk, member, &parent, &name) &&
             new_record(&disk, parent, name, NULL, &node);
        uint32_t cluster = ok ? allocate_cluster(&disk) : 0;
        if (!cluster)
            ok = 0;
        if (ok)
        {
            unsigned char *record = disk.memory + node.record;
            record[11] = 16;
            tb_put_le16(record + 26, cluster);
            unsigned char *dot = disk.memory + cluster_offset(&disk, cluster);
            memset(dot, ' ', 11);
            dot[0] = '.';
            dot[11] = 16;
            tb_put_le16(dot + 26, cluster);
            record_time(dot);
            memcpy(dot + 32, dot, 32);
            dot[33] = '.';
            tb_put_le16(dot + 32 + 26, parent.cluster);
            ok = commit_disk(&disk);
        }
    }
    free(name);
    volume_close(&disk);
    return ok;
}

int fat12_16_readonly(const char *path, int readonly)
{
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    VolumeNode node;
    int ok = volume_lookup(&disk, member, &node);
    if (ok && !node.record)
        ok = volume_failure(EACCES);
    if (ok)
    {
        unsigned char *record = disk.memory + node.record;
        record[11] = readonly ? record[11] | 1 : record[11] & ~1u;
        ok = commit_disk(&disk);
    }
    volume_close(&disk);
    return ok;
}

int fat12_16_stamps(const char *path, const char *expected, const TbImageStamps *stamps)
{
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(path, expected, &disk, &member))
        return 0;
    VolumeNode node;
    int ok = volume_lookup(&disk, member, &node);
    if (ok && !node.record)
        ok = volume_failure(EACCES);
    if (ok)
    {
        unsigned char *record = disk.memory + node.record;
        if (stamps->created != INT64_MIN)
            tb_put_le32(record + 14, tb_dos_stamp(stamps->created));
        if (stamps->accessed != INT64_MIN)
        {
            uint32_t stamp = tb_dos_stamp(stamps->accessed);
            record[18] = (unsigned char)(stamp >> 16);
            record[19] = (unsigned char)(stamp >> 24);
        }
        if (stamps->modified != INT64_MIN)
            tb_put_le32(record + 22, tb_dos_stamp(stamps->modified));
        ok = commit_disk(&disk);
    }
    volume_close(&disk);
    return ok;
}

int fat12_16_attributes(const char *path, const char *expected, unsigned attributes)
{
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(path, expected, &disk, &member))
        return 0;
    VolumeNode node;
    int ok = volume_lookup(&disk, member, &node);
    if (ok && !node.record)
        ok = volume_failure(EACCES);
    if (ok)
    {
        unsigned char *record = disk.memory + node.record;
        record[11] = (unsigned char)((record[11] & ~0x26u) | attributes);
        ok = commit_disk(&disk);
    }
    volume_close(&disk);
    return ok;
}

int fat12_16_move(const char *source, const char *target)
{
    char *a = tb_image_source(source), *b = tb_image_source(target);
    int same = a && b && !strcmp(a, b);
    free(a);
    free(b);
    if (!same)
        return volume_failure(EXDEV);
    VolumeDisk disk;
    const char *member;
    if (!stage_disk(source, NULL, &disk, &member))
        return 0;
    VolumeNode node, parent, destination, found, created;
    char *oldname = NULL, *newname = NULL;
    const char *tail = tb_image_member(target);
    int ok = tail && volume_lookup(&disk, member, &node) &&
             parent_node(&disk, member, &parent, &oldname) &&
             parent_node(&disk, tail, &destination, &newname);
    if (ok && volume_lookup(&disk, tail, &found))
        ok = volume_failure(EEXIST);
    if (ok && node.attributes & 1)
        ok = volume_failure(EACCES);
    if (ok && node.directory)
    {
        VolumeNode ancestor = destination;
        for (uint32_t steps = 0; ancestor.cluster; steps++)
        {
            if (ancestor.cluster == node.cluster)
            {
                ok = volume_failure(EINVAL);
                break;
            }
            if (steps >= disk.clusters || ancestor.cluster < 2 ||
                ancestor.cluster >= disk.clusters + 2)
            {
                ok = volume_failure(EIO);
                break;
            }
            uint64_t offset = cluster_offset(&disk, ancestor.cluster);
            uint32_t upper = tb_le16(disk.memory + offset + 32 + 26);
            ancestor.cluster = upper;
        }
    }
    unsigned char original[32];
    if (ok)
    {
        memcpy(original, disk.memory + node.record, 32);
        ok = new_record(&disk, destination, newname, original, &created) &&
             erase_record(&disk, parent, node);
    }
    if (ok && node.directory)
        tb_put_le16(disk.memory + cluster_offset(&disk, node.cluster) + 32 + 26,
                    destination.cluster);
    if (ok)
        ok = commit_disk(&disk);
    free(oldname);
    free(newname);
    volume_close(&disk);
    return ok;
}
