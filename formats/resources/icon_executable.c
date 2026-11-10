/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read resources without loading or running guest code. All offsets and
   decompressed extents are bounded before use. See formats/executable/lx.h and ne.h. */
#include "formats/resources/icon.h"
#include "formats/endian.h"
#include "formats/executable/lx.h"
#include "formats/executable/lx_exepack.h"
#include "formats/executable/ne.h"
#include "formats/executable/os2_executable.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define LX32(header, field) tb_le32((header) + offsetof(LxHeader, field))
#define NE16(header, field) tb_le16((header) + offsetof(NeHeader, field))
#define FIELD16(record, type, field) tb_le16((record) + offsetof(type, field))
#define FIELD32(record, type, field) tb_le32((record) + offsetof(type, field))
/* OS/2 pointer and icon resources (RT_POINTER). */
#define RESOURCE_POINTER 1u
/* OS/2 NE resource table entries are a type and an id; the resources are
   the last segments of the segment table. */
#define NE_RESOURCE_ENTRY_SIZE 4u
#define NE_SEGMENT_LIMIT 65536u
/* Header sanity bounds: larger pages, shifts or tables mark a corrupt file. */
#define MAX_PAGE_SIZE 65536u
#define MAX_PAGE_SHIFT 31u
#define MAX_RESOURCE_COUNT 65536u
#define RESOURCE_LIMIT (16u * 1024u * 1024u)

static int extent(size_t size, uint64_t offset, uint64_t count)
{
    return offset <= size && count <= size - offset;
}

