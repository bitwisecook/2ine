/* Public domain. No rights reserved. Wholly AI-authored. */
/* Built-in FAT12/16: BPB, cluster chains, 8.3 and long-name entries, and in-place updates. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/fat/fat12_16.h"
#include "storage/volume/volume_internal.h"
#include "storage/common/file_time.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "formats/endian.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* FAT entry values at or above this end a chain. */
static unsigned fat_chain_end(const VolumeDisk *d)
{
    return d->fat_bits == 12 ? 0xff8u : 0xfff8u;
}

/* The end-of-chain value written for the last cluster of a file. */
static unsigned fat_chain_end_mark(const VolumeDisk *d)
{
    return d->fat_bits == 12 ? 0xfffu : 0xffffu;
}

/* FAT12 packs two 12-bit entries into three bytes; FAT16 entries are 16-bit words. */
static size_t fat_entry_offset(const VolumeDisk *d, uint32_t cluster)
{
    return d->fat_bits == 12 ? cluster + cluster / 2 : cluster * 2;
}

static unsigned fat_value(VolumeDisk *d, uint32_t cluster)
{
    unsigned value = tb_le16(d->fat + fat_entry_offset(d, cluster));
    return d->fat_bits == 12 ? (cluster & 1 ? value >> 4 : value & 0xfff) : value;
}

static void trim_label(char label[12])
{
    label[11] = 0;
    for (int i = 10; i >= 0 && label[i] == ' '; i--)
        label[i] = 0;
}

int fat_identify(VolumeDisk *d)
{
    unsigned char b[FAT_BOOT_SECTOR_BYTES];
    if (!volume_read_at(d, 0, b, sizeof b))
        return 0;
    if (tb_le16(b + FAT_BOOT_SIGNATURE_OFFSET) != FAT_BOOT_SIGNATURE)
        return volume_failure(EINVAL);
    unsigned sector = tb_le16(b + FAT_BPB_SECTOR_BYTES), spc = b[FAT_BPB_SECTORS_PER_CLUSTER],
             reserved = tb_le16(b + FAT_BPB_RESERVED_SECTORS), fats = b[FAT_BPB_FAT_COUNT],
             entries = tb_le16(b + FAT_BPB_ROOT_ENTRIES),
             fat_sectors = tb_le16(b + FAT_BPB_FAT_SECTORS);
    uint32_t total = tb_le16(b + FAT_BPB_TOTAL_SECTORS_16);
    if (!total)
        total = tb_le32(b + FAT_BPB_TOTAL_SECTORS_32);
    /* A zero root-entry or FAT size means FAT32, which FatFs handles. */
    if ((sector != 512 && sector != 1024 && sector != 2048 && sector != 4096) || !spc ||
        (spc & (spc - 1)) || spc > 128 || !reserved || !fats || fats > 2 || !entries ||
        !fat_sectors)
        return volume_failure(ENOTSUP);
    uint64_t root_sectors = ((uint64_t)entries * FAT_ENTRY_BYTES + sector - 1) / sector;
    uint64_t data_sector = reserved + (uint64_t)fats * fat_sectors + root_sectors;
    if (data_sector >= total || (uint64_t)total * sector > d->bytes)
        return volume_failure(EINVAL);
    uint64_t clusters = (total - data_sector) / spc;
    if (!clusters || clusters >= FAT16_CLUSTER_LIMIT)
        return volume_failure(ENOTSUP);
    d->reserved_sectors = reserved;
    d->fat_count = fats;
    d->root_entries = entries;
    d->fat_sectors = fat_sectors;
    d->sectors_per_track = tb_le16(b + FAT_BPB_SECTORS_PER_TRACK);
    d->heads = tb_le16(b + FAT_BPB_HEADS);
    d->hidden_sectors = tb_le32(b + FAT_BPB_HIDDEN_SECTORS);
    d->media = b[FAT_BPB_MEDIA];
    d->bytes = (uint64_t)total * sector;
    d->fat_bits = clusters < FAT12_CLUSTER_LIMIT ? 12 : 16;
    d->clusters = (uint32_t)clusters;
    d->sector_bytes = sector;
    d->cluster_bytes = sector * spc;
    d->data_offset = data_sector * sector;
    d->fat_size = (size_t)fat_sectors * sector;
    if (((clusters + FAT_FIRST_CLUSTER) * d->fat_bits + 7) / 8 > d->fat_size)
        return volume_failure(EINVAL);
    d->fat = malloc(d->fat_size);
    if (!d->fat)
        return 0;
    if (!volume_read_at(d, (uint64_t)reserved * sector, d->fat, d->fat_size))
        return 0;
    d->root = (VolumeNode){.offset = ((uint64_t)reserved + (uint64_t)fats * fat_sectors) * sector,
                           .size = (uint64_t)entries * FAT_ENTRY_BYTES,
                           .modified = INT64_MIN,
                           .directory = 1};
    if (b[FAT_BPB_EXTENDED_SIGNATURE] == FAT_EXTENDED_BOOT_SIGNATURE)
    {
        d->serial = tb_le32(b + FAT_BPB_SERIAL);
        memcpy(d->label, b + FAT_BPB_LABEL, FAT_NAME_BYTES);
    }
    /* A volume-label entry in the root directory overrides the boot sector label. */
    unsigned char record[FAT_ENTRY_BYTES];
    for (unsigned i = 0; i < entries; i++)
    {
        if (!volume_read_at(d, d->root.offset + i * FAT_ENTRY_BYTES, record, sizeof record))
            return 0;
        if (!record[0])
            break;
        if (record[0] != FAT_ENTRY_FREE && record[FAT_ENTRY_ATTRIBUTES] == FAT_ATTRIBUTE_VOLUME)
        {
            memcpy(d->label, record, FAT_NAME_BYTES);
            break;
        }
    }
    trim_label(d->label);
    return 1;
}

