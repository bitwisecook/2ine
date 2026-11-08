/* Public domain. No rights reserved. Wholly AI-authored. */
/* ARJ archives and self-extractors: checked main and file headers, and member decoding
   with the LZH (methods 1-3) and method 4 codecs.
   Format cross-check: Deark ARJ module (Jason Summers, BSD-2-Clause),
   c19b47c80a5ff8a7b41eb4f578e1afdda99202a2. No Deark source is linked. */
#include "storage/archives/container_internal.h"
#include "storage/archives/codecs/codecs.h"
#include "storage/common/file_time.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct ArjHeader
{
    unsigned char basic[2600];
    unsigned size;
    uint64_t data;
} ArjHeader;

static int arj_header(TbReadStream *file, uint64_t at, ArjHeader *h)
{
    unsigned char prefix[4], crc[4];
    if (!tb_stream_read_at(file, at, prefix, 4) || memcmp(prefix, "\x60\xea", 2))
        return tb_archive_fail(EINVAL);
    h->size = tb_le16(prefix + 2);
    if (!h->size)
        return 2;
    if (h->size < 30 || h->size > sizeof h->basic ||
        !tb_stream_read_at(file, at + 4, h->basic, h->size) ||
        !tb_stream_read_at(file, at + 4 + h->size, crc, 4) ||
        tb_le32(crc) != (uint32_t)crc32(0, h->basic, h->size) || h->basic[0] < 30 ||
        h->basic[0] >= h->size || !memchr(h->basic + h->basic[0], 0, h->size - h->basic[0]))
        return tb_archive_fail(EINVAL);
    h->data = at + 8 + h->size;
    for (unsigned n = 0; n < 10000; n++)
    {
        unsigned char extended[2];
        if (!tb_stream_read_at(file, h->data, extended, 2))
            return tb_archive_fail(EINVAL);
        unsigned amount = tb_le16(extended);
        h->data += 2;
        if (!amount)
            return 1;
        if (h->data > tb_stream_size(file) || amount + 4 > tb_stream_size(file) - h->data)
            return tb_archive_fail(EINVAL);
        unsigned char *bytes = malloc(amount);
        int ok = bytes && tb_stream_read_at(file, h->data, bytes, amount) &&
                 tb_stream_read_at(file, h->data + amount, crc, 4) &&
                 (uint32_t)crc32(0, bytes, amount) == tb_le32(crc);
        free(bytes);
        if (!ok)
            return tb_archive_fail(EINVAL);
        h->data += amount + 4;
    }
    return tb_archive_fail(EFBIG);
}

static int arj_walk_at(TbReadStream *file, TbArchiveVisit visit, void *context, uint64_t at)
{
    ArjHeader h;
    if (arj_header(file, at, &h) != 1 || h.basic[6] != 2)
        return tb_archive_fail(EINVAL);
    at = h.data;
    for (unsigned count = 0; count < TB_MEDIA_ENTRY_LIMIT; count++)
    {
        int result = arj_header(file, at, &h);
        if (result == 2)
            return 1;
        if (!result)
            return 0;
        unsigned char *b = h.basic;
        uint32_t packed = tb_le32(b + 12), size = tb_le32(b + 16);
        if ((b[4] & 0x0d) || h.data > tb_stream_size(file) ||
            packed > tb_stream_size(file) - h.data || b[5] > 4 || b[6] > 3)
            return tb_archive_fail(ENOTSUP);
        char *name = tb_media_sanitise_member((char *)b + b[0], 0);
        if (name && *name)
        {
            int64_t modified = ((b[3] == 2 || b[3] == 8) && b[1] >= 11 && b[1] <= 49)
                                   ? (int64_t)tb_le32(b + 8)
                                   : tb_dos_time(tb_le16(b + 10), tb_le16(b + 8));
            result = visit(context, name, size, b[6] == 3, file, at, packed, TB_ARCHIVE_ARJ + b[5],
                           modified);
            free(name);
            if (result != 1)
                return result;
        }
        else
            free(name);
        at = h.data + packed;
    }
    return tb_archive_fail(EFBIG);
}

