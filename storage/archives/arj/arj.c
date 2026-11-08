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

/* Each header is the signature, a 16-bit basic header size (0 ends the archive), the
   basic header, its CRC-32, then extended headers each with a size and CRC-32. */
enum
{
    ARJ_SIGNATURE_BYTES = 2,
    ARJ_HEADER_SIZE = 2,
    ARJ_PREFIX_BYTES = 4,
    ARJ_CRC_BYTES = 4,
    ARJ_EXTENDED_SIZE_BYTES = 2,
    ARJ_EXTENDED_LIMIT = 10000,
    ARJ_BASIC_LIMIT = 2600,
    /* Basic header fields. */
    ARJ_FIRST_SIZE = 0, /* fixed part, followed by the name and comment */
    ARJ_VERSION = 1,
    ARJ_MIN_VERSION = 2,
    ARJ_HOST_OS = 3,
    ARJ_FLAGS = 4,
    ARJ_METHOD = 5,
    ARJ_FILE_TYPE = 6,
    ARJ_TIME = 8, /* DOS time and date, or Unix seconds; see ARJ_UNIX_TIME_* */
    ARJ_DATE = 10,
    ARJ_PACKED = 12,
    ARJ_SIZE = 16,
    ARJ_CRC = 20,
    ARJ_FIRST_MIN = 30,

    ARJ_FLAGS_UNSUPPORTED = 0x0d, /* garbled, volume continuation, extended file */
    ARJ_TYPE_MAIN = 2,
    ARJ_TYPE_DIRECTORY = 3,
    ARJ_TYPE_LAST = 3,
    ARJ_METHOD_STORED = 0,
    ARJ_METHOD_LZH_FIRST = 1,
    ARJ_METHOD_LZH_LAST = 3,
    ARJ_METHOD_FASTEST = 4,
    ARJ_METHOD_LAST = 4,
    /* Unix and NeXT hosts of these archiver versions store Unix modification times. */
    ARJ_HOST_UNIX = 2,
    ARJ_HOST_NEXT = 8,
    ARJ_UNIX_TIME_FIRST_VERSION = 11,
    ARJ_UNIX_TIME_LAST_VERSION = 49,
    /* ARJ32 archives (minimum version 51) use a 64 KB LZH dictionary. */
    ARJ_VERSION_ARJ32 = 51,
    ARJ_LZH_DICTIONARY_BITS = 15,
    ARJ32_LZH_DICTIONARY_BITS = 16,
    /* Version 1 archives encode method 4 lengths and distances with an extra bit. */
    ARJ_VERSION_OLD_FASTEST = 1,
    /* A self-extractor's archive starts within this many bytes. */
    ARJ_SFX_LIMIT = 2 * 1024 * 1024
};

typedef struct ArjHeader
{
    unsigned char basic[ARJ_BASIC_LIMIT];
    unsigned size;
    uint64_t data;
} ArjHeader;

static int arj_header(TbReadStream *file, uint64_t at, ArjHeader *h)
{
    unsigned char prefix[ARJ_PREFIX_BYTES], crc[ARJ_CRC_BYTES];
    if (!tb_stream_read_at(file, at, prefix, ARJ_PREFIX_BYTES) ||
        memcmp(prefix, TB_ARJ_SIGNATURE, ARJ_SIGNATURE_BYTES))
        return tb_archive_fail(EINVAL);
    h->size = tb_le16(prefix + ARJ_HEADER_SIZE);
    if (!h->size)
        return 2;
    if (h->size < ARJ_FIRST_MIN || h->size > sizeof h->basic ||
        !tb_stream_read_at(file, at + ARJ_PREFIX_BYTES, h->basic, h->size) ||
        !tb_stream_read_at(file, at + ARJ_PREFIX_BYTES + h->size, crc, ARJ_CRC_BYTES) ||
        tb_le32(crc) != (uint32_t)crc32(0, h->basic, h->size) ||
        h->basic[ARJ_FIRST_SIZE] < ARJ_FIRST_MIN || h->basic[ARJ_FIRST_SIZE] >= h->size ||
        !memchr(h->basic + h->basic[ARJ_FIRST_SIZE], 0, h->size - h->basic[ARJ_FIRST_SIZE]))
        return tb_archive_fail(EINVAL);
    h->data = at + ARJ_PREFIX_BYTES + ARJ_CRC_BYTES + h->size;
    for (unsigned n = 0; n < ARJ_EXTENDED_LIMIT; n++)
    {
        unsigned char extended[ARJ_EXTENDED_SIZE_BYTES];
        if (!tb_stream_read_at(file, h->data, extended, ARJ_EXTENDED_SIZE_BYTES))
            return tb_archive_fail(EINVAL);
        unsigned amount = tb_le16(extended);
        h->data += ARJ_EXTENDED_SIZE_BYTES;
        if (!amount)
            return 1;
        if (h->data > tb_stream_size(file) ||
            amount + ARJ_CRC_BYTES > tb_stream_size(file) - h->data)
            return tb_archive_fail(EINVAL);
        unsigned char *bytes = malloc(amount);
        int ok = bytes && tb_stream_read_at(file, h->data, bytes, amount) &&
                 tb_stream_read_at(file, h->data + amount, crc, ARJ_CRC_BYTES) &&
                 (uint32_t)crc32(0, bytes, amount) == tb_le32(crc);
        free(bytes);
        if (!ok)
            return tb_archive_fail(EINVAL);
        h->data += amount + ARJ_CRC_BYTES;
    }
    return tb_archive_fail(EFBIG);
}