uint32_t fat_next_cluster(VolumeDisk *d, uint32_t cluster)
{
    if (!fat_data_cluster(d, cluster))
    {
        errno = EIO;
        return UINT32_MAX;
    }
    uint32_t next = fat_value(d, cluster);
    if (next >= fat_chain_end(d))
        return 0;
    if (!fat_data_cluster(d, next))
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
    return fat_cluster_offset(d, cluster) + at % d->cluster_bytes;
}

/* The checksum of an 8.3 name that its long-name slots carry. */
static unsigned short_name_checksum(const unsigned char *name)
{
    unsigned sum = 0;
    for (unsigned i = 0; i < FAT_NAME_BYTES; i++)
        sum = ((((sum & 1) << 7) | sum >> 1) + name[i]) & 255;
    return sum;
}

/* Byte offsets of the 13 UTF-16 name units within a long-name slot. */
static const unsigned lfn_offsets[FAT_LFN_UNITS] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

int fat_read_entries(VolumeDisk *d, VolumeNode directory, const unsigned char *data, size_t size,
                     VolumeEntryCallback callback, void *context)
{
    int ok = 1;
    uint16_t lfn[FAT_LFN_BUFFER_UNITS];
    unsigned expected = 0, checksum = 0, has_lfn = 0;
    memset(lfn, 0xff, sizeof lfn);
    for (size_t at = 0; at < size;)
    {
        char name[1024];
        VolumeNode node = {0};
        if (size - at < FAT_ENTRY_BYTES)
        {
            ok = volume_failure(EINVAL);
            break;
        }
        const unsigned char *r = data + at;
        at += FAT_ENTRY_BYTES;
        if (!r[0])
            break;
        if (r[0] == FAT_ENTRY_FREE)
        {
            expected = 0;
            has_lfn = 0;
            continue;
        }
        unsigned attributes = r[FAT_ENTRY_ATTRIBUTES];
        if (attributes == FAT_ATTRIBUTE_LONG_NAME)
        {
            /* Slots precede their 8.3 entry in descending order, the last part first. */
            unsigned ordinal = r[0] & FAT_LFN_ORDINAL;
            if ((r[0] & FAT_LFN_LAST) && ordinal && ordinal <= FAT_LFN_SLOTS)
            {
                memset(lfn, 0xff, sizeof lfn);
                expected = ordinal;
                checksum = r[FAT_LFN_CHECKSUM];
                has_lfn = 1;
            }
            if (!expected || ordinal != expected || r[FAT_LFN_CHECKSUM] != checksum ||
                tb_le16(r + FAT_LFN_CLUSTER))
            {
                expected = 0;
                has_lfn = 0;
                continue;
            }
            for (unsigned i = 0; i < FAT_LFN_UNITS; i++)
                lfn[(ordinal - 1) * FAT_LFN_UNITS + i] = (uint16_t)tb_le16(r + lfn_offsets[i]);
            expected--;
            continue;
        }
        if (attributes & FAT_ATTRIBUTE_VOLUME)
        {
            expected = 0;
            has_lfn = 0;
            continue;
        }
        int long_name =
            has_lfn && !expected && short_name_checksum(r) == checksum && lfn[0] != FAT_LFN_PADDING;
        if (long_name && !volume_utf16_name(lfn, FAT_LFN_BUFFER_UNITS, name, sizeof name))
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
                for (unsigned i = 8; i < FAT_NAME_BYTES && r[i] != ' '; i++)
                    name[n++] = (char)r[i];
            }
            name[n] = 0;
        }
        expected = 0;
        has_lfn = 0;
        memset(lfn, 0xff, sizeof lfn);
        node.record = fat_directory_offset(d, directory, at - FAT_ENTRY_BYTES);
        node.attributes = attributes;
        node.modified =
            tb_dos_time(tb_le16(r + FAT_ENTRY_MODIFIED_DATE), tb_le16(r + FAT_ENTRY_MODIFIED_TIME));
        node.accessed = tb_dos_time(tb_le16(r + FAT_ENTRY_ACCESSED_DATE), 0);
        node.created =
            tb_dos_time(tb_le16(r + FAT_ENTRY_CREATED_DATE), tb_le16(r + FAT_ENTRY_CREATED_TIME));
        node.accessed = node.accessed == INT64_MIN ? 0 : node.accessed;
        node.created = node.created == INT64_MIN ? 0 : node.created;
        node.cluster = tb_le16(r + FAT_ENTRY_CLUSTER);
        node.size = tb_le32(r + FAT_ENTRY_SIZE);
        node.directory = !!(attributes & FAT_ATTRIBUTE_DIRECTORY);
        if (node.directory && node.cluster < FAT_FIRST_CLUSTER && strcmp(name, ".") &&
            strcmp(name, ".."))
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
    for (uint32_t i = FAT_FIRST_CLUSTER; i < d->clusters + FAT_FIRST_CLUSTER; i++)
        if (!fat_value(d, i))
            available += d->cluster_bytes;
    return available;
}