int tb_arj_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    return arj_walk_at(file, visit, context, 0);
}

int tb_arj_sfx_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    size_t size = tb_stream_size(file) < 2u * 1024u * 1024u ? (size_t)tb_stream_size(file)
                                                            : 2u * 1024u * 1024u;
    unsigned char *prefix = malloc(size ? size : 1);
    if (!prefix || !tb_stream_read_at(file, 0, prefix, size))
    {
        free(prefix);
        return 0;
    }
    for (size_t at = 2; at + 38 <= size; at++)
    {
        if (prefix[at] != 0x60 || prefix[at + 1] != 0xea)
            continue;
        unsigned count = tb_le16(prefix + at + 2);
        if (count < 30 || count > 2600 || count + 8 > size - at || prefix[at + 10] != 2 ||
            (uint32_t)crc32(0, prefix + at + 4, count) != tb_le32(prefix + at + 4 + count))
            continue;
        free(prefix);
        return arj_walk_at(file, visit, context, at);
    }
    free(prefix);
    return tb_archive_fail(ENOTSUP);
}

static int arj_method4(const unsigned char *input, size_t packed, unsigned char *output,
                       size_t size, int old)
{
    TbBitReader bits = {input, packed, 0, 1};
    size_t at = 0;
    while (at < size && bits.valid)
    {
        unsigned ones = 0;
        while (ones < 7 && tb_bits_msb(&bits, 1))
            ones++;
        if (ones == 7 && old)
            tb_bits_msb(&bits, 1);
        unsigned length = ones ? (1u << ones) - 1 + tb_bits_msb(&bits, ones) : 0;
        if (!length)
        {
            unsigned byte = tb_bits_msb(&bits, 8);
            if (!bits.valid)
                break;
            output[at++] = (unsigned char)byte;
            continue;
        }
        ones = 0;
        while (ones < 4 && tb_bits_msb(&bits, 1))
            ones++;
        if (ones == 4 && old)
            tb_bits_msb(&bits, 1);
        unsigned distance = (1u << (9 + ones)) - 511 + tb_bits_msb(&bits, 9 + ones);
        length += 2;
        if (!bits.valid || distance > at || length > size - at)
            return 0;
        for (unsigned i = 0; i < length; i++, at++)
            output[at] = output[at - distance];
    }
    return bits.valid && at == size;
}

TbReadStream *tb_arj_member(TbReadStream *parent, uint64_t offset, uint64_t packed, uint64_t size,
                            TbArchiveKind kind)
{
    if (packed > TB_LEGACY_ARCHIVE_LIMIT || size > TB_LEGACY_ARCHIVE_LIMIT)
    {
        tb_archive_fail(EFBIG);
        return NULL;
    }
    unsigned method = (unsigned)(kind - TB_ARCHIVE_ARJ);
    ArjHeader header;
    if (arj_header(parent, offset, &header) != 1 || tb_le32(header.basic + 12) != packed ||
        tb_le32(header.basic + 16) != size)
        return NULL;
    uint32_t checksum = tb_le32(header.basic + 20);
    uint64_t data = header.data;
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(parent, data, input, (size_t)packed);
    if (ok && !method)
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && method >= 1 && method <= 3)
        ok = header.basic[2] <= 51 && tb_lzh_decode(input, (size_t)packed, output, (size_t)size,
                                                    header.basic[2] == 51 ? 16 : 15);
    else if (ok && method == 4)
        ok = arj_method4(input, (size_t)packed, output, (size_t)size, header.basic[1] == 1);
    else
    {
        ok = 0;
        tb_archive_fail(ENOTSUP);
    }
    free(input);
    if (ok)
        ok = (uint32_t)crc32(0, output, (uInt)size) == checksum;

    return tb_legacy_member_result(output, size, ok);
}
