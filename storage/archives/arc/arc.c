/* Public domain. No rights reserved. Wholly AI-authored. */
/* ARC archives: checked member framing, and member decoding with RLE90, squeeze and
   Unix-compress (methods 8 and 9) codecs. ARC LZW streams use libarchive's codec.
   Format cross-check: Deark ARC module (Jason Summers, BSD-2-Clause),
   c19b47c80a5ff8a7b41eb4f578e1afdda99202a2. No Deark source is linked. */
#include "storage/archives/container_internal.h"
#include "storage/archives/codecs/codecs.h"
#include "storage/common/file_time.h"
#include "storage/vendor/libarchive/archive.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>

int tb_arc_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    uint64_t at = 0, length = tb_stream_size(file);
    for (unsigned count = 0; count < TB_MEDIA_ENTRY_LIMIT; count++)
    {
        unsigned char h[29];
        if (!tb_stream_read_at(file, at, h, 2) || h[0] != 0x1a)
            return tb_archive_fail(EINVAL);
        unsigned method = h[1];
        if (!method)
            return 1;
        size_t header = method == 1 ? 25 : 29;
        if (method > 9 || !tb_stream_read_at(file, at, h, header))
            return tb_archive_fail(ENOTSUP);
        uint32_t packed = tb_le32(h + 15), size = method == 1 ? packed : tb_le32(h + 25);
        if (at > length || header > length - at || packed > length - at - header ||
            !memchr(h + 2, 0, 13))
            return tb_archive_fail(EINVAL);
        char *name = tb_media_sanitise_member((char *)h + 2, 0);
        if (name && *name)
        {
            int result = visit(context, name, size, 0, file, at, packed, TB_ARCHIVE_ARC + method,
                               tb_dos_time(tb_le16(h + 19), tb_le16(h + 21)));
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
    unsigned char arc[29] = {0};
    size_t bytes = method == 1 ? 25 : 29;
    if (!tb_stream_read_at(parent, offset, arc, bytes))
        return NULL;
    uint32_t checksum = tb_le16(arc + 23);
    uint64_t data = offset + bytes;
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(parent, data, input, (size_t)packed);
    if (ok && method <= 2)
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && method == 3)
        ok = tb_rle90_decode(input, (size_t)packed, output, (size_t)size);
    else if (ok && (method == 4 || method == 8 || method == 9))
    {
        size_t capacity = method == 9 ? (size_t)size : (size_t)size * 2 + 1, used = 0;
        unsigned char *plain = malloc(capacity ? capacity : 1);
        ok = plain != NULL;
        if (ok && method == 4)
            ok = tb_squeeze_decode(input, (size_t)packed, plain, capacity, &used);
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
                ok = arc_compress_frame(frame, 3 + (size_t)packed - skip, plain, capacity, &used);
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
