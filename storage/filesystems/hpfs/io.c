/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS sector I/O with staged writes, volume probe and open, and code-page tables. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/volume/volume_internal.h"
#include "storage/common/host_stdio.h"
#include "storage/vendor/fatfs/ff.h"
#include "formats/endian.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int hpfs_fail(int error)
{
    errno = error;
    return 0;
}

static int read_bytes(FILE *file, uint64_t offset, void *bytes, size_t size)
{
    return !fseeko(file, (int64_t)offset, SEEK_SET) && fread(bytes, 1, size, file) == size
               ? 1
               : hpfs_fail(EIO);
}

int hpfs_read_sectors(HpfsVolume *d, uint32_t sector, void *bytes, unsigned count)
{
    if (sector >= d->sectors || count > d->sectors - sector)
        return hpfs_fail(EIO);
    for (unsigned i = 0; i < count; i++)
    {
        HpfsPatch *patch = d->changes[(sector + i) % 1024];
        while (patch && patch->sector != sector + i)
            patch = patch->next;
        if (patch)
            memcpy((unsigned char *)bytes + i * 512, patch->data, 512);
        else if (!read_bytes(d->file, ((uint64_t)d->base + sector + i) * 512,
                             (unsigned char *)bytes + i * 512, 512))
            return 0;
    }
    return 1;
}

