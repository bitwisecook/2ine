/* Public domain. No rights reserved. Wholly AI-authored. */
/* IBM PACK and PACK2 (FTCOMP) files: the A5 96 directory walk and member decoding. */
#include "storage/archives/container_internal.h"
#include "storage/archives/ibm_pack/decode.h"
#include "storage/common/file_time.h"
#include "formats/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK2_SIGNATURE "FTCOMP"

/* Each member starts with a header; PACK2 headers add an FTCOMP tag. */
enum
{
    PACK_SIGNATURE_BYTES = 2,
    PACK_TYPE = 2, /* PACK_TYPE_* */
    PACK_DATE = 4,
    PACK_TIME = 6,
    PACK_EA = 12,   /* extended attributes after the data, 0 when absent */
    PACK_SIZE = 16, /* unpacked size */
    PACK_NEXT = 20, /* next header, 0 for the last member */
    PACK_PREFIX_BYTES = 24,
    PACK_HEADER_BYTES = 26, /* ends with the 16-bit name length */
    PACK2_TAG = 24,
    PACK2_TAG_BYTES = 6,
    PACK2_HEADER_BYTES = 41,
    PACK_NAME_LIMIT = 4096,
    PACK_TYPE_PACK2 = 0xfffd,
    PACK_TYPE_PACK = 0xfffe,
    /* FTCOMP's stored form is a four-byte marker followed by raw bytes. */
    PACK2_STORED_MARKER_BYTES = 4,
    FTCOMP_SIGNATURE_BYTES = 4,
    PACK_MEMBER_LIMIT = 16 * 1024 * 1024
};

/* The callback may request one member; extraction never uses archive paths as host paths. */
int tb_ibm_pack_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    if (tb_stream_seek(file, 0, SEEK_END))
        return 0;
    int64_t length = tb_stream_tell(file);
    uint64_t at = 0;
    for (unsigned entries = 0; entries < TB_MEDIA_ENTRY_LIMIT; entries++)
    {
        unsigned char h[PACK2_HEADER_BYTES];
        if (at > (uint64_t)length || tb_stream_seek(file, (int64_t)at, SEEK_SET) ||
            tb_stream_read(h, 1, PACK_PREFIX_BYTES, file) != PACK_PREFIX_BYTES ||
            memcmp(h, TB_IBM_PACK_SIGNATURE, PACK_SIGNATURE_BYTES))
            return tb_archive_fail(EINVAL);
        unsigned kind = tb_le16(h + PACK_TYPE);
        uint64_t end = tb_le32(h + PACK_NEXT);
        if (!end)
            end = (uint64_t)length;
        if (end <= at || end > (uint64_t)length)
            return tb_archive_fail(EINVAL);
        unsigned header = kind == PACK_TYPE_PACK2 ? PACK2_HEADER_BYTES : PACK_HEADER_BYTES;
        if (kind != PACK_TYPE_PACK2 && kind != PACK_TYPE_PACK)
            return tb_archive_fail(ENOTSUP);
        if (tb_stream_read(h + PACK_PREFIX_BYTES, 1, header - PACK_PREFIX_BYTES, file) !=
            header - PACK_PREFIX_BYTES)
            return tb_archive_fail(EIO);
        if (kind == PACK_TYPE_PACK2 && memcmp(h + PACK2_TAG, PACK2_SIGNATURE, PACK2_TAG_BYTES))
            return tb_archive_fail(EINVAL);
        unsigned n = tb_le16(h + header - 2);
        if (!n || n > PACK_NAME_LIMIT || at + header + n > end)
            return tb_archive_fail(EINVAL);
        char *raw = calloc(1, n + 1);
        if (!raw)
            return 0;
        if (tb_stream_read(raw, 1, n, file) != n)
        {
            free(raw);
            return tb_archive_fail(EIO);
        }
        char *name = tb_media_sanitise_member(raw, 1);
        free(raw);
        uint64_t data = at + header + n, ea = tb_le32(h + PACK_EA), data_end = ea ? ea : end;
        if (data_end < data || data_end > end)
        {
            free(name);
            return tb_archive_fail(EINVAL);
        }
        if (name && *name)
        {
            int result =
                visit(context, name, tb_le32(h + PACK_SIZE), 0, file, data, data_end - data,
                      kind == PACK_TYPE_PACK2 ? TB_ARCHIVE_IBM_PACK2 : TB_ARCHIVE_IBM_PACK,
                      tb_dos_time(tb_le16(h + PACK_DATE), tb_le16(h + PACK_TIME)));
            free(name);
            if (result != 1)
                return result;
        }
        else
            free(name);
        if (!tb_le32(h + PACK_NEXT))
            return 1;
        at = end;
    }
    return tb_archive_fail(EFBIG);
}

TbReadStream *tb_ibm_pack_member(TbReadStream *input, uint64_t offset, uint64_t packed,
                                 uint64_t size, TbArchiveKind kind)
{
    /* A stored member's raw bytes cannot begin with a compressed stream's signature. */
    unsigned char marker[PACK2_STORED_MARKER_BYTES + FTCOMP_SIGNATURE_BYTES];
    unsigned char *signature = marker + PACK2_STORED_MARKER_BYTES;
    if (kind == TB_ARCHIVE_IBM_PACK2 && packed == size + PACK2_STORED_MARKER_BYTES &&
        tb_stream_read_at(input, offset, marker,
                          size >= FTCOMP_SIGNATURE_BYTES ? sizeof marker
                                                         : PACK2_STORED_MARKER_BYTES) &&
        (size < FTCOMP_SIGNATURE_BYTES || (memcmp(signature, "fT19", FTCOMP_SIGNATURE_BYTES) &&
                                           memcmp(signature, "fT21", FTCOMP_SIGNATURE_BYTES))))
        return tb_stream_slice(input, offset + PACK2_STORED_MARKER_BYTES, size);
    if (packed > PACK_MEMBER_LIMIT || size > PACK_MEMBER_LIMIT)
    {
        tb_archive_fail(EFBIG);
        return NULL;
    }
    unsigned char *data = malloc(packed ? (size_t)packed : 1), *output = NULL;
    int ok =
        data && tb_stream_read_at(input, offset, data, (size_t)packed) &&
        tb_pack_decode(data, (size_t)packed, (size_t)size, kind == TB_ARCHIVE_IBM_PACK2, &output);
    free(data);
    return ok ? tb_stream_memory(output, (size_t)size) : NULL;
}
