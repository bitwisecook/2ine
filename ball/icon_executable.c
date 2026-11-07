/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read resources without loading or running guest code. All offsets and
   decompressed extents are bounded before use. See formats/lx.h and ne.h. */
#include "icon.h"
#include "../formats/decompress.h"
#include <stdlib.h>
#include <string.h>
static unsigned u16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)u16(p) | (uint32_t)u16(p + 2) << 16;
}
static int extent(size_t size, uint64_t offset, uint64_t count)
{
    return offset <= size && count <= size - offset;
}
/* Copies one resource out of its object pages, expanding packed pages as needed. */
static uint8_t *lx_resource(const uint8_t *bytes, size_t size, size_t h, const uint8_t *entry,
                            uint32_t *result)
{
    const uint8_t *header = bytes + h;
    uint32_t objects = u32(header + 64), object_count = u32(header + 68), pages = u32(header + 72),
             page_size = u32(header + 40), shift = u32(header + 44), data = u32(header + 128);
    uint32_t length = u32(entry + 4), object = u16(entry + 8), offset = u32(entry + 10);
    if (!length || length > 16 * 1024 * 1024 || !object || object > object_count)
        return NULL;
    const uint8_t *obj = bytes + h + objects + (size_t)(object - 1) * 24;
    uint32_t virtual_size = u32(obj), first = u32(obj + 12), page_count = u32(obj + 16);
    if (!first || offset > virtual_size || length > virtual_size - offset ||
        !extent(size, (uint64_t)h + pages + (uint64_t)(first - 1) * 8, (uint64_t)page_count * 8))
        return NULL;
    uint8_t *resource = calloc(1, length), *page = calloc(1, page_size);
    if (!resource || !page)
    {
        free(resource);
        free(page);
        return NULL;
    }
    int valid = 1;
    for (size_t done = 0; done < length;)
    {
        uint64_t pos = (uint64_t)offset + done;
        uint32_t index = (uint32_t)(pos / page_size), within = (uint32_t)(pos % page_size);
        if (index >= page_count)
        {
            valid = 0;
            break;
        }
        const uint8_t *pg = bytes + h + pages + (size_t)(first - 1 + index) * 8;
        uint32_t stored = u16(pg + 4), flags = u16(pg + 6);
        uint64_t origin = (uint64_t)data + ((uint64_t)u32(pg) << shift);
        memset(page, 0, page_size);
        if (flags == 0 || flags == 1 || flags == 5)
        {
            if (!extent(size, origin, stored) || (flags == 0 && stored > page_size))
            {
                valid = 0;
                break;
            }
            const uint8_t *src = bytes + (size_t)origin;
            if (flags == 0)
                memcpy(page, src, stored);
            else if (!(flags == 1 ? decompressIterated(page, page_size, src, stored)
                                  : decompressExePack2(page, page_size, src, stored)))
            {
                valid = 0;
                break;
            }
        }
        else if (flags != 2 && flags != 3)
        {
            valid = 0;
            break;
        }
        size_t n = page_size - within;
        if (n > length - done)
            n = length - done;
        memcpy(resource + done, page + within, n);
        done += n;
    }
    free(page);
    if (!valid)
    {
        free(resource);
        return NULL;
    }
    *result = length;
    return resource;
}
static TbIconImage *lx_icon(const uint8_t *bytes, size_t size, size_t h, int resource_id)
{
    const uint8_t *header = bytes + h;
    uint32_t table = u32(header + 80), count = u32(header + 84), objects = u32(header + 64),
             object_count = u32(header + 68), page_size = u32(header + 40),
             shift = u32(header + 44);
    if (!page_size || page_size > 65536 || shift > 31 || count > 65536 ||
        !extent(size, (uint64_t)h + table, (uint64_t)count * 14) ||
        !extent(size, (uint64_t)h + objects, (uint64_t)object_count * 24))
        return NULL;
    for (uint32_t r = 0; r < count; r++)
    {
        const uint8_t *entry = bytes + h + table + (size_t)r * 14;
        if (u16(entry) != 1 || (resource_id >= 0 && u16(entry + 2) != (unsigned)resource_id))
            continue;
        uint32_t length = 0;
        uint8_t *resource = lx_resource(bytes, size, h, entry, &length);
        if (!resource)
            continue;
        TbIconImage *image = tb_icon_bitmap(resource, length);
        free(resource);
        if (image)
            return image;
    }
    return NULL;
}
static TbIconImage *ne_icon(const uint8_t *bytes, size_t size, size_t h, int resource_id)
{
    const uint8_t *header = bytes + h;
    unsigned segments = u16(header + 28), resources = u16(header + 52), shift = u16(header + 50);
    uint64_t table = (uint64_t)h + u16(header + 36), segtab = (uint64_t)h + u16(header + 34);
    if (header[54] != 1 || resources > segments || shift > 31 ||
        !extent(size, table, (uint64_t)resources * 4) ||
        !extent(size, segtab, (uint64_t)segments * 8))
        return NULL;
    for (unsigned i = 0; i < resources; i++)
    {
        if (u16(bytes + (size_t)table + i * 4) != 1 ||
            (resource_id >= 0 && u16(bytes + (size_t)table + i * 4 + 2) != (unsigned)resource_id))
            continue;
        const uint8_t *segment = bytes + (size_t)segtab + (size_t)(segments - resources + i) * 8;
        uint64_t origin = (uint64_t)u16(segment) << shift;
        unsigned stored = u16(segment + 2);
        if (!stored)
            stored = 65536;
        if (!extent(size, origin, stored))
            continue;
        TbIconImage *image = NULL;
        if (u16(segment + 4) & 8)
        {
            uint8_t *decoded = calloc(1, 65536);
            if (decoded && decompressIterated(decoded, 65536, bytes + (size_t)origin, stored))
                image = tb_icon_bitmap(decoded, 65536);
            free(decoded);
        }
        else
            image = tb_icon_bitmap(bytes + (size_t)origin, stored);
        if (image)
            return image;
    }
    return NULL;
}
static TbIconImage *executable_icon(const uint8_t *bytes, size_t size, int id)
{
    if (!bytes || size < 2)
        return NULL;
    size_t h = 0;
    if (!memcmp(bytes, "MZ", 2))
    {
        if (size < 64)
            return NULL;
        h = u32(bytes + 60);
    }
    if (!extent(size, h, 64))
        return NULL;
    if (!memcmp(bytes + h, "LX", 2) && extent(size, h, 196) && !bytes[h + 2] && !bytes[h + 3])
        return lx_icon(bytes, size, h, id);
    if (!memcmp(bytes + h, "NE", 2))
        return ne_icon(bytes, size, h, id);
    return NULL;
}
uint8_t *tb_executable_resource(const uint8_t *bytes, size_t size, unsigned type, unsigned id,
                                size_t *length)
{
    if (!bytes || size < 64 || !length)
        return NULL;
    size_t h = !memcmp(bytes, "MZ", 2) ? u32(bytes + 60) : 0;
    if (!extent(size, h, 196) || memcmp(bytes + h, "LX", 2))
        return NULL;
    const uint8_t *header = bytes + h;
    uint32_t table = u32(header + 80), count = u32(header + 84), objects = u32(header + 64),
             object_count = u32(header + 68), page_size = u32(header + 40),
             shift = u32(header + 44);
    if (!page_size || page_size > 65536 || shift > 31 || count > 65536 ||
        !extent(size, (uint64_t)h + table, (uint64_t)count * 14) ||
        !extent(size, (uint64_t)h + objects, (uint64_t)object_count * 24))
        return NULL;
    for (uint32_t r = 0; r < count; r++)
    {
        const uint8_t *entry = bytes + h + table + (size_t)r * 14;
        if (u16(entry) != type || u16(entry + 2) != id)
            continue;
        uint32_t result = 0;
        uint8_t *resource = lx_resource(bytes, size, h, entry, &result);
        if (resource)
            *length = result;
        return resource;
    }
    return NULL;
}
TbIconImage *tb_icon_executable(const uint8_t *bytes, size_t size)
{
    return executable_icon(bytes, size, -1);
}
TbIconImage *tb_icon_executable_resource(const uint8_t *bytes, size_t size, unsigned id)
{
    return id <= 65535 ? executable_icon(bytes, size, (int)id) : NULL;
}
char *tb_executable_description(const uint8_t *bytes, size_t size)
{
    if (!bytes || size < 64)
        return NULL;
    size_t h = !memcmp(bytes, "MZ", 2) ? u32(bytes + 60) : 0;
    if (!extent(size, h, 64))
        return NULL;
    uint32_t offset = 0, length = 0;
    if (!memcmp(bytes + h, "LX", 2) && extent(size, h, 196))
    {
        offset = u32(bytes + h + 136);
        length = u32(bytes + h + 140);
    }
    else if (!memcmp(bytes + h, "NE", 2))
    {
        offset = u32(bytes + h + 44);
        length = u16(bytes + h + 32);
    }
    if (!offset || !length || !extent(size, offset, length))
        return NULL;
    size_t n = bytes[offset];
    if (!n || n + 3 > length || u16(bytes + offset + n + 1))
        return NULL;
    const uint8_t *name = bytes + offset + 1;
    /* Module descriptions have no declared code page. Retain printable ASCII
       exactly; ambiguous encodings use the caller's filename fallback. */
    for (size_t i = 0; i < n; i++)
        if (name[i] < 32 || name[i] > 126)
            return NULL;
    while (n && *name == ' ')
    {
        name++;
        n--;
    }
    while (n && name[n - 1] == ' ')
        n--;
    if (!n)
        return NULL;
    char *result = malloc(n + 1);
    if (result)
    {
        memcpy(result, name, n);
        result[n] = 0;
    }
    return result;
}