static int arj_walk_at(TbReadStream *file, TbArchiveVisit visit, void *context, uint64_t at)
{
    ArjHeader h;
    if (arj_header(file, at, &h) != 1 || h.basic[ARJ_FILE_TYPE] != ARJ_TYPE_MAIN)
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
        uint32_t packed = tb_le32(b + ARJ_PACKED), size = tb_le32(b + ARJ_SIZE);
        if ((b[ARJ_FLAGS] & ARJ_FLAGS_UNSUPPORTED) || h.data > tb_stream_size(file) ||
            packed > tb_stream_size(file) - h.data || b[ARJ_METHOD] > ARJ_METHOD_LAST ||
            b[ARJ_FILE_TYPE] > ARJ_TYPE_LAST)
            return tb_archive_fail(ENOTSUP);
        char *name = tb_media_sanitise_member((char *)b + b[ARJ_FIRST_SIZE], 0);
        if (name && *name)
        {
            int unix_time = (b[ARJ_HOST_OS] == ARJ_HOST_UNIX || b[ARJ_HOST_OS] == ARJ_HOST_NEXT) &&
                            b[ARJ_VERSION] >= ARJ_UNIX_TIME_FIRST_VERSION &&
                            b[ARJ_VERSION] <= ARJ_UNIX_TIME_LAST_VERSION;
            int64_t modified = unix_time
                                   ? (int64_t)tb_le32(b + ARJ_TIME)
                                   : tb_dos_time(tb_le16(b + ARJ_DATE), tb_le16(b + ARJ_TIME));
            result = visit(context, name, size, b[ARJ_FILE_TYPE] == ARJ_TYPE_DIRECTORY, file, at,
                           packed, TB_ARCHIVE_ARJ + b[ARJ_METHOD], modified);
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
    size_t size =
        tb_stream_size(file) < ARJ_SFX_LIMIT ? (size_t)tb_stream_size(file) : ARJ_SFX_LIMIT;
    unsigned char *prefix = malloc(size ? size : 1);
    if (!prefix || !tb_stream_read_at(file, 0, prefix, size))
    {
        free(prefix);
        return 0;
    }
    /* The search starts after the executable's MZ signature. */
    for (size_t at = 2; at + ARJ_PREFIX_BYTES + ARJ_FIRST_MIN + ARJ_CRC_BYTES <= size; at++)
    {
        if (memcmp(prefix + at, TB_ARJ_SIGNATURE, ARJ_SIGNATURE_BYTES))
            continue;
        unsigned count = tb_le16(prefix + at + ARJ_HEADER_SIZE);
        const unsigned char *basic = prefix + at + ARJ_PREFIX_BYTES;
        if (count < ARJ_FIRST_MIN || count > ARJ_BASIC_LIMIT ||
            count + ARJ_PREFIX_BYTES + ARJ_CRC_BYTES > size - at ||
            basic[ARJ_FILE_TYPE] != ARJ_TYPE_MAIN ||
            (uint32_t)crc32(0, basic, count) != tb_le32(basic + count))
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
    if (arj_header(parent, offset, &header) != 1 || tb_le32(header.basic + ARJ_PACKED) != packed ||
        tb_le32(header.basic + ARJ_SIZE) != size)
        return NULL;
    uint32_t checksum = tb_le32(header.basic + ARJ_CRC);
    uint64_t data = header.data;
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(parent, data, input, (size_t)packed);
    if (ok && method == ARJ_METHOD_STORED)
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && method >= ARJ_METHOD_LZH_FIRST && method <= ARJ_METHOD_LZH_LAST)
        ok = header.basic[ARJ_MIN_VERSION] <= ARJ_VERSION_ARJ32 &&
             tb_lzh_decode(input, (size_t)packed, output, (size_t)size,
                           header.basic[ARJ_MIN_VERSION] == ARJ_VERSION_ARJ32
                               ? ARJ32_LZH_DICTIONARY_BITS
                               : ARJ_LZH_DICTIONARY_BITS);
    else if (ok && method == ARJ_METHOD_FASTEST)
        ok = arj_method4(input, (size_t)packed, output, (size_t)size,
                         header.basic[ARJ_VERSION] == ARJ_VERSION_OLD_FASTEST);
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
