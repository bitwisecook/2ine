/* Public domain. No rights reserved. Wholly AI-authored. */
/* ARC archives: checked member framing, and member decoding with RLE90, squeeze and
   Unix-compress (methods 8 and 9) codecs. ARC LZW streams use libarchive's codec.
   Format cross-check: Deark ARC module (Jason Summers, BSD-2-Clause),
   c19b47c80a5ff8a7b41eb4f578e1afdda99202a2. No Deark source is linked. */
#include "storage/archives/container_internal.h"
#include "storage/archives/codecs/codecs.h"
#include "storage/common/file_time.h"
#include "third_party/libarchive/archive.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>

/* Member header: TB_ARC_MARK, the method, then fields; method 1 omits the unpacked size. */
enum
{
    ARC_METHOD = 1,
    ARC_NAME = 2,
    ARC_NAME_BYTES = 13,
    ARC_PACKED = 15,
    ARC_DATE = 19,
    ARC_TIME = 21,
    ARC_CRC = 23,
    ARC_SIZE = 25,
    ARC_MARK_BYTES = 2, /* mark and method */
    ARC_OLD_HEADER_BYTES = 25,
    ARC_HEADER_BYTES = 29,

    ARC_METHOD_END = 0,
    ARC_METHOD_OLD_STORED = 1,
    ARC_METHOD_STORED = 2,
    ARC_METHOD_PACKED = 3,   /* RLE90 */
    ARC_METHOD_SQUEEZED = 4, /* Huffman, then RLE90 */
    ARC_METHOD_CRUNCHED = 8, /* Unix compress with a maximum-bits byte, then RLE90 */
    ARC_METHOD_SQUASHED = 9, /* Unix compress, 13 bits */
    ARC_METHOD_LAST = 9
};

/* Unix compress (.Z) stream header: two magic bytes, then block mode and maximum bits. */
enum
{
    COMPRESS_MAGIC0 = 0x1f,
    COMPRESS_MAGIC1 = 0x9d,
    COMPRESS_BLOCK_MODE = 0x80,
    COMPRESS_HEADER_BYTES = 3,
    COMPRESS_BITS_MASK = 31,
    COMPRESS_MIN_BITS = 9,
    COMPRESS_MAX_BITS = 16,
    ARC_SQUASHED_BITS = 13
};

int tb_arc_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    uint64_t at = 0, length = tb_stream_size(file);
    for (unsigned count = 0; count < TB_MEDIA_ENTRY_LIMIT; count++)
    {
        unsigned char h[ARC_HEADER_BYTES];
        if (!tb_stream_read_at(file, at, h, ARC_MARK_BYTES) || h[0] != TB_ARC_MARK)
            return tb_archive_fail(EINVAL);
        unsigned method = h[ARC_METHOD];
        if (method == ARC_METHOD_END)
            return 1;
        size_t header = method == ARC_METHOD_OLD_STORED ? ARC_OLD_HEADER_BYTES : ARC_HEADER_BYTES;
        if (method > ARC_METHOD_LAST || !tb_stream_read_at(file, at, h, header))
            return tb_archive_fail(ENOTSUP);
        uint32_t packed = tb_le32(h + ARC_PACKED),
                 size = method == ARC_METHOD_OLD_STORED ? packed : tb_le32(h + ARC_SIZE);
        if (at > length || header > length - at || packed > length - at - header ||
            !memchr(h + ARC_NAME, 0, ARC_NAME_BYTES))
            return tb_archive_fail(EINVAL);
        char *name = tb_media_sanitise_member((char *)h + ARC_NAME, 0);
        if (name && *name)
        {
            int result = visit(context, name, size, 0, file, at, packed, TB_ARCHIVE_ARC + method,
                               tb_dos_time(tb_le16(h + ARC_DATE), tb_le16(h + ARC_TIME)));
            free(name);
            if (result != 1)
                return result;
        }
        else
            free(name);
        at += header + packed;
    }
    return tb_archive_fail(EFBIG);
}

