/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS v2 image access. All sector numbers are relative to the selected volume. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "image_internal.h"
#include "vendor/fatfs/ff.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define fseeko _fseeki64
#define ftello _ftelli64
#define strdup _strdup
#else
#include <unistd.h>
#endif
#define H_SUPER 0xf995e849u
#define H_SPARE 0xf9911849u
#define H_FNODE 0xf7e40aaeu
#define H_ANODE 0x37e40aaeu
#define H_DNODE 0x77e40aaeu
#define H_LIMIT 1048576u
static unsigned h16(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}
static uint32_t h32(const unsigned char *p)
{
    return h16(p) | (uint32_t)h16(p + 2) << 16;
}
static void p16(unsigned char *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}
static void p32(unsigned char *p, uint32_t v)
{
    p16(p, v);
    p16(p + 2, v >> 16);
}
static int herr(int error)
{
    errno = error;
    return 0;
}
typedef struct HPatch
{
    uint32_t sector;
    unsigned char data[512];
    struct HPatch *next;
} HPatch;
typedef struct HCodepage
{
    unsigned codepage;
    unsigned char map[256];
} HCodepage;
typedef struct HDisk
{
    FILE *file;
    uint64_t bytes;
    uint32_t base, sectors, root, bands, bitmap_list;
    uint32_t dir_start, dir_count, dir_map, cursor;
    unsigned codepage;
    HCodepage *codepages;
    unsigned codepage_count;
    unsigned char case_map[256], boot[512], super[512], spare[512];
    unsigned char *bitmaps, dir_bitmap[2048];
    uint32_t *bitmap_locations;
    int writable;
    HPatch *changes[1024];
} HDisk;
typedef struct HEntry
{
    unsigned char *record;
    size_t size;
    uint32_t parent;
} HEntry;
typedef struct HDirectory
{
    HEntry *entries;
    size_t count;
    uint32_t *nodes;
    size_t node_count;
} HDirectory;
typedef struct HExtent
{
    uint32_t logical, count, sector;
} HExtent;
typedef struct HFile
{
    uint32_t sector;
    unsigned char data[512];
    HExtent *extents;
    size_t count;
    uint32_t *nodes;
    size_t node_count;
} HFile;
static int raw_read(FILE *file, uint64_t offset, void *bytes, size_t size)
{
    return !fseeko(file, (int64_t)offset, SEEK_SET) && fread(bytes, 1, size, file) == size
               ? 1
               : herr(EIO);
}
static int hread(HDisk *d, uint32_t sector, void *bytes, unsigned count)
{
    if (sector >= d->sectors || count > d->sectors - sector)
        return herr(EIO);
    for (unsigned i = 0; i < count; i++)
    {
        HPatch *patch = d->changes[(sector + i) % 1024];
        while (patch && patch->sector != sector + i)
            patch = patch->next;
        if (patch)
            memcpy((unsigned char *)bytes + i * 512, patch->data, 512);
        else if (!raw_read(d->file, ((uint64_t)d->base + sector + i) * 512,
                           (unsigned char *)bytes + i * 512, 512))
            return 0;
    }
    return 1;
}
static int hpatch(HDisk *d, uint32_t sector, const void *bytes, unsigned count)
{
    if (sector >= d->sectors || count > d->sectors - sector)
        return herr(EIO);
    for (unsigned i = 0; i < count; i++)
    {
        unsigned bucket = (sector + i) % 1024;
        HPatch *patch = d->changes[bucket];
        while (patch && patch->sector != sector + i)
            patch = patch->next;
        if (!patch)
        {
            patch = malloc(sizeof *patch);
            if (!patch)
                return 0;
            patch->sector = sector + i;
            patch->next = d->changes[bucket];
            d->changes[bucket] = patch;
        }
        memcpy(patch->data, (const unsigned char *)bytes + i * 512, 512);
    }
    return 1;
}
static int unique_node(uint32_t **nodes, size_t *count, uint32_t value)
{
    if (*count >= H_LIMIT)
        return herr(EFBIG);
    for (size_t i = 0; i < *count; i++)
        if ((*nodes)[i] == value)
            return herr(EIO);
    uint32_t *next = realloc(*nodes, (*count + 1) * sizeof **nodes);
    if (!next)
        return 0;
    *nodes = next;
    next[(*count)++] = value;
    return 1;
}
static void hclose(HDisk *d)
{
    int saved = errno;
    if (d->file)
        fclose(d->file);
    for (unsigned i = 0; i < 1024; i++)
    {
        HPatch *p = d->changes[i];
        while (p)
        {
            HPatch *next = p->next;
            free(p);
            p = next;
        }
    }
    free(d->codepages);
    free(d->bitmaps);
    free(d->bitmap_locations);
    errno = saved;
}
static int partition(FILE *file, uint64_t bytes, uint32_t *base, unsigned char *super)
{
    unsigned char boot[512];
    *base = 0;
    if (bytes >= 18 * 512 && raw_read(file, 16 * 512, super, 512) && h32(super) == H_SUPER)
        return 1;
    if (!raw_read(file, 0, boot, 512) || boot[510] != 0x55 || boot[511] != 0xaa)
        return herr(EINVAL);
    for (unsigned i = 0; i < 4; i++)
    {
        const unsigned char *p = boot + 446 + i * 16;
        uint32_t start = h32(p + 8), count = h32(p + 12);
        if (p[4] != 7 || count < 18 || ((uint64_t)start + count) * 512 > bytes)
            continue;
        if (raw_read(file, ((uint64_t)start + 16) * 512, super, 512) && h32(super) == H_SUPER)
        {
            *base = start;
            return 1;
        }
    }
    return herr(EINVAL);
}
int tb_hpfs_volume(const char *path)
{
    char *source = tb_image_source(path);
    if (!source)
        return 0;
    FILE *file = tb_image_open_source(source, 0);
    free(source);
    if (!file)
        return 0;
    unsigned char super[512];
    uint32_t base;
    int ok = !fseeko(file, 0, SEEK_END);
    int64_t bytes = ok ? ftello(file) : -1;
    ok = bytes >= 0 && partition(file, (uint64_t)bytes, &base, super);
    fclose(file);
    return ok;
}
static int hopen(const char *path, int write, const char *expected, HDisk *d)
{
    memset(d, 0, sizeof *d);
    int protected = tb_image_protected(path);
    if (protected < 0)
        return 0;
    if (write && protected)
        return herr(EROFS);
    char *source = tb_image_source(path);
    if (!source)
        return 0;
    if (expected && strcmp(expected, source))
    {
        free(source);
        return herr(ENXIO);
    }
    d->file = tb_image_open_source(source, write);
    free(source);
    if (!d->file)
        return 0;
    if (!tb_image_lock_source(d->file, write) || fseeko(d->file, 0, SEEK_END))
        goto fail;
    int64_t bytes = ftello(d->file);
    if (bytes < 0 || !partition(d->file, (uint64_t)bytes, &d->base, d->super))
        goto fail;
    d->bytes = bytes;
    d->sectors = h32(d->super + 16);
    d->root = h32(d->super + 12);
    if (h32(d->super + 4) != 0xfa53e9c5u || d->super[8] != 2 || d->sectors < 2048 ||
        ((uint64_t)d->base + d->sectors) * 512 > d->bytes)
    {
        errno = EINVAL;
        goto fail;
    }
    if (!hread(d, 0, d->boot, 1) || !hread(d, 17, d->spare, 1))
        goto fail;
    if (h16(d->boot + 11) != 512 || h32(d->spare) != H_SPARE || h32(d->spare + 4) != 0xfa5229c5u)
    {
        errno = EINVAL;
        goto fail;
    }
    /* Hotfix remapping and HPFS386 allocation/ACL variants require their own backend. */
    if (h32(d->spare + 16) || (write && (d->super[9] > 2 || d->spare[8] & 0x1f)))
    {
        errno = ENOTSUP;
        goto fail;
    }
    unsigned char root_fnode[512];
    if (d->root < 18 || !hread(d, d->root, root_fnode, 1) || h32(root_fnode) != H_FNODE ||
        !(root_fnode[55] & 1))
    {
        errno = EINVAL;
        goto fail;
    }
    d->writable = write;
    d->bands = (d->sectors + 16383) / 16384;
    d->bitmap_list = h32(d->super + 24);
    d->dir_start = h32(d->super + 52);
    d->dir_count = h32(d->super + 48);
    d->dir_map = h32(d->super + 60);
    if (d->dir_count > 65536 || d->dir_start % 4 || d->dir_count % 4 ||
        d->dir_start >= d->sectors || d->dir_count > d->sectors - d->dir_start)
    {
        errno = EINVAL;
        goto fail;
    }
    d->codepage = 437;
    for (unsigned i = 0; i < 256; i++)
        d->case_map[i] = i < 128 ? toupper(i) : i;
    uint32_t cpinfo = h32(d->spare + 32);
    if (cpinfo)
    {
        d->codepage_count = h32(d->spare + 36);
        if (!d->codepage_count || d->codepage_count > 128)
        {
            errno = EINVAL;
            goto fail;
        }
        d->codepages = calloc(d->codepage_count, sizeof *d->codepages);
        if (!d->codepages)
            goto fail;
        unsigned loaded = 0;
        for (unsigned page = 0; cpinfo; page++)
        {
            unsigned char info[512];
            if (page >= 128 || !hread(d, cpinfo, info, 1) || h32(info) != 0x494521f7u ||
                h32(info + 8) != loaded)
            {
                errno = EINVAL;
                goto fail;
            }
            unsigned count = h32(info + 4);
            if (!count || count > 31 || count > d->codepage_count - loaded)
            {
                errno = EINVAL;
                goto fail;
            }
            for (unsigned i = 0; i < count; i++)
            {
                const unsigned char *entry = info + 16 + i * 16;
                unsigned index = loaded + i, codepage = h16(entry + 2);
                unsigned char data[512];
                if (h16(entry + 12) != index || !hread(d, h32(entry + 8), data, 1) ||
                    h32(data) != 0x894521f7u)
                {
                    errno = EINVAL;
                    goto fail;
                }
                unsigned first = h16(data + 6), entries = h16(data + 4);
                if (entries > 3 || index < first || index - first >= entries)
                {
                    errno = EINVAL;
                    goto fail;
                }
                unsigned offset = h16(data + 20 + (index - first) * 2);
                if (offset > 512 - 136 || h16(data + offset + 2) != codepage)
                {
                    errno = EINVAL;
                    goto fail;
                }
                if (codepage >= 900 || h16(data + offset + 4) || !ff_oem2uni(128, codepage))
                {
                    errno = ENOTSUP;
                    goto fail;
                }
                HCodepage *cp = d->codepages + index;
                cp->codepage = codepage;
                for (unsigned j = 0; j < 128; j++)
                    cp->map[j] = toupper(j);
                memcpy(cp->map + 128, data + offset + 6, 128);
            }
            loaded += count;
            cpinfo = h32(info + 12);
        }
        if (loaded != d->codepage_count)
        {
            errno = EINVAL;
            goto fail;
        }
        d->codepage = d->codepages[0].codepage;
        memcpy(d->case_map, d->codepages[0].map, 256);
    }
    return 1;
fail:
    hclose(d);
    d->file = NULL;
    return 0;
}
static int bitmap_load(HDisk *d)
{
    if (d->bitmaps)
        return 1;
    d->bitmaps = malloc((size_t)d->bands * 2048);
    d->bitmap_locations = calloc(d->bands, sizeof *d->bitmap_locations);
    if (!d->bitmaps || !d->bitmap_locations)
        return 0;
    unsigned char list[512];
    uint32_t current = UINT32_MAX;
    for (uint32_t i = 0; i < d->bands; i++)
    {
        uint32_t sector = d->bitmap_list + i / 128;
        if (sector != current)
        {
            if (!hread(d, sector, list, 1))
                return 0;
            current = sector;
        }
        d->bitmap_locations[i] = h32(list + (i % 128) * 4);
        if (d->bitmap_locations[i] < 18 || d->bitmap_locations[i] % 4 ||
            !hread(d, d->bitmap_locations[i], d->bitmaps + (size_t)i * 2048, 4))
            return herr(EIO);
    }
    return hread(d, d->dir_map, d->dir_bitmap, 4);
}
static int free_sector(HDisk *d, uint32_t sector)
{
    return sector < d->sectors && !!(d->bitmaps[sector / 8] & (1u << (sector % 8)));
}
static int mark_range(HDisk *d, uint32_t sector, uint32_t count, int free)
{
    if (!sector || sector >= d->sectors || count > d->sectors - sector)
        return herr(EIO);
    for (uint32_t i = sector; i < sector + count; i++)
    {
        if (free)
            d->bitmaps[i / 8] |= 1u << (i % 8);
        else
            d->bitmaps[i / 8] &= ~(1u << (i % 8));
    }
    return 1;
}
static uint32_t allocate(HDisk *d, unsigned count, unsigned alignment)
{
    for (uint32_t i = 18; i < d->sectors && count <= d->sectors - i;)
    {
        if (i % alignment)
        {
            i += alignment - i % alignment;
            continue;
        }
        unsigned n = 0;
        while (n < count && free_sector(d, i + n))
            n++;
        if (n == count)
        {
            mark_range(d, i, count, 0);
            return i;
        }
        i += n + 1;
    }
    errno = ENOSPC;
    return 0;
}
static uint32_t allocate_dir(HDisk *d)
{
    for (uint32_t i = 0; i < d->dir_count / 4; i++)
        if (d->dir_bitmap[i / 8] & (1u << (i % 8)))
        {
            d->dir_bitmap[i / 8] &= ~(1u << (i % 8));
            return d->dir_start + i * 4;
        }
    return allocate(d, 4, 4);
}
static int free_dir(HDisk *d, uint32_t sector)
{
    if (sector >= d->dir_start && sector < d->dir_start + d->dir_count)
    {
        uint32_t bit = (sector - d->dir_start) / 4;
        d->dir_bitmap[bit / 8] |= 1u << (bit % 8);
        return 1;
    }
    return mark_range(d, sector, 4, 1);
}
static uint32_t hsum(const unsigned char *bytes, size_t n)
{
    uint32_t value = 0;
    while (n--)
    {
        value += *bytes++;
        value = value << 7 | value >> 25;
    }
    return value;
}
static int sync_source(FILE *file)
{
    if (fflush(file))
        return 0;
#ifdef _WIN32
    return !_commit(_fileno(file));
#else
    return !fsync(fileno(file));
#endif
}
static int commit(HDisk *d)
{
    if (!bitmap_load(d))
        return 0;
    for (uint32_t i = 0; i < d->bands; i++)
        if (!hpatch(d, d->bitmap_locations[i], d->bitmaps + (size_t)i * 2048, 4))
            return 0;
    if (!hpatch(d, d->dir_map, d->dir_bitmap, 4))
        return 0;
    p32(d->spare + 40, hsum(d->super, 512));
    p32(d->spare + 44, 0);
    p32(d->spare + 44, hsum(d->spare, 512));
    if (!hpatch(d, 17, d->spare, 1))
        return 0;
    /* A failed write leaves the HPFS dirty bit set for the original checker. */
    unsigned char dirty[512];
    memcpy(dirty, d->spare, 512);
    dirty[8] |= 1;
    if (fseeko(d->file, ((uint64_t)d->base + 17) * 512, SEEK_SET) ||
        fwrite(dirty, 1, 512, d->file) != 512 || !sync_source(d->file))
        return herr(EIO);
    for (unsigned i = 0; i < 1024; i++)
        for (HPatch *p = d->changes[i]; p; p = p->next)
        {
            if (p->sector == 17)
                continue;
            if (fseeko(d->file, ((uint64_t)d->base + p->sector) * 512, SEEK_SET) ||
                fwrite(p->data, 1, 512, d->file) != 512)
                return herr(EIO);
        }
    if (!sync_source(d->file))
        return herr(EIO);
    if (fseeko(d->file, ((uint64_t)d->base + 17) * 512, SEEK_SET) ||
        fwrite(d->spare, 1, 512, d->file) != 512 || fflush(d->file))
        return herr(EIO);
#ifdef _WIN32
    return !_commit(_fileno(d->file));
#else
    return !fsync(fileno(d->file));
#endif
}
static void directory_free(HDirectory *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->entries[i].record);
    free(list->entries);
    free(list->nodes);
    memset(list, 0, sizeof *list);
}
static int directory_read_node(HDisk *d, uint32_t sector, HDirectory *list, uint32_t parent,
                               unsigned depth)
{
    if (depth > 32 || !unique_node(&list->nodes, &list->node_count, sector))
        return herr(EIO);
    unsigned char block[2048];
    if (sector % 4 || !hread(d, sector, block, 4) || h32(block) != H_DNODE ||
        h32(block + 16) != sector)
        return herr(EIO);
    unsigned end = h32(block + 4);
    if (end > 2048 || end < 52)
        return herr(EIO);
    for (unsigned at = 20; at < end;)
    {
        const unsigned char *entry = block + at;
        unsigned size = h16(entry), flags = entry[2];
        if (size < 32 || size % 4 || size > end - at ||
            31u + entry[30] + ((flags & 4) ? 4u : 0u) > size)
            return herr(EIO);
        if ((flags & 4) && !directory_read_node(d, h32(entry + size - 4), list, parent, depth + 1))
            return 0;
        if (flags & 8)
            return at + size == end ? 1 : herr(EIO);
        if ((entry[29] & 128) ||
            ((entry[29] & 127) && (!d->codepages || (entry[29] & 127) >= d->codepage_count)))
            return herr(ENOTSUP);
        HEntry *next = realloc(list->entries, (list->count + 1) * sizeof *next);
        if (!next)
            return 0;
        list->entries = next;
        size -= (flags & 4) ? 4 : 0;
        unsigned char *copy = malloc(size);
        if (!copy)
            return 0;
        memcpy(copy, entry, size);
        copy[2] &= ~4u;
        p16(copy, size);
        list->entries[list->count++] = (HEntry){copy, size, parent};
        at += h16(entry);
    }
    return herr(EIO);
}
static int directory_read(HDisk *d, uint32_t fnode, HDirectory *list)
{
    memset(list, 0, sizeof *list);
    unsigned char file[512];
    if (!hread(d, fnode, file, 1) || h32(file) != H_FNODE || !(file[55] & 1))
        return herr(ENOTDIR);
    uint32_t root = h32(file + ((file[56] & 128) ? 68 : 72));
    if (!directory_read_node(d, root, list, fnode, 0))
    {
        directory_free(list);
        return 0;
    }
    return 1;
}
static char *name_utf8(HDisk *d, const unsigned char *entry)
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
static int name_oem(HDisk *d, unsigned index, const char *text, unsigned char name[255],
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
                return herr(EILSEQ);
            value &= more == 2 ? 15 : 31;
            for (unsigned i = 0; i < more; i++)
            {
                if ((*p & 0xc0) != 0x80)
                    return herr(EILSEQ);
                value = value << 6 | (*p++ & 63);
            }
            if (value >= 0xd800 && value <= 0xdfff)
                return herr(EILSEQ);
            value = ff_uni2oem(value, d->codepages ? d->codepages[index].codepage : d->codepage);
            if (!value || value > 255)
                return herr(EILSEQ);
        }
        if (value < 32 || strchr("<>:\"/\\|?*", value) || n == 254)
            return herr(EINVAL);
        name[n++] = value;
    }
    if (!n || name[n - 1] == ' ' || name[n - 1] == '.')
        return herr(EINVAL);
    *length = n;
    return 1;
}
static int compare_name(HDisk *d, const unsigned char *a, size_t na, unsigned ia,
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
static int find_entry(HDisk *d, HDirectory *list, const char *name, size_t *index)
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
    return herr(ENOENT);
}
static char *normalized(const char *path)
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
static int locate(HDisk *d, const char *path, HDirectory *parent, size_t *index, int allow_missing)
{
    char *copy = normalized(path);
    if (!copy)
        return 0;
    char *cursor = copy;
    uint32_t directory = d->root;
    if (!*cursor)
    {
        free(copy);
        return herr(EISDIR);
    }
    for (;;)
    {
        char *slash = strchr(cursor, '/');
        if (slash)
            *slash = 0;
        if (!directory_read(d, directory, parent))
        {
            free(copy);
            return 0;
        }
        if (!find_entry(d, parent, cursor, index))
        {
            int missing = allow_missing && !slash && errno == ENOENT;
            if (!missing)
                directory_free(parent);
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
        directory = h32(entry + 4);
        int isdir = entry[3] & 16;
        directory_free(parent);
        if (!isdir)
        {
            free(copy);
            return herr(ENOTDIR);
        }
        cursor = slash + 1;
    }
}
static void file_free(HFile *file)
{
    free(file->extents);
    free(file->nodes);
    memset(file, 0, sizeof *file);
}
static int extent_add(HFile *file, uint32_t logical, uint32_t count, uint32_t sector)
{
    if (file->count >= H_LIMIT)
        return herr(EFBIG);
    if (file->count)
    {
        HExtent *last = file->extents + file->count - 1;
        if ((uint64_t)last->logical + last->count != logical)
            return herr(EIO);
        if ((uint64_t)last->sector + last->count == sector)
        {
            last->count += count;
            return 1;
        }
    }
    else if (logical)
        return herr(EIO);
    HExtent *next = realloc(file->extents, (file->count + 1) * sizeof *next);
    if (!next)
        return 0;
    file->extents = next;
    next[file->count++] = (HExtent){logical, count, sector};
    return 1;
}
static int extent_tree(HDisk *d, HFile *file, const unsigned char *header, unsigned capacity,
                       unsigned depth)
{
    if (depth > 32)
        return herr(EIO);
    unsigned used = header[5], internal = header[0] & 128, width = internal ? 8 : 12;
    if (used > capacity || h16(header + 6) < 8 + used * width)
        return herr(EIO);
    for (unsigned i = 0; i < used; i++)
    {
        const unsigned char *entry = header + 8 + i * width;
        if (internal)
        {
            uint32_t sector = h32(entry + 4);
            unsigned char node[512];
            if (!unique_node(&file->nodes, &file->node_count, sector) ||
                !hread(d, sector, node, 1) || h32(node) != H_ANODE || h32(node + 4) != sector)
                return herr(EIO);
            if (!extent_tree(d, file, node + 12, node[12] & 128 ? 60 : 40, depth + 1))
                return 0;
        }
        else
        {
            uint32_t logical = h32(entry), count = h32(entry + 4), sector = h32(entry + 8);
            if (!count || sector < 18 || sector >= d->sectors || count > d->sectors - sector ||
                (uint64_t)logical + count > UINT32_MAX)
                return herr(EIO);
            if (!extent_add(file, logical, count, sector))
                return 0;
        }
    }
    return 1;
}
static int file_load(HDisk *d, uint32_t sector, HFile *file)
{
    memset(file, 0, sizeof *file);
    file->sector = sector;
    if (!hread(d, sector, file->data, 1) || h32(file->data) != H_FNODE || (file->data[55] & 1))
        return herr(EISDIR);
    if (!extent_tree(d, file, file->data + 56, file->data[56] & 128 ? 12 : 8, 0))
    {
        file_free(file);
        return 0;
    }
    uint64_t sectors = file->count ? (uint64_t)file->extents[file->count - 1].logical +
                                         file->extents[file->count - 1].count
                                   : 0;
    if (sectors * 512 < h32(file->data + 160))
    {
        file_free(file);
        return herr(EIO);
    }
    return 1;
}
static uint32_t file_sector(HFile *file, uint32_t logical)
{
    for (size_t i = 0; i < file->count; i++)
    {
        HExtent *e = file->extents + i;
        if (logical >= e->logical && logical - e->logical < e->count)
            return e->sector + logical - e->logical;
    }
    errno = EIO;
    return 0;
}
static int file_bytes(HDisk *d, HFile *file, FILE *output)
{
    uint32_t size = h32(file->data + 160);
    unsigned char data[512];
    for (uint32_t at = 0; at < size;)
    {
        uint32_t sector = file_sector(file, at / 512);
        unsigned n = size - at < 512 ? size - at : 512;
        if (!sector || !hread(d, sector, data, 1) || fwrite(data, 1, n, output) != n)
            return 0;
        at += n;
    }
    return 1;
}
int tb_hpfs_info(const char *path, TbImageInfo *info)
{
    HDisk disk;
    if (!hopen(path, 0, NULL, &disk))
        return 0;
    char *name = normalized(path);
    HDirectory list = {0};
    size_t index = 0;
    int ok = name != NULL;
    memset(info, 0, sizeof *info);
    info->modified = INT64_MIN;
    if (ok && *name)
    {
        ok = locate(&disk, path, &list, &index, 0);
        if (ok)
        {
            info->directory = !!(list.entries[index].record[3] & 16);
            info->size = h32(list.entries[index].record + 12);
            info->readonly = !!(list.entries[index].record[3] & 1);
            info->attributes = list.entries[index].record[3] & 0x26;
            info->modified = h32(list.entries[index].record + 8);
            info->accessed = h32(list.entries[index].record + 16);
            info->created = h32(list.entries[index].record + 20);
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
        info->serial = h32(disk.boot + 39);
        memcpy(info->label, disk.boot + 43, 11);
        for (int i = 10; i >= 0 && info->label[i] == ' '; i--)
            info->label[i] = 0;
        if (!info->readonly && name && !*name)
        {
            ok = bitmap_load(&disk);
            for (uint32_t i = 0; ok && i < disk.sectors; i++)
                if (free_sector(&disk, i))
                    info->available += 512;
        }
    }
    free(name);
    directory_free(&list);
    hclose(&disk);
    return ok;
}
char **tb_hpfs_names(const char *path, size_t *count)
{
    *count = (size_t)-1;
    HDisk disk;
    if (!hopen(path, 0, NULL, &disk))
        return NULL;
    char *name = normalized(path);
    HDirectory parent = {0}, list = {0};
    size_t index = 0, n = 0;
    char **names = NULL;
    uint32_t fnode = disk.root;
    int ok = name != NULL;
    if (ok && *name)
    {
        ok = locate(&disk, path, &parent, &index, 0);
        if (ok)
            fnode = h32(parent.entries[index].record + 4);
    }
    if (ok)
        ok = directory_read(&disk, fnode, &list);
    for (size_t i = 0; ok && i < list.count; i++)
    {
        if (list.entries[i].record[2] & 1)
            continue;
        char *text = name_utf8(&disk, list.entries[i].record);
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
    directory_free(&parent);
    directory_free(&list);
    hclose(&disk);
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
    HDisk disk;
    if (!hopen(path, 0, NULL, &disk))
        return NULL;
    HDirectory parent = {0};
    HFile file = {0};
    size_t index;
    FILE *snapshot = NULL;
    int ok = locate(&disk, path, &parent, &index, 0) &&
             file_load(&disk, h32(parent.entries[index].record + 4), &file);
    if (ok)
    {
        snapshot = tmpfile();
        ok = snapshot != NULL && file_bytes(&disk, &file, snapshot);
    }
    if (ok)
        ok = !fflush(snapshot) && !fseeko(snapshot, 0, SEEK_SET);
    if (!ok && snapshot)
    {
        fclose(snapshot);
        snapshot = NULL;
    }
    file_free(&file);
    directory_free(&parent);
    hclose(&disk);
    return snapshot;
}
static int node_parent(HDisk *d, uint32_t node, uint32_t parent, int fnode)
{
    unsigned char bytes[512];
    if (!hread(d, node, bytes, 1))
        return 0;
    p32(bytes + 8, parent);
    bytes[12] = fnode ? bytes[12] | 32 : bytes[12] & ~32u;
    return hpatch(d, node, bytes, 1);
}
typedef struct HRef
{
    uint32_t sector, end;
} HRef;
static int file_tree_build(HDisk *d, HFile *file)
{
    for (size_t i = 0; i < file->node_count; i++)
        if (!mark_range(d, file->nodes[i], 1, 1))
            return 0;
    free(file->nodes);
    file->nodes = NULL;
    file->node_count = 0;
    memset(file->data + 56, 0, 104);
    if (file->count <= 8)
    {
        file->data[60] = 8 - file->count;
        file->data[61] = file->count;
        p16(file->data + 62, (unsigned)(8 + file->count * 12));
        for (size_t i = 0; i < file->count; i++)
        {
            unsigned char *p = file->data + 64 + i * 12;
            p32(p, file->extents[i].logical);
            p32(p + 4, file->extents[i].count);
            p32(p + 8, file->extents[i].sector);
        }
    }
    else
    {
        size_t count = (file->count + 39) / 40;
        HRef *refs = calloc(count, sizeof *refs);
        if (!refs)
            return 0;
        for (size_t group = 0; group < count; group++)
        {
            unsigned char bytes[512] = {0};
            uint32_t sector = allocate(d, 1, 1);
            if (!sector)
            {
                free(refs);
                return 0;
            }
            size_t start = group * 40, used = file->count - start;
            if (used > 40)
                used = 40;
            p32(bytes, H_ANODE);
            p32(bytes + 4, sector);
            bytes[16] = 40 - used;
            bytes[17] = used;
            p16(bytes + 18, (unsigned)(8 + used * 12));
            for (size_t i = 0; i < used; i++)
            {
                unsigned char *p = bytes + 20 + i * 12;
                HExtent *e = file->extents + start + i;
                p32(p, e->logical);
                p32(p + 4, e->count);
                p32(p + 8, e->sector);
            }
            HExtent *last = file->extents + start + used - 1;
            refs[group] = (HRef){sector, last->logical + last->count};
            if (!hpatch(d, sector, bytes, 1))
            {
                free(refs);
                return 0;
            }
        }
        while (count > 12)
        {
            size_t groups = (count + 59) / 60;
            HRef *next = calloc(groups, sizeof *next);
            if (!next)
            {
                free(refs);
                return 0;
            }
            int ok = 1;
            for (size_t group = 0; ok && group < groups; group++)
            {
                unsigned char bytes[512] = {0};
                uint32_t sector = allocate(d, 1, 1);
                if (!sector)
                {
                    ok = 0;
                    break;
                }
                size_t start = group * 60, used = count - start;
                if (used > 60)
                    used = 60;
                p32(bytes, H_ANODE);
                p32(bytes + 4, sector);
                bytes[12] = 128;
                bytes[16] = 60 - used;
                bytes[17] = used;
                p16(bytes + 18, (unsigned)(8 + used * 8));
                for (size_t i = 0; ok && i < used; i++)
                {
                    p32(bytes + 20 + i * 8, i + 1 == used ? UINT32_MAX : refs[start + i].end);
                    p32(bytes + 24 + i * 8, refs[start + i].sector);
                    ok = node_parent(d, refs[start + i].sector, sector, 0);
                }
                next[group] = (HRef){sector, refs[start + used - 1].end};
                if (ok)
                    ok = hpatch(d, sector, bytes, 1);
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
        file->data[56] = 128;
        file->data[60] = 12 - count;
        file->data[61] = count;
        p16(file->data + 62, (unsigned)(8 + count * 8));
        for (size_t i = 0; i < count; i++)
        {
            p32(file->data + 64 + i * 8, i + 1 == count ? UINT32_MAX : refs[i].end);
            p32(file->data + 68 + i * 8, refs[i].sector);
            if (!node_parent(d, refs[i].sector, file->sector, 1))
            {
                free(refs);
                return 0;
            }
        }
        free(refs);
    }
    return hpatch(d, file->sector, file->data, 1);
}
static int file_resize(HDisk *d, HFile *file, uint64_t bytes)
{
    if (bytes > 0x7fffffffu)
        return herr(EFBIG);
    uint32_t needed = (uint32_t)((bytes + 511) / 512);
    uint32_t allocated =
        file->count ? file->extents[file->count - 1].logical + file->extents[file->count - 1].count
                    : 0;
    if (needed < allocated)
    {
        size_t used = 0;
        for (size_t i = 0; i < file->count; i++)
        {
            HExtent extent = file->extents[i];
            if (extent.logical >= needed)
            {
                if (!mark_range(d, extent.sector, extent.count, 1))
                    return 0;
                continue;
            }
            if (extent.count > needed - extent.logical)
            {
                uint32_t keep = needed - extent.logical;
                if (!mark_range(d, extent.sector + keep, extent.count - keep, 1))
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
        for (uint32_t i = 18; i < d->sectors; i++)
            free += free_sector(d, i);
        if (needed - allocated > free)
            return herr(ENOSPC);
        unsigned char zero[512] = {0};
        for (uint32_t at = 18; allocated < needed && at < d->sectors;)
        {
            if (!free_sector(d, at))
            {
                at++;
                continue;
            }
            uint32_t count = 0;
            while (count < needed - allocated && at + count < d->sectors &&
                   free_sector(d, at + count))
                count++;
            if (!mark_range(d, at, count, 0) || !extent_add(file, allocated, count, at))
                return 0;
            for (uint32_t i = 0; i < count; i++)
                if (!hpatch(d, at + i, zero, 1))
                    return 0;
            allocated += count;
            at += count;
        }
        if (allocated < needed)
            return herr(ENOSPC);
    }
    uint32_t previous = h32(file->data + 160);
    if (bytes > previous && previous % 512)
    {
        unsigned char data[512];
        uint32_t sector = file_sector(file, previous / 512);
        if (!sector || !hread(d, sector, data, 1))
            return 0;
        memset(data + previous % 512, 0, 512 - previous % 512);
        if (!hpatch(d, sector, data, 1))
            return 0;
    }
    p32(file->data + 160, (uint32_t)bytes);
    return file_tree_build(d, file);
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
static uint32_t directory_build_node(HDisk *d, HEntry *entries, size_t count, uint32_t parent,
                                     unsigned height, unsigned keys, int root)
{
    uint32_t sector = allocate_dir(d);
    if (!sector)
        return 0;
    unsigned char block[2048] = {0};
    p32(block, H_DNODE);
    p32(block + 8, root ? 1 : 0);
    p32(block + 12, parent);
    p32(block + 16, sector);
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
        p16(block + at, 32);
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
                HEntry *entry = entries + start++;
                if (entry->size + 4 > 2048 - at - 36)
                {
                    errno = EIO;
                    return 0;
                }
                memcpy(block + at, entry->record, entry->size);
                p16(block + at, (unsigned)(entry->size + 4));
                block[at + 2] |= 4;
                p32(block + at + entry->size, child);
                at += entry->size + 4;
            }
            else
            {
                p16(block + at, 36);
                block[at + 2] = 12;
                block[at + 30] = 1;
                block[at + 31] = 255;
                p32(block + at + 32, child);
                at += 36;
            }
        }
    }
    p32(block + 4, (uint32_t)at);
    return hpatch(d, sector, block, 4) ? sector : 0;
}
static int directory_save(HDisk *d, uint32_t fnode, HDirectory *list)
{
    for (size_t i = 0; i < list->node_count; i++)
        if (!free_dir(d, list->nodes[i]))
            return 0;
    size_t longest = 32;
    for (size_t i = 0; i < list->count; i++)
        if (list->entries[i].size > longest)
            longest = list->entries[i].size;
    unsigned keys = (2028 - 36) / (longest + 4);
    if (!keys)
        return herr(ENOTSUP);
    if (keys > 64)
        keys = 64;
    unsigned height = 0;
    while (tree_capacity(height, keys) < list->count)
    {
        if (++height > 32)
            return herr(EFBIG);
    }
    uint32_t root = directory_build_node(d, list->entries, list->count, fnode, height, keys, 1);
    if (!root)
        return 0;
    unsigned char file[512];
    if (!hread(d, fnode, file, 1) || h32(file) != H_FNODE)
        return herr(EIO);
    memset(file + 56, 0, 104);
    file[60] = 7;
    file[61] = 1;
    p16(file + 62, 20);
    p32(file + 72, root);
    p32(file + 76, UINT32_MAX);
    return hpatch(d, fnode, file, 1);
}
static int entry_insert(HDisk *d, HDirectory *list, HEntry entry)
{
    size_t at = 0;
    while (at < list->count &&
           compare_name(d, list->entries[at].record + 31, list->entries[at].record[30],
                        list->entries[at].record[29] & 127, entry.record + 31, entry.record[30],
                        entry.record[29] & 127) < 0)
        at++;
    HEntry *next = realloc(list->entries, (list->count + 1) * sizeof *next);
    if (!next)
        return 0;
    list->entries = next;
    memmove(next + at + 1, next + at, (list->count - at) * sizeof *next);
    next[at] = entry;
    list->count++;
    return 1;
}
static void entry_remove(HDirectory *list, size_t index)
{
    free(list->entries[index].record);
    memmove(list->entries + index, list->entries + index + 1,
            (list->count - index - 1) * sizeof *list->entries);
    list->count--;
}
static int entry_named(HDisk *d, const char *path, const unsigned char *previous, uint32_t parent,
                       HEntry *entry)
{
    char *name = normalized(path);
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
    size_t extra = previous ? h16(previous) - ((31u + previous[30] + 3) & ~3u) : 0;
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
            memcpy(record + ((31 + n + 3) & ~3u), previous + h16(previous) - extra, extra);
    }
    else
    {
        record[3] = 32;
        uint32_t now = (uint32_t)time(NULL);
        p32(record + 8, now);
        p32(record + 16, now);
        p32(record + 20, now);
    }
    p16(record, (unsigned)size);
    record[30] = n;
    memcpy(record + 31, bytes, n);
    if (n > 12 || memchr(bytes, ' ', n))
        record[3] |= 64;
    *entry = (HEntry){record, size, parent};
    free(name);
    return 1;
}
static int new_file(HDisk *d, HEntry *entry, int directory)
{
    uint32_t sector = allocate(d, 1, 1);
    if (!sector)
        return 0;
    unsigned char bytes[512] = {0};
    p32(bytes, H_FNODE);
    bytes[12] = entry->record[30];
    unsigned n = bytes[12] > 15 ? 15 : bytes[12];
    memcpy(bytes + 13, entry->record + 31, n);
    p32(bytes + 28, entry->parent);
    bytes[55] = directory;
    bytes[60] = 8;
    p16(bytes + 62, 8);
    p16(bytes + 184, 196);
    p32(entry->record + 4, sector);
    if (directory)
        entry->record[3] = 16 | (entry->record[3] & 64);
    if (!hpatch(d, sector, bytes, 1))
        return 0;
    if (directory)
    {
        unsigned char *dot = calloc(36, 1);
        if (!dot)
            return 0;
        p16(dot, 36);
        dot[2] = 1;
        dot[3] = 16;
        p32(dot + 4, sector);
        p32(dot + 8, h32(entry->record + 8));
        p32(dot + 16, h32(entry->record + 16));
        p32(dot + 20, h32(entry->record + 20));
        dot[30] = 2;
        dot[31] = dot[32] = 1;
        HEntry special = {dot, 36, sector};
        HDirectory list = {0};
        int ok = entry_insert(d, &list, special);
        if (!ok)
            free(dot);
        else
            ok = directory_save(d, sector, &list);
        directory_free(&list);
        return ok;
    }
    return 1;
}
static int release_external(HDisk *d, const unsigned char *fnode, unsigned offset)
{
    uint32_t size = h32(fnode + offset), sector = h32(fnode + offset + 4);
    if (!size)
        return 1;
    if (!fnode[offset + 10])
        return mark_range(d, sector, (uint32_t)(((uint64_t)size + 511u) / 512u), 1);
    HFile tree = {0};
    unsigned char node[512];
    int ok = hread(d, sector, node, 1) && h32(node) == H_ANODE &&
             unique_node(&tree.nodes, &tree.node_count, sector) &&
             extent_tree(d, &tree, node + 12, node[12] & 128 ? 60 : 40, 0);
    for (size_t i = 0; ok && i < tree.count; i++)
        ok = mark_range(d, tree.extents[i].sector, tree.extents[i].count, 1);
    for (size_t i = 0; ok && i < tree.node_count; i++)
        ok = mark_range(d, tree.nodes[i], 1, 1);
    file_free(&tree);
    return ok;
}
static int ea_values_free(HDisk *d, const unsigned char *fnode);
static int file_delete(HDisk *d, HEntry *entry, int directory)
{
    uint32_t sector = h32(entry->record + 4);
    unsigned char fnode[512];
    if (!hread(d, sector, fnode, 1) || h32(fnode) != H_FNODE)
        return herr(EIO);
    if (directory)
    {
        HDirectory list;
        if (!directory_read(d, sector, &list))
            return 0;
        int ok = 1;
        for (size_t i = 0; i < list.count; i++)
            if (!(list.entries[i].record[2] & 1))
                ok = herr(ENOTEMPTY);
        for (size_t i = 0; ok && i < list.node_count; i++)
            ok = free_dir(d, list.nodes[i]);
        directory_free(&list);
        if (!ok)
            return 0;
    }
    else
    {
        HFile file;
        if (!file_load(d, sector, &file))
            return 0;
        int ok = 1;
        for (size_t i = 0; ok && i < file.count; i++)
            ok = mark_range(d, file.extents[i].sector, file.extents[i].count, 1);
        for (size_t i = 0; ok && i < file.node_count; i++)
            ok = mark_range(d, file.nodes[i], 1, 1);
        file_free(&file);
        if (!ok)
            return 0;
    }
    return ea_values_free(d, fnode) && release_external(d, fnode, 32) &&
           release_external(d, fnode, 44) && mark_range(d, sector, 1, 1);
}
static int write_file(HDisk *d, HEntry *entry, int action, uint64_t offset, const void *data,
                      size_t size)
{
    HFile file;
    if (!file_load(d, h32(entry->record + 4), &file))
        return 0;
    uint64_t length = action == TB_FS_RESIZE        ? offset
                      : offset <= UINT64_MAX - size ? offset + size
                                                    : UINT64_MAX;
    if (action != TB_FS_RESIZE && length < h32(file.data + 160))
        length = h32(file.data + 160);
    int ok = file_resize(d, &file, length);
    for (size_t at = 0; ok && action == TB_FS_WRITE && at < size;)
    {
        unsigned char bytes[512];
        uint64_t location = offset + at;
        uint32_t sector = file_sector(&file, (uint32_t)(location / 512));
        unsigned skip = location % 512;
        size_t n = 512 - skip;
        if (n > size - at)
            n = size - at;
        ok = sector && hread(d, sector, bytes, 1);
        if (ok)
        {
            memcpy(bytes + skip, (const unsigned char *)data + at, n);
            ok = hpatch(d, sector, bytes, 1);
        }
        at += n;
    }
    if (ok)
    {
        p32(entry->record + 12, (uint32_t)length);
        p32(entry->record + 8, (uint32_t)time(NULL));
        entry->record[3] |= 32;
    }
    file_free(&file);
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
            return herr(same ? EROFS : EXDEV);
    }
    HDisk disk;
    if (!hopen(path, 1, expected, &disk))
        return 0;
    HDirectory parent = {0}, destination = {0};
    size_t index = 0, to = 0;
    int create = action == TB_FS_CREATE || action == TB_FS_EXCLUSIVE || action == TB_FS_MKDIR;
    int ok = bitmap_load(&disk) && locate(&disk, path, &parent, &index, create);
    uint32_t parent_sector = parent.count ? parent.entries[0].parent : 0;
    if (ok && index == SIZE_MAX)
    {
        HEntry entry = {0};
        ok = entry_named(&disk, path, NULL, parent_sector, &entry);
        if (ok)
            ok = new_file(&disk, &entry, action == TB_FS_MKDIR);
        if (ok)
            ok = entry_insert(&disk, &parent, entry);
        if (!ok)
            free(entry.record);
        if (ok)
            ok = directory_save(&disk, parent_sector, &parent);
    }
    else if (ok)
    {
        HEntry *entry = parent.entries + index;
        if (action == TB_FS_EXCLUSIVE || action == TB_FS_MKDIR)
            ok = herr(EEXIST);
        else if (entry->record[3] & 1 && action != TB_FS_READONLY && action != TB_FS_WRITABLE &&
                 action != TB_FS_CREATE && action != TB_FS_ATTRIBUTES && action != TB_FS_TOUCH)
            ok = herr(EACCES);
        else if (action == TB_FS_CREATE && entry->record[3] & 16)
            ok = herr(EISDIR);
        else if (action == TB_FS_READONLY || action == TB_FS_WRITABLE)
        {
            entry->record[3] =
                action == TB_FS_READONLY ? entry->record[3] | 1 : entry->record[3] & ~1u;
            ok = directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_TOUCH)
        {
            const TbImageStamps *stamps = data;
            if (stamps->modified != INT64_MIN)
                p32(entry->record + 8, (uint32_t)stamps->modified);
            if (stamps->accessed != INT64_MIN)
                p32(entry->record + 16, (uint32_t)stamps->accessed);
            if (stamps->created != INT64_MIN)
                p32(entry->record + 20, (uint32_t)stamps->created);
            ok = directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_ATTRIBUTES)
        {
            entry->record[3] = (unsigned char)((entry->record[3] & ~0x26u) | (offset & 0x26));
            ok = directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_DELETE || action == TB_FS_RMDIR)
        {
            int directory = !!(entry->record[3] & 16);
            if (directory != (action == TB_FS_RMDIR))
                ok = herr(directory ? EISDIR : ENOTDIR);
            else
                ok = file_delete(&disk, entry, directory);
            if (ok)
            {
                entry_remove(&parent, index);
                ok = directory_save(&disk, parent_sector, &parent);
            }
        }
        else if (action == TB_FS_WRITE || action == TB_FS_RESIZE)
        {
            entry->record[3] |= 0x20;
            ok = write_file(&disk, entry, action, offset, data, size) &&
                 directory_save(&disk, parent_sector, &parent);
        }
        else if (action == TB_FS_RENAME)
        {
            ok = target && locate(&disk, target, &destination, &to, 1);
            uint32_t target_parent = destination.count ? destination.entries[0].parent : 0;
            uint32_t fnode = h32(entry->record + 4);
            if (ok && to != SIZE_MAX && h32(destination.entries[to].record + 4) != fnode)
                ok = herr(EEXIST);
            if (ok && entry->record[3] & 16)
            {
                uint32_t ancestor = target_parent;
                for (unsigned depth = 0; ancestor != disk.root; depth++)
                {
                    unsigned char bytes[512];
                    if (depth > 256 || ancestor == fnode || !hread(&disk, ancestor, bytes, 1) ||
                        h32(bytes) != H_FNODE)
                    {
                        ok = herr(EINVAL);
                        break;
                    }
                    uint32_t next = h32(bytes + 28);
                    if (next == ancestor)
                    {
                        ok = herr(EIO);
                        break;
                    }
                    ancestor = next;
                }
                if (target_parent == fnode)
                    ok = herr(EINVAL);
            }
            HEntry moved = {0};
            if (ok)
                ok = entry_named(&disk, target, entry->record, target_parent, &moved);
            unsigned char bytes[512];
            if (ok)
                ok = hread(&disk, fnode, bytes, 1);
            if (ok)
            {
                p32(bytes + 28, target_parent);
                bytes[12] = moved.record[30];
                memset(bytes + 13, 0, 15);
                unsigned n = bytes[12] > 15 ? 15 : bytes[12];
                memcpy(bytes + 13, moved.record + 31, n);
                ok = hpatch(&disk, fnode, bytes, 1);
            }
            if (ok)
            {
                entry_remove(&parent, index);
                if (parent_sector == target_parent)
                    ok = entry_insert(&disk, &parent, moved);
                else
                    ok = entry_insert(&disk, &destination, moved);
                if (ok)
                {
                    moved.record = NULL;
                    ok = directory_save(&disk, parent_sector, &parent);
                }
                if (ok && parent_sector != target_parent)
                    ok = directory_save(&disk, target_parent, &destination);
            }
            free(moved.record);
        }
    }
    if (ok)
        ok = commit(&disk);
    directory_free(&parent);
    directory_free(&destination);
    hclose(&disk);
    return ok;
}
static int format_file(const char *path, uint64_t bytes, FILE **file)
{
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = n ? malloc((size_t)n * sizeof *wide) : NULL;
    if (wide)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n);
    *file = wide ? _wfopen(wide, L"wbx") : NULL;
    free(wide);
#else
    *file = fopen(path, "wbx");
#endif
    if (!*file)
        return 0;
    if (fseeko(*file, (int64_t)bytes - 1, SEEK_SET) || fputc(0, *file) != 0 || fflush(*file))
    {
        fclose(*file);
        *file = NULL;
        remove(path);
        return 0;
    }
    fclose(*file);
    *file = tb_image_open_source(path, 1);
    return *file != NULL;
}
int tb_hpfs_format(const char *path, uint64_t bytes)
{
    if (bytes < 8 * 1024 * 1024 || bytes > UINT64_C(65536) * 1024 * 1024 ||
        bytes % (8 * 1024 * 1024))
        return herr(EINVAL);
    HDisk disk = {0};
    if (!format_file(path, bytes, &disk.file))
        return 0;
    disk.bytes = bytes;
    disk.sectors = (uint32_t)(bytes / 512);
    disk.bands = (disk.sectors + 16383) / 16384;
    disk.writable = 1;
    disk.codepage = 437;
    int ok = tb_image_lock_source(disk.file, 1);
    disk.bitmaps = malloc((size_t)disk.bands * 2048);
    disk.bitmap_locations = calloc(disk.bands, sizeof *disk.bitmap_locations);
    if (!disk.bitmaps || !disk.bitmap_locations)
        ok = 0;
    if (ok)
    {
        memset(disk.bitmaps, 255, (size_t)disk.bands * 2048);
        for (uint64_t i = disk.sectors; i < (uint64_t)disk.bands * 16384; i++)
            disk.bitmaps[i / 8] &= ~(1u << (i % 8));
        for (unsigned i = 0; i < 18; i++)
            disk.bitmaps[i / 8] &= ~(1u << (i % 8));
        unsigned list_sectors = ((disk.bands * 4 + 511) / 512 + 3) & ~3u;
        disk.bitmap_list = allocate(&disk, list_sectors, 4);
        for (uint32_t i = 0; ok && i < disk.bands; i++)
        {
            disk.bitmap_locations[i] = allocate(&disk, 4, 4);
            if (!disk.bitmap_locations[i])
                ok = 0;
        }
        disk.dir_map = allocate(&disk, 4, 4);
        uint32_t bad = allocate(&disk, 4, 4), hotfix = allocate(&disk, 4, 4),
                 uid = allocate(&disk, 8, 4), cpinfo = allocate(&disk, 1, 1),
                 cpdata = allocate(&disk, 1, 1);
        uint32_t hotfix_spares[8];
        for (unsigned i = 0; i < 8; i++)
            hotfix_spares[i] = allocate(&disk, 1, 1);
        disk.dir_count = (disk.sectors / 100 + 3) & ~3u;
        if (disk.dir_count > 65536)
            disk.dir_count = 65536;
        disk.dir_start = allocate(&disk, disk.dir_count, 4);
        memset(disk.dir_bitmap, 255, sizeof disk.dir_bitmap);
        if (!disk.bitmap_list || !disk.dir_map || !bad || !hotfix || !uid || !cpinfo || !cpdata ||
            !disk.dir_start)
            ok = 0;
        unsigned char zeros[2048] = {0}, table[2048] = {0};
        for (unsigned i = 0; i < 8; i++)
            p32(table + (8 + i) * 4, hotfix_spares[i]);
        if (ok)
            ok = hpatch(&disk, bad, zeros, 4) && hpatch(&disk, hotfix, table, 4);
        memcpy(disk.boot,
               "\xeb\x3c\x90"
               "2INEHPFS",
               11);
        p16(disk.boot + 11, 512);
        disk.boot[13] = 1;
        disk.boot[21] = 0xf8;
        p16(disk.boot + 24, 63);
        p16(disk.boot + 26, 255);
        p32(disk.boot + 32, disk.sectors);
        disk.boot[36] = 0x80;
        disk.boot[38] = 0x28;
        p32(disk.boot + 39, (uint32_t)time(NULL));
        memcpy(disk.boot + 43, "NO NAME    HPFS    ", 19);
        memcpy(disk.boot + 62, "\xfa\xf4\xeb\xfd", 4);
        disk.boot[510] = 0x55;
        disk.boot[511] = 0xaa;
        p32(disk.super, H_SUPER);
        p32(disk.super + 4, 0xfa53e9c5u);
        disk.super[8] = 2;
        disk.super[9] = 2;
        p32(disk.super + 16, disk.sectors);
        p32(disk.super + 24, disk.bitmap_list);
        p32(disk.super + 32, bad);
        p32(disk.super + 48, disk.dir_count);
        p32(disk.super + 52, disk.dir_start);
        p32(disk.super + 56, disk.dir_start + disk.dir_count - 1);
        p32(disk.super + 60, disk.dir_map);
        p32(disk.super + 96, uid);
        p32(disk.spare, H_SPARE);
        p32(disk.spare + 4, 0xfa5229c5u);
        p32(disk.spare + 12, hotfix);
        p32(disk.spare + 20, 8);
        p32(disk.spare + 24, 4);
        p32(disk.spare + 28, 4);
        p32(disk.spare + 32, cpinfo);
        p32(disk.spare + 36, 1);
        for (unsigned i = 0; i < 4; i++)
        {
            uint32_t sector = allocate(&disk, 4, 4);
            if (!sector)
                ok = 0;
            p32(disk.spare + 108 + i * 4, sector);
        }
        unsigned char info[512] = {0}, data[512] = {0};
        p32(info, 0x494521f7u);
        p32(info + 4, 1);
        p16(info + 16, 1);
        p16(info + 18, 437);
        p32(info + 24, cpdata);
        p32(data, 0x894521f7u);
        p16(data + 4, 1);
        p16(data + 20, 26);
        p16(data + 26, 1);
        p16(data + 28, 437);
        for (unsigned i = 0; i < 256; i++)
            disk.case_map[i] = i < 128 ? toupper(i) : i;
        for (unsigned i = 0; i < 128; i++)
        {
            unsigned value = ff_uni2oem(ff_wtoupper(ff_oem2uni(i + 128, 437)), 437);
            data[32 + i] = value && value <= 255 ? value : i + 128;
            disk.case_map[i + 128] = data[32 + i];
        }
        uint32_t checksum = hsum(data + 26, 136);
        p32(data + 8, checksum);
        p32(info + 20, checksum);
        if (ok)
            ok = hpatch(&disk, cpinfo, info, 1) && hpatch(&disk, cpdata, data, 1) &&
                 hpatch(&disk, 0, disk.boot, 1);
        for (unsigned i = 0; ok && i < list_sectors; i++)
        {
            unsigned char block[512] = {0};
            for (unsigned j = 0; j < 128 && i * 128 + j < disk.bands; j++)
                p32(block + j * 4, disk.bitmap_locations[i * 128 + j]);
            ok = hpatch(&disk, disk.bitmap_list + i, block, 1);
        }
        HEntry root = {0};
        unsigned char record[36] = {0};
        p16(record, 36);
        record[3] = 16;
        record[30] = 1;
        record[31] = '\\';
        root.record = record;
        root.size = 36;
        if (ok)
            ok = new_file(&disk, &root, 1);
        disk.root = h32(root.record + 4);
        p32(disk.super + 12, disk.root);
        if (ok)
        {
            unsigned char fnode[512];
            ok = hread(&disk, disk.root, fnode, 1);
            if (ok)
            {
                p32(fnode + 28, disk.root);
                fnode[12] = 0;
                memset(fnode + 13, 0, 15);
                ok = hpatch(&disk, disk.root, fnode, 1);
            }
        }
        if (ok)
            ok = hpatch(&disk, 16, disk.super, 1) && commit(&disk);
    }
    hclose(&disk);
    if (!ok)
    {
        int saved = errno;
        remove(path);
        errno = saved;
    }
    return ok;
}
/* OS/2 family APIs use a length-prefixed FEA list. HPFS stores its records
   without that four-byte prefix, either in the FNODE or external sectors. */
static int auxiliary_read(HDisk *d, uint32_t sector, uint32_t size, int tree, unsigned char *output)
{
    if (!size)
        return 1;
    if (size > 1048576)
        return herr(EFBIG);
    if (!tree)
    {
        unsigned char data[512];
        for (uint32_t at = 0; at < size;)
        {
            unsigned n = size - at < 512 ? size - at : 512;
            if (!hread(d, sector + at / 512, data, 1))
                return 0;
            memcpy(output + at, data, n);
            at += n;
        }
        return 1;
    }
    HFile file = {0};
    unsigned char node[512];
    int ok = hread(d, sector, node, 1) && h32(node) == H_ANODE &&
             unique_node(&file.nodes, &file.node_count, sector) &&
             extent_tree(d, &file, node + 12, node[12] & 128 ? 60 : 40, 0);
    for (uint32_t at = 0; ok && at < size;)
    {
        unsigned char data[512];
        uint32_t physical = file_sector(&file, at / 512);
        unsigned n = size - at < 512 ? size - at : 512;
        ok = physical && hread(d, physical, data, 1);
        if (ok)
            memcpy(output + at, data, n);
        at += n;
    }
    file_free(&file);
    return ok;
}
static unsigned char *ea_raw(HDisk *d, const unsigned char *fnode, size_t *size)
{
    uint32_t external = h32(fnode + 44);
    unsigned internal = h16(fnode + 52), base = h16(fnode + 184), acl = h16(fnode + 40);
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
    if (external && !auxiliary_read(d, h32(fnode + 48), external, fnode[54], data))
    {
        free(data);
        return NULL;
    }
    if (internal)
        memcpy(data, fnode + base + acl, internal);
    return data;
}
static int auxiliary_free(HDisk *d, uint32_t sector, uint32_t size, int tree)
{
    if (!size)
        return 1;
    if (!tree)
        return mark_range(d, sector, (uint32_t)(((uint64_t)size + 511) / 512), 1);
    HFile file = {0};
    unsigned char node[512];
    int ok = hread(d, sector, node, 1) && h32(node) == H_ANODE &&
             unique_node(&file.nodes, &file.node_count, sector) &&
             extent_tree(d, &file, node + 12, node[12] & 128 ? 60 : 40, 0);
    for (size_t i = 0; ok && i < file.count; i++)
        ok = mark_range(d, file.extents[i].sector, file.extents[i].count, 1);
    for (size_t i = 0; ok && i < file.node_count; i++)
        ok = mark_range(d, file.nodes[i], 1, 1);
    file_free(&file);
    return ok;
}
static int ea_values_free(HDisk *d, const unsigned char *fnode)
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
            ok = herr(EIO);
            break;
        }
        const unsigned char *entry = data + at;
        size_t count = 5u + entry[1] + h16(entry + 2);
        unsigned method = entry[0] & 127;
        if (count > size - at || entry[4 + entry[1]] || (method != 0 && method != 1 && method != 3))
        {
            ok = herr(EIO);
            break;
        }
        if (method)
        {
            const unsigned char *pointer = entry + 5 + entry[1];
            if (h16(entry + 2) != 8)
            {
                ok = herr(EIO);
                break;
            }
            ok = auxiliary_free(d, h32(pointer + 4), h32(pointer), method == 3);
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
    HDisk disk;
    if (!hopen(path, 0, expected, &disk))
        return 0;
    HDirectory parent = {0};
    size_t index;
    unsigned char fnode[512], *raw = NULL, *flat = NULL;
    size_t size = 0, used = 4;
    int ok = locate(&disk, path, &parent, &index, 0) &&
             hread(&disk, h32(parent.entries[index].record + 4), fnode, 1) && h32(fnode) == H_FNODE;
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
            ok = herr(EIO);
            break;
        }
        const unsigned char *entry = raw + at;
        size_t stored = 5u + entry[1] + h16(entry + 2), value = h16(entry + 2);
        unsigned method = entry[0] & 127;
        if (stored > size - at || !entry[1] || entry[4 + entry[1]] ||
            memchr(entry + 4, 0, entry[1]) || (method != 0 && method != 1 && method != 3))
        {
            ok = herr(EIO);
            break;
        }
        const unsigned char *pointer = entry + 5 + entry[1];
        if (method)
        {
            if (value != 8)
            {
                ok = herr(EIO);
                break;
            }
            value = h32(pointer);
        }
        size_t count = 5u + entry[1] + value;
        if (count > 65535 - used || value > 65535)
        {
            ok = herr(EFBIG);
            break;
        }
        unsigned char *record = flat + used;
        record[0] = entry[0] & 128;
        record[1] = entry[1];
        p16(record + 2, (unsigned)value);
        memcpy(record + 4, entry + 4, entry[1] + 1);
        if (method)
            ok = auxiliary_read(&disk, h32(pointer + 4), (uint32_t)value, method == 3,
                                record + 5 + entry[1]);
        else
            memcpy(record + 5 + entry[1], pointer, value);
        used += count;
        at += stored;
    }
    if (ok && size)
    {
        p32(flat, (uint32_t)used);
        *required = used;
        if (output)
        {
            if (capacity < used)
                ok = herr(ERANGE);
            else
                memcpy(output, flat, used);
        }
    }
    free(raw);
    free(flat);
    directory_free(&parent);
    hclose(&disk);
    return ok;
}
int tb_hpfs_metadata_set(const char *path, const char *expected, const void *bytes, size_t size)
{
    if (size < 4 || size > 65535 || h32(bytes) != size)
        return herr(EINVAL);
    const unsigned char *data = bytes;
    unsigned needed = 0;
    for (size_t at = 4; at < size;)
    {
        if (size - at < 5)
            return herr(EINVAL);
        size_t count = 5u + data[at + 1] + h16(data + at + 2);
        if (count > size - at || (data[at] & 127) || !data[at + 1] || data[at + 4 + data[at + 1]] ||
            memchr(data + at + 4, 0, data[at + 1]))
            return herr(EINVAL);
        needed += !!(data[at] & 128);
        at += count;
    }
    HDisk disk;
    if (!hopen(path, 1, expected, &disk))
        return 0;
    HDirectory parent = {0};
    size_t index;
    unsigned char fnode[512];
    int ok = bitmap_load(&disk) && locate(&disk, path, &parent, &index, 0) &&
             hread(&disk, h32(parent.entries[index].record + 4), fnode, 1) && h32(fnode) == H_FNODE;
    if (ok && parent.entries[index].record[3] & 1)
        ok = herr(EACCES);
    if (ok)
        ok = ea_values_free(&disk, fnode) && release_external(&disk, fnode, 44);
    if (ok)
    {
        unsigned base = h16(fnode + 184), acl = h16(fnode + 40);
        size_t n = size - 4;
        if (!base)
            base = 196;
        if (base < 196 || base > 512 || acl > 512 - base)
            ok = herr(EIO);
        else
        {
            memset(fnode + base + acl, 0, 512 - base - acl);
            p32(fnode + 44, 0);
            p32(fnode + 48, 0);
            p16(fnode + 52, 0);
            fnode[54] = 0;
            p16(fnode + 184, base);
            p32(fnode + 164, needed);
            if (n <= 512 - base - acl)
            {
                memcpy(fnode + base + acl, data + 4, n);
                p16(fnode + 52, (unsigned)n);
            }
            else
            {
                uint32_t count = (uint32_t)((n + 511) / 512), first = allocate(&disk, count, 1);
                if (!first)
                    ok = 0;
                for (uint32_t i = 0; ok && i < count; i++)
                {
                    unsigned char block[512] = {0};
                    size_t length = n - i * 512 < 512 ? n - i * 512 : 512;
                    memcpy(block, data + 4 + i * 512, length);
                    ok = hpatch(&disk, first + i, block, 1);
                }
                p32(fnode + 44, (uint32_t)n);
                p32(fnode + 48, first);
            }
            if (ok)
            {
                unsigned char *entry = parent.entries[index].record;
                entry[2] = (entry[2] & ~(16u | 128u)) | (n ? 16 : 0) | (needed ? 128 : 0);
                p32(entry + 24, (uint32_t)n);
                ok = hpatch(&disk, h32(entry + 4), fnode, 1) &&
                     directory_save(&disk, parent.entries[index].parent, &parent) && commit(&disk);
            }
        }
    }
    directory_free(&parent);
    hclose(&disk);
    return ok;
}