/* FAT updates stage a complete filesystem in memory before touching the source.
   The source is exclusively locked through validation and commit. */
static void fat_set(VolumeDisk *d, uint32_t cluster, unsigned value)
{
    size_t at = fat_entry_offset(d, cluster);
    if (d->fat_bits == 12)
    {
        unsigned old = tb_le16(d->fat + at);
        value = cluster & 1 ? (old & 15) | (value << 4) : (old & 0xf000) | value;
    }
    tb_put_le16(d->fat + at, value);
}

static uint32_t allocate_cluster(VolumeDisk *d)
{
    for (uint32_t i = FAT_FIRST_CLUSTER; i < d->clusters + FAT_FIRST_CLUSTER; i++)
        if (!fat_value(d, i))
        {
            fat_set(d, i, fat_chain_end_mark(d));
            memset(d->memory + fat_cluster_offset(d, i), 0, d->cluster_bytes);
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
    tb_put_le16(entry + FAT_ENTRY_MODIFIED_TIME,
                value.tm_hour << 11 | value.tm_min << 5 | value.tm_sec / 2);
    tb_put_le16(entry + FAT_ENTRY_MODIFIED_DATE,
                (year - 1980) << 9 | (value.tm_mon + 1) << 5 | value.tm_mday);
}

static int short_exists(VolumeDisk *d, VolumeNode parent, const unsigned char name[FAT_NAME_BYTES])
{
    size_t size;
    unsigned char *data = volume_directory_data(d, parent, &size);
    if (!data)
        return -1;
    int found = 0;
    for (size_t i = 0; i + FAT_ENTRY_BYTES <= size && data[i]; i += FAT_ENTRY_BYTES)
        if (data[i] != FAT_ENTRY_FREE &&
            data[i + FAT_ENTRY_ATTRIBUTES] != FAT_ATTRIBUTE_LONG_NAME &&
            !memcmp(data + i, name, FAT_NAME_BYTES))
        {
            found = 1;
            break;
        }
    free(data);
    return found;
}

static int name_units(const char *name, uint16_t units[FAT_LFN_BUFFER_UNITS], size_t *length)
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
        if (value < 32 || n == FAT_LFN_NAME_UNITS)
            return volume_failure(ENAMETOOLONG);
        units[n++] = value;
    }
    if (!n || units[n - 1] == ' ' || units[n - 1] == '.')
        return volume_failure(EINVAL);
    *length = n;
    while (n < FAT_LFN_BUFFER_UNITS)
        units[n++] = FAT_LFN_PADDING;
    return 1;
}