/* Copies one resource out of its object pages, expanding packed pages as needed. */
static uint8_t *lx_resource(const uint8_t *bytes, size_t size, size_t h, const uint8_t *entry,
                            uint32_t *result)
{
    const uint8_t *header = bytes + h;
    uint32_t objects = LX32(header, object_table_offset),
             object_count = LX32(header, module_num_objects),
             pages = LX32(header, object_page_table_offset), page_size = LX32(header, page_size),
             shift = LX32(header, page_offset_shift), data = LX32(header, data_pages_offset);
    uint32_t length = FIELD32(entry, LxResourceTableEntry, resource_size),
             object = FIELD16(entry, LxResourceTableEntry, object),
             offset = FIELD32(entry, LxResourceTableEntry, offset);
    if (!length || length > RESOURCE_LIMIT || !object || object > object_count)
        return NULL;
    const uint8_t *obj = bytes + h + objects + (size_t)(object - 1) * sizeof(LxObjectTableEntry);
    uint32_t virtual_size = FIELD32(obj, LxObjectTableEntry, virtual_size),
             first = FIELD32(obj, LxObjectTableEntry, page_table_index),
             page_count = FIELD32(obj, LxObjectTableEntry, num_page_table_entries);
    if (!first || offset > virtual_size || length > virtual_size - offset ||
        !extent(size, (uint64_t)h + pages + (uint64_t)(first - 1) * sizeof(LxObjectPageTableEntry),
                (uint64_t)page_count * sizeof(LxObjectPageTableEntry)))
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
        const uint8_t *pg =
            bytes + h + pages + (size_t)(first - 1 + index) * sizeof(LxObjectPageTableEntry);
        uint32_t stored = FIELD16(pg, LxObjectPageTableEntry, data_size),
                 flags = FIELD16(pg, LxObjectPageTableEntry, flags);
        uint64_t origin =
            (uint64_t)data +
            ((uint64_t)FIELD32(pg, LxObjectPageTableEntry, page_data_offset) << shift);
        memset(page, 0, page_size);
        if (flags == LX_PAGE_VALID || flags == LX_PAGE_ITERDATA || flags == LX_PAGE_ITERDATA2)
        {
            if (!extent(size, origin, stored) || (flags == LX_PAGE_VALID && stored > page_size))
            {
                valid = 0;
                break;
            }
            const uint8_t *src = bytes + (size_t)origin;
            if (flags == LX_PAGE_VALID)
                memcpy(page, src, stored);
            else if (!(flags == LX_PAGE_ITERDATA
                           ? lx_exepack_iterated_decompress(page, page_size, src, stored)
                           : lx_exepack2_decompress(page, page_size, src, stored)))
            {
                valid = 0;
                break;
            }
        }
        else if (flags != LX_PAGE_INVALID && flags != LX_PAGE_ZEROED)
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
    uint32_t table = LX32(header, resource_table_offset),
             count = LX32(header, num_resource_table_entries),
             objects = LX32(header, object_table_offset),
             object_count = LX32(header, module_num_objects), page_size = LX32(header, page_size),
             shift = LX32(header, page_offset_shift);
    if (!page_size || page_size > MAX_PAGE_SIZE || shift > MAX_PAGE_SHIFT ||
        count > MAX_RESOURCE_COUNT ||
        !extent(size, (uint64_t)h + table, (uint64_t)count * sizeof(LxResourceTableEntry)) ||
        !extent(size, (uint64_t)h + objects, (uint64_t)object_count * sizeof(LxObjectTableEntry)))
        return NULL;
    for (uint32_t r = 0; r < count; r++)
    {
        const uint8_t *entry = bytes + h + table + (size_t)r * sizeof(LxResourceTableEntry);
        if (FIELD16(entry, LxResourceTableEntry, type_id) != RESOURCE_POINTER ||
            (resource_id >= 0 &&
             FIELD16(entry, LxResourceTableEntry, name_id) != (unsigned)resource_id))
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
    unsigned segments = NE16(header, num_segment_table_entries),
             resources = NE16(header, num_resource_entries),
             shift = NE16(header, sector_alignment_shift_count);
    uint64_t table = (uint64_t)h + NE16(header, resource_table_offset),
             segtab = (uint64_t)h + NE16(header, segment_table_offset);
    if (header[offsetof(NeHeader, exe_type)] != TB_NE_TARGET_OS2 || resources > segments ||
        shift > MAX_PAGE_SHIFT ||
        !extent(size, table, (uint64_t)resources * NE_RESOURCE_ENTRY_SIZE) ||
        !extent(size, segtab, (uint64_t)segments * sizeof(NeSegmentTableEntry)))
        return NULL;
    for (unsigned i = 0; i < resources; i++)
    {
        const uint8_t *resource = bytes + (size_t)table + i * NE_RESOURCE_ENTRY_SIZE;
        if (tb_le16(resource) != RESOURCE_POINTER ||
            (resource_id >= 0 && tb_le16(resource + 2) != (unsigned)resource_id))
            continue;
        const uint8_t *segment = bytes + (size_t)segtab +
                                 (size_t)(segments - resources + i) * sizeof(NeSegmentTableEntry);
        uint64_t origin = (uint64_t)FIELD16(segment, NeSegmentTableEntry, offset) << shift;
        /* A zero segment size means 64 KiB. */
        unsigned stored = FIELD16(segment, NeSegmentTableEntry, size);
        if (!stored)
            stored = NE_SEGMENT_LIMIT;
        if (!extent(size, origin, stored))
            continue;
        TbIconImage *image = NULL;
        if (FIELD16(segment, NeSegmentTableEntry, segment_flags) & NSITER)
        {
            uint8_t *decoded = calloc(1, NE_SEGMENT_LIMIT);
            if (decoded && lx_exepack_iterated_decompress(decoded, NE_SEGMENT_LIMIT,
                                                          bytes + (size_t)origin, stored))
                image = tb_icon_bitmap(decoded, NE_SEGMENT_LIMIT);
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
    TbExecutable exe;
    if (!bytes || size < 2 || !tb_executable_open_memory(&exe, bytes, size) ||
        exe.header_size < TB_NE_HEADER_SIZE)
        return NULL;
    if (exe.format == TB_EXECUTABLE_LX && tb_executable_linear_header(&exe))
        return lx_icon(bytes, size, (size_t)exe.header_offset, id);
    if (exe.format == TB_EXECUTABLE_NE)
        return ne_icon(bytes, size, (size_t)exe.header_offset, id);
    return NULL;
}

uint8_t *tb_executable_resource(const uint8_t *bytes, size_t size, unsigned type, unsigned id,
                                size_t *length)
{
    TbExecutable exe;
    if (!bytes || size < TB_MZ_HEADER_SIZE || !length ||
        !tb_executable_open_memory(&exe, bytes, size) || exe.format != TB_EXECUTABLE_LX ||
        exe.header_size < TB_LX_HEADER_SIZE)
        return NULL;
    size_t h = (size_t)exe.header_offset;
    const uint8_t *header = bytes + h;
    uint32_t table = LX32(header, resource_table_offset),
             count = LX32(header, num_resource_table_entries),
             objects = LX32(header, object_table_offset),
             object_count = LX32(header, module_num_objects), page_size = LX32(header, page_size),
             shift = LX32(header, page_offset_shift);
    if (!page_size || page_size > MAX_PAGE_SIZE || shift > MAX_PAGE_SHIFT ||
        count > MAX_RESOURCE_COUNT ||
        !extent(size, (uint64_t)h + table, (uint64_t)count * sizeof(LxResourceTableEntry)) ||
        !extent(size, (uint64_t)h + objects, (uint64_t)object_count * sizeof(LxObjectTableEntry)))
        return NULL;
    for (uint32_t r = 0; r < count; r++)
    {
        const uint8_t *entry = bytes + h + table + (size_t)r * sizeof(LxResourceTableEntry);
        if (FIELD16(entry, LxResourceTableEntry, type_id) != type ||
            FIELD16(entry, LxResourceTableEntry, name_id) != id)
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
    return id <= UINT16_MAX ? executable_icon(bytes, size, (int)id) : NULL;
}

char *tb_executable_description(const uint8_t *bytes, size_t size)
{
    TbExecutable exe;
    if (!bytes || size < TB_MZ_HEADER_SIZE || !tb_executable_open_memory(&exe, bytes, size) ||
        exe.header_size < TB_NE_HEADER_SIZE)
        return NULL;
    const uint8_t *header = bytes + exe.header_offset;
    uint32_t offset = 0, length = 0;
    if (exe.format == TB_EXECUTABLE_LX && exe.header_size == TB_LX_HEADER_SIZE)
    {
        offset = LX32(header, non_resident_name_table_offset);
        length = LX32(header, non_resident_name_table_len);
    }
    else if (exe.format == TB_EXECUTABLE_NE)
    {
        offset = tb_le32(header + offsetof(NeHeader, non_resident_name_table_offset));
        length = NE16(header, non_resident_name_table_size);
    }
    if (!offset || !length || !extent(size, offset, length))
        return NULL;
    size_t n = bytes[offset];
    if (!n || n + 3 > length || tb_le16(bytes + offset + n + 1))
        return NULL;
    const uint8_t *name = bytes + offset + 1;
    /* Module descriptions have no declared code page. Retain printable ASCII
       exactly; ambiguous encodings use the caller's filename fallback. */
    for (size_t i = 0; i < n; i++)
        if (name[i] < ' ' || name[i] > '~')
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