int hpfs_stage_sectors(HpfsVolume *d, uint32_t sector, const void *bytes, unsigned count)
{
    if (sector >= d->sectors || count > d->sectors - sector)
        return hpfs_fail(EIO);
    for (unsigned i = 0; i < count; i++)
    {
        unsigned bucket = (sector + i) % 1024;
        HpfsPatch *patch = d->changes[bucket];
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

int hpfs_add_unique_node(uint32_t **nodes, size_t *count, uint32_t value)
{
    if (*count >= HPFS_NODE_LIMIT)
        return hpfs_fail(EFBIG);
    for (size_t i = 0; i < *count; i++)
        if ((*nodes)[i] == value)
            return hpfs_fail(EIO);
    uint32_t *next = realloc(*nodes, (*count + 1) * sizeof **nodes);
    if (!next)
        return 0;
    *nodes = next;
    next[(*count)++] = value;
    return 1;
}

void hpfs_close(HpfsVolume *d)
{
    int saved = errno;
    if (d->file)
        fclose(d->file);
    for (unsigned i = 0; i < 1024; i++)
    {
        HpfsPatch *p = d->changes[i];
        while (p)
        {
            HpfsPatch *next = p->next;
            free(p);
            p = next;
        }
    }
    free(d->codepages);
    free(d->bitmaps);
    free(d->bitmap_locations);
    errno = saved;
}

static int find_superblock(FILE *file, uint64_t bytes, uint32_t *base, unsigned char *super)
{
    unsigned char boot[512];
    *base = 0;
    if (bytes >= 18 * 512 && read_bytes(file, 16 * 512, super, 512) &&
        tb_le32(super) == HPFS_SUPERBLOCK_SIGNATURE)
        return 1;
    if (!read_bytes(file, 0, boot, 512) || boot[510] != 0x55 || boot[511] != 0xaa)
        return hpfs_fail(EINVAL);
    for (unsigned i = 0; i < 4; i++)
    {
        const unsigned char *p = boot + 446 + i * 16;
        uint32_t start = tb_le32(p + 8), count = tb_le32(p + 12);
        if (p[4] != 7 || count < 18 || ((uint64_t)start + count) * 512 > bytes)
            continue;
        if (read_bytes(file, ((uint64_t)start + 16) * 512, super, 512) &&
            tb_le32(super) == HPFS_SUPERBLOCK_SIGNATURE)
        {
            *base = start;
            return 1;
        }
    }
    return hpfs_fail(EINVAL);
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
    ok = bytes >= 0 && find_superblock(file, (uint64_t)bytes, &base, super);
    fclose(file);
    return ok;
}

int hpfs_open(const char *path, int write, const char *expected, HpfsVolume *d)
{
    memset(d, 0, sizeof *d);
    int protected = tb_image_protected(path);
    if (protected < 0)
        return 0;
    if (write && protected)
        return hpfs_fail(EROFS);
    char *source = tb_image_source(path);
    if (!source)
        return 0;
    if (expected && strcmp(expected, source))
    {
        free(source);
        return hpfs_fail(ENXIO);
    }
    d->file = tb_image_open_source(source, write);
    free(source);
    if (!d->file)
        return 0;
    if (!tb_image_lock_source(d->file, write) || fseeko(d->file, 0, SEEK_END))
        goto fail;
    int64_t bytes = ftello(d->file);
    if (bytes < 0 || !find_superblock(d->file, (uint64_t)bytes, &d->base, d->super))
        goto fail;
    d->bytes = bytes;
    d->sectors = tb_le32(d->super + 16);
    d->root = tb_le32(d->super + 12);
    if (tb_le32(d->super + 4) != 0xfa53e9c5u || d->super[8] != 2 || d->sectors < 2048 ||
        ((uint64_t)d->base + d->sectors) * 512 > d->bytes)
    {
        errno = EINVAL;
        goto fail;
    }
    if (!hpfs_read_sectors(d, 0, d->boot, 1) || !hpfs_read_sectors(d, 17, d->spare, 1))
        goto fail;
    if (tb_le16(d->boot + 11) != 512 || tb_le32(d->spare) != HPFS_SPAREBLOCK_SIGNATURE ||
        tb_le32(d->spare + 4) != 0xfa5229c5u)
    {
        errno = EINVAL;
        goto fail;
    }
    /* Hotfix remapping and HPFS386 allocation/ACL variants require their own backend. */
    if (tb_le32(d->spare + 16) || (write && (d->super[9] > 2 || d->spare[8] & 0x1f)))
    {
        errno = ENOTSUP;
        goto fail;
    }
    unsigned char root_fnode[512];
    if (d->root < 18 || !hpfs_read_sectors(d, d->root, root_fnode, 1) ||
        tb_le32(root_fnode) != HPFS_FNODE_SIGNATURE || !(root_fnode[55] & 1))
    {
        errno = EINVAL;
        goto fail;
    }
    d->writable = write;
    d->bands = (d->sectors + 16383) / 16384;
    d->bitmap_list = tb_le32(d->super + 24);
    d->dir_start = tb_le32(d->super + 52);
    d->dir_count = tb_le32(d->super + 48);
    d->dir_map = tb_le32(d->super + 60);
    if (d->dir_count > 65536 || d->dir_start % 4 || d->dir_count % 4 ||
        d->dir_start >= d->sectors || d->dir_count > d->sectors - d->dir_start)
    {
        errno = EINVAL;
        goto fail;
    }
    d->codepage = 437;
    for (unsigned i = 0; i < 256; i++)
        d->case_map[i] = i < 128 ? toupper(i) : i;
    uint32_t cpinfo = tb_le32(d->spare + 32);
    if (cpinfo)
    {
        d->codepage_count = tb_le32(d->spare + 36);
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
            if (page >= 128 || !hpfs_read_sectors(d, cpinfo, info, 1) ||
                tb_le32(info) != 0x494521f7u || tb_le32(info + 8) != loaded)
            {
                errno = EINVAL;
                goto fail;
            }
            unsigned count = tb_le32(info + 4);
            if (!count || count > 31 || count > d->codepage_count - loaded)
            {
                errno = EINVAL;
                goto fail;
            }
            for (unsigned i = 0; i < count; i++)
            {
                const unsigned char *entry = info + 16 + i * 16;
                unsigned index = loaded + i, codepage = tb_le16(entry + 2);
                unsigned char data[512];
                if (tb_le16(entry + 12) != index ||
                    !hpfs_read_sectors(d, tb_le32(entry + 8), data, 1) ||
                    tb_le32(data) != 0x894521f7u)
                {
                    errno = EINVAL;
                    goto fail;
                }
                unsigned first = tb_le16(data + 6), entries = tb_le16(data + 4);
                if (entries > 3 || index < first || index - first >= entries)
                {
                    errno = EINVAL;
                    goto fail;
                }
                unsigned offset = tb_le16(data + 20 + (index - first) * 2);
                if (offset > 512 - 136 || tb_le16(data + offset + 2) != codepage)
                {
                    errno = EINVAL;
                    goto fail;
                }
                if (codepage >= 900 || tb_le16(data + offset + 4) || !ff_oem2uni(128, codepage))
                {
                    errno = ENOTSUP;
                    goto fail;
                }
                HpfsCodepage *cp = d->codepages + index;
                cp->codepage = codepage;
                for (unsigned j = 0; j < 128; j++)
                    cp->map[j] = toupper(j);
                memcpy(cp->map + 128, data + offset + 6, 128);
            }
            loaded += count;
            cpinfo = tb_le32(info + 12);
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
    hpfs_close(d);
    d->file = NULL;
    return 0;
}