static int short_character(unsigned char c)
{
    return c < 128 && (isalnum(c) || strchr("!#$%&'()-@^_`{}~", c));
}

static int make_short(VolumeDisk *d, VolumeNode parent, const char *name,
                      unsigned char alias[FAT_NAME_BYTES], int *lfn)
{
    const char *dot = strrchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name), ext = dot ? strlen(dot + 1) : 0;
    memset(alias, ' ', FAT_NAME_BYTES);
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
        for (size_t at = 0; at + FAT_ENTRY_BYTES <= size; at += FAT_ENTRY_BYTES)
        {
            if (!data[at])
                end = 1;
            run = end || data[at] == FAT_ENTRY_FREE ? run + 1 : 0;
            if (run == required)
            {
                *slot = at / FAT_ENTRY_BYTES + 1 - required;
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
    unsigned char alias[FAT_NAME_BYTES];
    int lfn;
    uint16_t units[FAT_LFN_BUFFER_UNITS];
    size_t n = 0;
    if (!name_units(name, units, &n) || !make_short(d, parent, name, alias, &lfn))
        return 0;
    size_t count = lfn ? (n + FAT_LFN_UNITS - 1) / FAT_LFN_UNITS : 0, slot = 0;
    if (!directory_slots(d, parent, count + 1, &slot))
        return 0;
    unsigned checksum = short_name_checksum(alias);
    if (n < FAT_LFN_BUFFER_UNITS)
        units[n] = 0;
    for (size_t i = 0; i < count; i++)
    {
        uint64_t offset = fat_directory_offset(d, parent, (slot + i) * FAT_ENTRY_BYTES);
        if (offset == UINT64_MAX)
            return volume_failure(EIO);
        unsigned char *entry = d->memory + offset;
        memset(entry, 0, FAT_ENTRY_BYTES);
        unsigned ordinal = (unsigned)(count - i);
        entry[0] = ordinal | (!i ? FAT_LFN_LAST : 0);
        entry[FAT_ENTRY_ATTRIBUTES] = FAT_ATTRIBUTE_LONG_NAME;
        entry[FAT_LFN_CHECKSUM] = checksum;
        for (unsigned j = 0; j < FAT_LFN_UNITS; j++)
            tb_put_le16(entry + lfn_offsets[j], units[(ordinal - 1) * FAT_LFN_UNITS + j]);
    }
    uint64_t offset = fat_directory_offset(d, parent, (slot + count) * FAT_ENTRY_BYTES);
    if (offset == UINT64_MAX)
        return volume_failure(EIO);
    unsigned char *entry = d->memory + offset;
    if (previous)
        memcpy(entry, previous, FAT_ENTRY_BYTES);
    else
    {
        memset(entry, 0, FAT_ENTRY_BYTES);
        entry[FAT_ENTRY_ATTRIBUTES] = FAT_ATTRIBUTE_ARCHIVE;
        record_time(entry);
    }
    memcpy(entry, alias, FAT_NAME_BYTES);
    *result = (VolumeNode){0};
    result->record = offset;
    result->cluster = tb_le16(entry + FAT_ENTRY_CLUSTER);
    result->size = tb_le32(entry + FAT_ENTRY_SIZE);
    result->attributes = entry[FAT_ENTRY_ATTRIBUTES];
    result->directory = !!(entry[FAT_ENTRY_ATTRIBUTES] & FAT_ATTRIBUTE_DIRECTORY);
    return 1;
}

static int erase_record(VolumeDisk *d, VolumeNode parent, VolumeNode node)
{
    size_t size;
    unsigned char *data = volume_directory_data(d, parent, &size);
    if (!data)
        return 0;
    size_t slot = SIZE_MAX;
    for (size_t at = 0; at + FAT_ENTRY_BYTES <= size; at += FAT_ENTRY_BYTES)
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
    d->memory[node.record] = FAT_ENTRY_FREE;
    /* Also free the long-name slots stored just before the entry. */
    while (slot >= FAT_ENTRY_BYTES &&
           data[slot - FAT_ENTRY_BYTES + FAT_ENTRY_ATTRIBUTES] == FAT_ATTRIBUTE_LONG_NAME &&
           data[slot - FAT_ENTRY_BYTES] != FAT_ENTRY_FREE)
    {
        slot -= FAT_ENTRY_BYTES;
        uint64_t offset = fat_directory_offset(d, parent, slot);
        if (offset == UINT64_MAX)
        {
            free(data);
            return volume_failure(EIO);
        }
        d->memory[offset] = FAT_ENTRY_FREE;
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
        if (cluster < FAT_FIRST_CLUSTER || cluster >= d->clusters + FAT_FIRST_CLUSTER)
            return volume_failure(EIO);
        memcpy(out + at, d->memory + fat_cluster_offset(d, cluster), n);
        at += n;
        cluster = fat_next_cluster(d, cluster);
    }
    return 1;
}

static int replace_contents(VolumeDisk *d, VolumeNode node, const void *contents, uint64_t size)
{
    if (node.directory)
        return volume_failure(EISDIR);
    if (node.attributes & FAT_ATTRIBUTE_READONLY)
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
        memcpy(d->memory + fat_cluster_offset(d, cluster), (const unsigned char *)contents + at, n);
    }
    unsigned char *entry = d->memory + node.record;
    tb_put_le16(entry + FAT_ENTRY_CLUSTER, first);
    tb_put_le32(entry + FAT_ENTRY_SIZE, (uint32_t)size);
    entry[FAT_ENTRY_ATTRIBUTES] |= FAT_ATTRIBUTE_ARCHIVE;
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

static int create_file(const char *path, int exclusive)
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

static int remove_entry(const char *path, int directory)
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
    if (ok && node.attributes & FAT_ATTRIBUTE_READONLY)
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

static int make_directory(const char *path)
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
            record[FAT_ENTRY_ATTRIBUTES] = FAT_ATTRIBUTE_DIRECTORY;
            tb_put_le16(record + FAT_ENTRY_CLUSTER, cluster);
            /* A new directory starts with "." for itself and ".." for its parent. */
            unsigned char *dot = disk.memory + fat_cluster_offset(&disk, cluster);
            unsigned char *dotdot = dot + FAT_ENTRY_BYTES;
            memset(dot, ' ', FAT_NAME_BYTES);
            dot[0] = '.';
            dot[FAT_ENTRY_ATTRIBUTES] = FAT_ATTRIBUTE_DIRECTORY;
            tb_put_le16(dot + FAT_ENTRY_CLUSTER, cluster);
            record_time(dot);
            memcpy(dotdot, dot, FAT_ENTRY_BYTES);
            dotdot[1] = '.';
            tb_put_le16(dotdot + FAT_ENTRY_CLUSTER, parent.cluster);
            ok = commit_disk(&disk);
        }
    }
    free(name);
    volume_close(&disk);
    return ok;
}