/* Decodes an ARC Unix-compress frame; the caller verifies the ARC CRC. */
static int arc_compress_frame(unsigned char *frame, size_t length, unsigned char *output,
                              size_t capacity, size_t *used)
{
    TbReadStream *stream = tb_stream_memory(frame, length);
    if (!stream)
        return 0;
    TbArchiveInput in = {.file = stream, .length = length};
    struct archive *a;
    struct archive_entry *entry;
    int ok = tb_libarchive_open(&a, &in);
    if (!a)
    {
        tb_stream_close(stream);
        return 0;
    }
    ok = ok && archive_read_next_header(a, &entry) == ARCHIVE_OK;
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

TbReadStream *tb_arc_member(TbReadStream *parent, uint64_t offset, uint64_t packed, uint64_t size,
                            TbArchiveKind kind)
{
    if (packed > TB_LEGACY_ARCHIVE_LIMIT || size > TB_LEGACY_ARCHIVE_LIMIT)
    {
        tb_archive_fail(EFBIG);
        return NULL;
    }
    unsigned method = (unsigned)(kind - TB_ARCHIVE_ARC);
    unsigned char arc[ARC_HEADER_BYTES] = {0};
    size_t bytes = method == ARC_METHOD_OLD_STORED ? ARC_OLD_HEADER_BYTES : ARC_HEADER_BYTES;
    if (!tb_stream_read_at(parent, offset, arc, bytes))
        return NULL;
    uint32_t checksum = tb_le16(arc + ARC_CRC);
    uint64_t data = offset + bytes;
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(parent, data, input, (size_t)packed);
    if (ok && method <= ARC_METHOD_STORED)
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && method == ARC_METHOD_PACKED)
        ok = tb_rle90_decode(input, (size_t)packed, output, (size_t)size);
    else if (ok && (method == ARC_METHOD_SQUEEZED || method == ARC_METHOD_CRUNCHED ||
                    method == ARC_METHOD_SQUASHED))
    {
        /* An RLE90 encoding is at most twice the size it decodes to. */
        size_t capacity = method == ARC_METHOD_SQUASHED ? (size_t)size : (size_t)size * 2 + 1,
               used = 0;
        unsigned char *plain = malloc(capacity ? capacity : 1);
        ok = plain != NULL;
        if (ok && method == ARC_METHOD_SQUEEZED)
            ok = tb_squeeze_decode(input, (size_t)packed, plain, capacity, &used);
        else if (ok)
        {
            size_t skip = method == ARC_METHOD_CRUNCHED ? 1 : 0;
            unsigned maxbits = method == ARC_METHOD_CRUNCHED && packed
                                   ? input[0] & COMPRESS_BITS_MASK
                                   : ARC_SQUASHED_BITS;
            unsigned char *frame =
                packed >= skip ? malloc(COMPRESS_HEADER_BYTES + (size_t)packed - skip) : NULL;
            if (!frame || maxbits < COMPRESS_MIN_BITS || maxbits > COMPRESS_MAX_BITS)
                ok = 0;
            else
            {
                frame[0] = COMPRESS_MAGIC0;
                frame[1] = COMPRESS_MAGIC1;
                frame[2] = (unsigned char)(COMPRESS_BLOCK_MODE | maxbits);
                memcpy(frame + COMPRESS_HEADER_BYTES, input + skip, (size_t)packed - skip);
                ok = arc_compress_frame(frame, COMPRESS_HEADER_BYTES + (size_t)packed - skip, plain,
                                        capacity, &used);
                frame = NULL; /* Reader owns it. */
            }
            free(frame);
        }
        if (ok && method == ARC_METHOD_SQUASHED)
        {
            ok = used == size;
            if (ok)
                memcpy(output, plain, used);
        }
        else if (ok)
            ok = tb_rle90_decode(plain, used, output, (size_t)size);
        free(plain);
    }
    else
    {
        ok = 0;
        tb_archive_fail(ENOTSUP);
    }
    free(input);
    if (ok)
        ok = tb_crc16_arc(output, (size_t)size) == checksum;
    return tb_legacy_member_result(output, size, ok);
}
