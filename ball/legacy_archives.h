/* Public domain. No rights reserved. Wholly AI-authored. */
/* Checked ARC/ARJ framing. ARC Unix-LZW streams use libarchive's codec.
   Format cross-check: Deark ARC/ARJ modules (Jason Summers, BSD-2-Clause),
   c19b47c80a5ff8a7b41eb4f578e1afdda99202a2. No Deark source is linked. */
#include <zlib.h>
#include <limits.h>
#define LEGACY_ARCHIVE_LIMIT (16u * 1024u * 1024u)
static uint16_t legacy_crc16(const unsigned char *data, size_t size)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
    }
    return crc;
}
static int legacy_arc_walk(TbReadStream *file, Visit visit, void *context)
{
    uint64_t at = 0, length = tb_stream_size(file);
    for (unsigned count = 0; count < MEDIA_ENTRY_LIMIT; count++)
    {
        unsigned char h[29];
        if (!tb_stream_read_at(file, at, h, 2) || h[0] != 0x1a)
            return bad(EINVAL);
        unsigned method = h[1];
        if (!method)
            return 1;
        size_t header = method == 1 ? 25 : 29;
        if (method > 9 || !tb_stream_read_at(file, at, h, header))
            return bad(ENOTSUP);
        uint32_t packed = u32(h + 15), size = method == 1 ? packed : u32(h + 25);
        if (at > length || header > length - at || packed > length - at - header ||
            !memchr(h + 2, 0, 13))
            return bad(EINVAL);
        char *name = member_name((char *)h + 2, 0);
        if (name && *name)
        {
            int result = visit(context, name, size, 0, file, at, packed, 32 + (int)method,
                               tb_dos_time(u16(h + 19), u16(h + 21)));
            free(name);
            if (result != 1)
                return result;
        }
        else
            free(name);
        at += header + packed;
    }
    return bad(EFBIG);
}
typedef struct LegacyArjHeader
{
    unsigned char basic[2600];
    unsigned size;
    uint64_t data;
} LegacyArjHeader;
static int legacy_arj_header(TbReadStream *file, uint64_t at, LegacyArjHeader *h)
{
    unsigned char prefix[4], crc[4];
    if (!tb_stream_read_at(file, at, prefix, 4) || memcmp(prefix, "\x60\xea", 2))
        return bad(EINVAL);
    h->size = u16(prefix + 2);
    if (!h->size)
        return 2;
    if (h->size < 30 || h->size > sizeof h->basic ||
        !tb_stream_read_at(file, at + 4, h->basic, h->size) ||
        !tb_stream_read_at(file, at + 4 + h->size, crc, 4) ||
        u32(crc) != (uint32_t)crc32(0, h->basic, h->size) || h->basic[0] < 30 ||
        h->basic[0] >= h->size || !memchr(h->basic + h->basic[0], 0, h->size - h->basic[0]))
        return bad(EINVAL);
    h->data = at + 8 + h->size;
    for (unsigned n = 0; n < 10000; n++)
    {
        unsigned char extended[2];
        if (!tb_stream_read_at(file, h->data, extended, 2))
            return bad(EINVAL);
        unsigned amount = u16(extended);
        h->data += 2;
        if (!amount)
            return 1;
        if (h->data > tb_stream_size(file) || amount + 4 > tb_stream_size(file) - h->data)
            return bad(EINVAL);
        unsigned char *bytes = malloc(amount);
        int ok = bytes && tb_stream_read_at(file, h->data, bytes, amount) &&
                 tb_stream_read_at(file, h->data + amount, crc, 4) &&
                 (uint32_t)crc32(0, bytes, amount) == u32(crc);
        free(bytes);
        if (!ok)
            return bad(EINVAL);
        h->data += amount + 4;
    }
    return bad(EFBIG);
}
static int legacy_arj_walk_at(TbReadStream *file, Visit visit, void *context, uint64_t at)
{
    LegacyArjHeader h;
    if (legacy_arj_header(file, at, &h) != 1 || h.basic[6] != 2)
        return bad(EINVAL);
    at = h.data;
    for (unsigned count = 0; count < MEDIA_ENTRY_LIMIT; count++)
    {
        int result = legacy_arj_header(file, at, &h);
        if (result == 2)
            return 1;
        if (!result)
            return 0;
        unsigned char *b = h.basic;
        uint32_t packed = u32(b + 12), size = u32(b + 16);
        if ((b[4] & 0x0d) || h.data > tb_stream_size(file) ||
            packed > tb_stream_size(file) - h.data || b[5] > 4 || b[6] > 3)
            return bad(ENOTSUP);
        char *name = member_name((char *)b + b[0], 0);
        if (name && *name)
        {
            int64_t modified = ((b[3] == 2 || b[3] == 8) && b[1] >= 11 && b[1] <= 49)
                                   ? (int64_t)u32(b + 8)
                                   : tb_dos_time(u16(b + 10), u16(b + 8));
            result = visit(context, name, size, b[6] == 3, file, at, packed, 64 + b[5], modified);
            free(name);
            if (result != 1)
                return result;
        }
        else
            free(name);
        at = h.data + packed;
    }
    return bad(EFBIG);
}
static int legacy_arj_walk(TbReadStream *file, Visit visit, void *context)
{
    return legacy_arj_walk_at(file, visit, context, 0);
}
static int legacy_arj_sfx(TbReadStream *file, Visit visit, void *context)
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
        unsigned count = u16(prefix + at + 2);
        if (count < 30 || count > 2600 || count + 8 > size - at || prefix[at + 10] != 2 ||
            (uint32_t)crc32(0, prefix + at + 4, count) != u32(prefix + at + 4 + count))
            continue;
        free(prefix);
        return legacy_arj_walk_at(file, visit, context, at);
    }
    free(prefix);
    return bad(ENOTSUP);
}
/* Decode an ARC Unix-compress frame, then verify the original ARC CRC. */
static int legacy_codec_frame(unsigned char *frame, size_t length, unsigned char *output,
                              size_t capacity, size_t *used)
{
    TbReadStream *stream = tb_stream_memory(frame, length);
    if (!stream)
        return 0;
    struct archive *a = archive_read_new();
    if (!a)
    {
        tb_stream_close(stream);
        return 0;
    }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a);
    ArchiveInput in = {.file = stream, .length = length};
    archive_read_set_seek_callback(a, archive_input_seek);
    struct archive_entry *entry;
    int ok = archive_read_open2(a, &in, NULL, archive_input_read, archive_input_skip, NULL) ==
                 ARCHIVE_OK &&
             archive_read_next_header(a, &entry) == ARCHIVE_OK;
    *used = 0;
    while (ok && *used < capacity)
    {
        la_ssize_t count = archive_read_data(a, output + *used, capacity - *used);
        if (count == 0)
            break;
        if (count < 0)
        {
            ok = 0;
            break;
        }
        *used += (size_t)count;
    }
    if (ok && *used == capacity)
    {
        unsigned char extra;
        ok = archive_read_data(a, &extra, 1) == 0;
    }
    archive_read_free(a);
    tb_stream_close(stream);
    return ok;
}
static int legacy_rle90(const unsigned char *input, size_t length, unsigned char *output,
                        size_t expected)
{
    size_t used = 0;
    for (size_t at = 0; at < length; at++)
    {
        unsigned byte = input[at];
        if (byte != 0x90)
        {
            if (used == expected)
                return 0;
            output[used++] = (unsigned char)byte;
            continue;
        }
        if (++at == length)
            return 0;
        unsigned repeat = input[at];
        if (!repeat)
        {
            if (used == expected)
                return 0;
            output[used++] = 0x90;
        }
        else
        {
            if (!used || repeat - 1 > expected - used)
                return 0;
            memset(output + used, output[used - 1], repeat - 1);
            used += repeat - 1;
        }
    }
    return used == expected;
}
typedef struct LegacyBits
{
    const unsigned char *data;
    size_t size, bit;
    int valid;
} LegacyBits;
static unsigned legacy_bits(LegacyBits *bits, unsigned count)
{
    if (!bits->valid || count > 24 || bits->bit > bits->size * 8 ||
        count > bits->size * 8 - bits->bit)
    {
        bits->valid = 0;
        return 0;
    }
    unsigned value = 0;
    for (unsigned i = 0; i < count; i++, bits->bit++)
        value = (value << 1) | ((bits->data[bits->bit / 8] >> (7 - bits->bit % 8)) & 1);
    return value;
}
static int legacy_arj4(const unsigned char *input, size_t packed, unsigned char *output,
                       size_t size, int old)
{
    LegacyBits bits = {input, packed, 0, 1};
    size_t at = 0;
    while (at < size && bits.valid)
    {
        unsigned ones = 0;
        while (ones < 7 && legacy_bits(&bits, 1))
            ones++;
        if (ones == 7 && old)
            legacy_bits(&bits, 1);
        unsigned length = ones ? (1u << ones) - 1 + legacy_bits(&bits, ones) : 0;
        if (!length)
        {
            unsigned byte = legacy_bits(&bits, 8);
            if (!bits.valid)
                break;
            output[at++] = (unsigned char)byte;
            continue;
        }
        ones = 0;
        while (ones < 4 && legacy_bits(&bits, 1))
            ones++;
        if (ones == 4 && old)
            legacy_bits(&bits, 1);
        unsigned distance = (1u << (9 + ones)) - 511 + legacy_bits(&bits, 9 + ones);
        length += 2;
        if (!bits.valid || distance > at || length > size - at)
            return 0;
        for (unsigned i = 0; i < length; i++, at++)
            output[at] = output[at - distance];
    }
    return bits.valid && at == size;
}
/* ARJ methods 1–3 use canonical LZH blocks, with a zero block count
   representing 65536 symbols rather than an end marker. */