static int set_readonly(const char *path, int readonly)
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
        record[FAT_ENTRY_ATTRIBUTES] = readonly
                                           ? record[FAT_ENTRY_ATTRIBUTES] | FAT_ATTRIBUTE_READONLY
                                           : record[FAT_ENTRY_ATTRIBUTES] & ~FAT_ATTRIBUTE_READONLY;
        ok = commit_disk(&disk);
    }
    volume_close(&disk);
    return ok;
}

static int set_stamps(const char *path, const char *expected, const TbImageStamps *stamps)
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
        /* A DOS stamp holds the date in its high word; entries store the time word first. */
        unsigned char *record = disk.memory + node.record;
        if (stamps->created != INT64_MIN)
            tb_put_le32(record + FAT_ENTRY_CREATED_TIME, tb_dos_stamp(stamps->created));
        if (stamps->accessed != INT64_MIN)
            tb_put_le16(record + FAT_ENTRY_ACCESSED_DATE,
                        (uint16_t)(tb_dos_stamp(stamps->accessed) >> 16));
        if (stamps->modified != INT64_MIN)
            tb_put_le32(record + FAT_ENTRY_MODIFIED_TIME, tb_dos_stamp(stamps->modified));
        ok = commit_disk(&disk);
    }
    volume_close(&disk);
    return ok;
}