typedef struct LegacyHuffman
{
    unsigned counts[17], symbols[510];
    int single;
} LegacyHuffman;
static int legacy_huffman(LegacyHuffman *tree, const unsigned char *lengths, unsigned count)
{
    memset(tree, 0, sizeof *tree);
    tree->single = -1;
    for (unsigned i = 0; i < count; i++)
    {
        if (lengths[i] > 16)
            return 0;
        tree->counts[lengths[i]]++;
    }
    tree->counts[0] = 0;
    int available = 1;
    unsigned positions[17] = {0};
    for (unsigned length = 1; length <= 16; length++)
    {
        available = available * 2 - (int)tree->counts[length];
        if (available < 0)
            return 0;
        if (length > 1)
            positions[length] = positions[length - 1] + tree->counts[length - 1];
    }
    for (unsigned i = 0; i < count; i++)
        if (lengths[i])
            tree->symbols[positions[lengths[i]]++] = i;
    return 1;
}
static int legacy_symbol(LegacyBits *bits, const LegacyHuffman *tree)
{
    if (tree->single >= 0)
        return tree->single;
    unsigned code = 0, first = 0, index = 0;
    for (unsigned length = 1; length <= 16; length++)
    {
        code = (code << 1) | legacy_bits(bits, 1);
        if (!bits->valid)
            return -1;
        if (code >= first && code - first < tree->counts[length])
            return (int)tree->symbols[index + code - first];
        index += tree->counts[length];
        first = (first + tree->counts[length]) << 1;
    }
    bits->valid = 0;
    return -1;
}
static int legacy_pt_tree(LegacyBits *bits, LegacyHuffman *tree, unsigned alphabet, unsigned width,
                          unsigned special)
{
    unsigned n = legacy_bits(bits, width);
    if (!n)
    {
        tree->single = (int)legacy_bits(bits, width);
        return bits->valid && (unsigned)tree->single < alphabet;
    }
    if (n > alphabet)
        return 0;
    unsigned char lengths[19] = {0};
    unsigned at = 0;
    while (at < n && bits->valid)
    {
        unsigned length = legacy_bits(bits, 3);
        if (length == 7)
            while (legacy_bits(bits, 1))
                if (++length > 16)
                    return 0;
        lengths[at++] = (unsigned char)length;
        if (at == special)
        {
            unsigned zeros = legacy_bits(bits, 2);
            if (zeros > alphabet - at)
                return 0;
            at += zeros;
        }
    }
    return bits->valid && legacy_huffman(tree, lengths, alphabet);
}
static int legacy_literal_tree(LegacyBits *bits, LegacyHuffman *tree,
                               const LegacyHuffman *length_tree)
{
    unsigned n = legacy_bits(bits, 9);
    if (!n)
    {
        tree->single = (int)legacy_bits(bits, 9);
        return bits->valid && tree->single < 510;
    }
    if (n > 510)
        return 0;
    unsigned char lengths[510] = {0};
    unsigned at = 0;
    while (at < n && bits->valid)
    {
        int code = legacy_symbol(bits, length_tree);
        if (code < 0)
            return 0;
        if (code <= 2)
        {
            unsigned zeros = code == 0   ? 1
                             : code == 1 ? legacy_bits(bits, 4) + 3
                                         : legacy_bits(bits, 9) + 20;
            if (zeros > n - at)
                return 0;
            at += zeros;
        }
        else
        {
            if (code > 18)
                return 0;
            lengths[at++] = (unsigned char)(code - 2);
        }
    }
    return bits->valid && legacy_huffman(tree, lengths, 510);
}
static int legacy_arj_lzh(const unsigned char *input, size_t packed, unsigned char *output,
                          size_t expected, unsigned dictionary_bits)
{
    LegacyBits bits = {input, packed, 0, 1};
    LegacyHuffman lengths, literals, distances;
    unsigned remaining = 0;
    size_t at = 0;
    while (at < expected && bits.valid)
    {
        if (!remaining)
        {
            remaining = legacy_bits(&bits, 16);
            if (!remaining)
                remaining = 65536;
            if (!legacy_pt_tree(&bits, &lengths, 19, 5, 3) ||
                !legacy_literal_tree(&bits, &literals, &lengths) ||
                !legacy_pt_tree(&bits, &distances, dictionary_bits + 1,
                                dictionary_bits <= 13 ? 4 : 5, UINT_MAX))
                return 0;
        }
        remaining--;
        int code = legacy_symbol(&bits, &literals);
        if (code < 0 || !bits.valid)
            return 0;
        if (code < 256)
            output[at++] = (unsigned char)code;
        else
        {
            unsigned length = (unsigned)code - 253;
            int symbol = legacy_symbol(&bits, &distances);
            if (symbol < 0 || symbol > (int)dictionary_bits)
                return 0;
            unsigned distance =
                symbol ? (1u << (symbol - 1)) + legacy_bits(&bits, (unsigned)symbol - 1) : 0;
            distance++;
            if (!bits.valid || distance > at || length > expected - at)
                return 0;
            for (unsigned i = 0; i < length; i++, at++)
                output[at] = output[at - distance];
        }
    }
    return bits.valid && at == expected;
}
static int legacy_squeeze(const unsigned char *input, size_t packed, unsigned char *output,
                          size_t capacity, size_t *used)
{
    if (packed < 2)
        return 0;
    unsigned count = u16(input);
    if (!count || count > 256 || 2 + count * 4 > packed)
        return 0;
    size_t bit = (2 + count * 4) * 8;
    *used = 0;
    for (;;)
    {
        unsigned node = 0;
        int value;
        do
        {
            if (node >= count || bit >= packed * 8)
                return 0;
            unsigned direction = (input[bit / 8] >> (bit % 8)) & 1;
            bit++;
            value = (int16_t)u16(input + 2 + node * 4 + direction * 2);
            node = (unsigned)value;
        } while (value >= 0);
        value = -value - 1;
        if (value == 256)
            return 1;
        if (value > 255 || *used == capacity)
            return 0;
        output[(*used)++] = (unsigned char)value;
    }
}
static TbReadStream *legacy_archive_member(TbReadStream *parent, uint64_t offset, uint64_t packed,
                                           uint64_t size, int kind)
{
    if (packed > LEGACY_ARCHIVE_LIMIT || size > LEGACY_ARCHIVE_LIMIT)
    {
        bad(EFBIG);
        return NULL;
    }
    int arj = kind >= 64;
    unsigned method = (unsigned)(kind - (arj ? 64 : 32));
    uint32_t checksum;
    unsigned char arc[29] = {0};
    LegacyArjHeader header;
    uint64_t data;
    if (arj)
    {
        if (legacy_arj_header(parent, offset, &header) != 1 || u32(header.basic + 12) != packed ||
            u32(header.basic + 16) != size)
            return NULL;
        checksum = u32(header.basic + 20);
        data = header.data;
    }
    else
    {
        size_t bytes = method == 1 ? 25 : 29;
        if (!tb_stream_read_at(parent, offset, arc, bytes))
            return NULL;
        checksum = u16(arc + 23);
        data = offset + bytes;
    }
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(parent, data, input, (size_t)packed);
    if (ok && ((!arj && method <= 2) || (arj && !method)))
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && !arj && method == 3)
        ok = legacy_rle90(input, (size_t)packed, output, (size_t)size);
    else if (ok && !arj && (method == 4 || method == 8 || method == 9))
    {
        size_t capacity = method == 9 ? (size_t)size : (size_t)size * 2 + 1, used = 0;
        unsigned char *plain = malloc(capacity ? capacity : 1);
        ok = plain != NULL;
        if (ok && method == 4)
            ok = legacy_squeeze(input, (size_t)packed, plain, capacity, &used);
        else if (ok)
        {
            size_t skip = method == 8 ? 1 : 0;
            unsigned maxbits = method == 8 && packed ? input[0] & 31 : 13;
            unsigned char *frame = packed >= skip ? malloc(3 + (size_t)packed - skip) : NULL;
            if (!frame || maxbits < 9 || maxbits > 16)
                ok = 0;
            else
            {
                frame[0] = 0x1f;
                frame[1] = 0x9d;
                frame[2] = (unsigned char)(0x80 | maxbits);
                memcpy(frame + 3, input + skip, (size_t)packed - skip);
                ok = legacy_codec_frame(frame, 3 + (size_t)packed - skip, plain, capacity, &used);
                frame = NULL; /* Reader owns it. */
            }
            free(frame);
        }
        if (ok && method == 9)
        {
            ok = used == size;
            if (ok)
                memcpy(output, plain, used);
        }
        else if (ok)
            ok = legacy_rle90(plain, used, output, (size_t)size);
        free(plain);
    }
    else if (ok && arj && method >= 1 && method <= 3)
        ok = header.basic[2] <= 51 && legacy_arj_lzh(input, (size_t)packed, output, (size_t)size,
                                                     header.basic[2] == 51 ? 16 : 15);
    else if (ok && arj && method == 4)
        ok = legacy_arj4(input, (size_t)packed, output, (size_t)size, header.basic[1] == 1);
    else
    {
        ok = 0;
        bad(ENOTSUP);
    }
    free(input);
    if (ok)
        ok = arj ? (uint32_t)crc32(0, output, (uInt)size) == checksum
                 : legacy_crc16(output, (size_t)size) == checksum;
    if (!ok)
    {
        free(output);
        if (errno != ENOTSUP)
            bad(EIO);
        return NULL;
    }
    media_session.stats.decompressed_bytes += size;
    return tb_stream_memory(output, (size_t)size);
}