static int set_attributes(const char *path, const char *expected, unsigned attributes)
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
        record[FAT_ENTRY_ATTRIBUTES] =
            (unsigned char)((record[FAT_ENTRY_ATTRIBUTES] & ~(unsigned)FAT_ATTRIBUTES_SETTABLE) |
                            attributes);
        ok = commit_disk(&disk);
    }
    volume_close(&disk);
    return ok;
}

static int move_entry(const char *source, const char *target)
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
    if (ok && node.attributes & FAT_ATTRIBUTE_READONLY)
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
            if (steps >= disk.clusters || ancestor.cluster < FAT_FIRST_CLUSTER ||
                ancestor.cluster >= disk.clusters + FAT_FIRST_CLUSTER)
            {
                ok = volume_failure(EIO);
                break;
            }
            /* The ".." entry, second in each directory, names the parent's cluster. */
            uint64_t offset = fat_cluster_offset(&disk, ancestor.cluster) + FAT_ENTRY_BYTES;
            uint32_t upper = tb_le16(disk.memory + offset + FAT_ENTRY_CLUSTER);
            ancestor.cluster = upper;
        }
    }
    unsigned char original[FAT_ENTRY_BYTES];
    if (ok)
    {
        memcpy(original, disk.memory + node.record, FAT_ENTRY_BYTES);
        ok = new_record(&disk, destination, newname, original, &created) &&
             erase_record(&disk, parent, node);
    }
    if (ok && node.directory)
        tb_put_le16(disk.memory + fat_cluster_offset(&disk, node.cluster) + FAT_ENTRY_BYTES +
                        FAT_ENTRY_CLUSTER,
                    destination.cluster);
    if (ok)
        ok = commit_disk(&disk);
    free(oldname);
    free(newname);
    volume_close(&disk);
    return ok;
}

int fat12_16_modify(const char *path, const char *expected, int action, uint64_t offset,
                    const void *data, size_t size, const char *target)
{
    switch (action)
    {
        case TB_FS_WRITE:
            return change_contents(path, expected, offset, data, size, 0, 0);
        case TB_FS_RESIZE:
            return change_contents(path, expected, 0, NULL, 0, offset, 1);
        case TB_FS_CREATE:
        case TB_FS_EXCLUSIVE:
            return create_file(path, action == TB_FS_EXCLUSIVE);
        case TB_FS_DELETE:
        case TB_FS_RMDIR:
            return remove_entry(path, action == TB_FS_RMDIR);
        case TB_FS_MKDIR:
            return make_directory(path);
        case TB_FS_READONLY:
        case TB_FS_WRITABLE:
            return set_readonly(path, action == TB_FS_READONLY);
        case TB_FS_TOUCH:
            return set_stamps(path, expected, data);
        case TB_FS_ATTRIBUTES:
            return set_attributes(path, expected, (unsigned)offset);
        case TB_FS_RENAME:
            return move_entry(path, target);
    }
    return volume_failure(EINVAL);
}
