/* Public domain. No rights reserved. Wholly AI-authored. */
/* IBM PACK and PACK2 (FTCOMP) files: the A5 96 directory walk and member decoding. */
#include "storage/archives/container_internal.h"
#include "storage/archives/ibm_pack/decode.h"
#include "storage/common/file_time.h"
#include "formats/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The callback may request one member; extraction never uses archive paths as host paths. */
int tb_ibm_pack_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    if (tb_stream_seek(file, 0, SEEK_END))
        return 0;
    int64_t length = tb_stream_tell(file);
    uint64_t at = 0;
    for (unsigned entries = 0; entries < TB_MEDIA_ENTRY_LIMIT; entries++)
    {
        unsigned char h[41];
        if (at > (uint64_t)length || tb_stream_seek(file, (int64_t)at, SEEK_SET) ||
            tb_stream_read(h, 1, 24, file) != 24 || h[0] != 0xa5 || h[1] != 0x96)
            return tb_archive_fail(EINVAL);
        unsigned kind = tb_le16(h + 2);
        uint64_t end = tb_le32(h + 20);
        if (!end)
            end = (uint64_t)length;
        if (end <= at || end > (uint64_t)length)
            return tb_archive_fail(EINVAL);
        unsigned header = kind == 0xfffd ? 41 : 26;
        if (kind != 0xfffd && kind != 0xfffe)
            return tb_archive_fail(ENOTSUP);
        if (tb_stream_read(h + 24, 1, header - 24, file) != header - 24)
            return tb_archive_fail(EIO);
        if (kind == 0xfffd && memcmp(h + 24, "FTCOMP", 6))
            return tb_archive_fail(EINVAL);
        unsigned n = tb_le16(h + header - 2);
        if (!n || n > 4096 || at + header + n > end)
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
        uint64_t data = at + header + n, ea = tb_le32(h + 12), data_end = ea ? ea : end;
        if (data_end < data || data_end > end)
        {
            free(name);
            return tb_archive_fail(EINVAL);
        }
        if (name && *name)
        {
            int result = visit(context, name, tb_le32(h + 16), 0, file, data, data_end - data,
                               kind == 0xfffd ? TB_ARCHIVE_IBM_PACK2 : TB_ARCHIVE_IBM_PACK,
                               tb_dos_time(tb_le16(h + 4), tb_le16(h + 6)));
            free(name);
            if (result != 1)
                return result;
        }
        else
            free(name);
        if (!tb_le32(h + 20))
            return 1;
        at = end;
    }
    return tb_archive_fail(EFBIG);
}

TbReadStream *tb_ibm_pack_member(TbReadStream *input, uint64_t offset, uint64_t packed,
                                 uint64_t size, TbArchiveKind kind)
{
    unsigned char marker[8];
    /* FTCOMP's stored form is a four-byte marker followed by raw bytes. */
    if (kind == TB_ARCHIVE_IBM_PACK2 && packed == size + 4 &&
        tb_stream_read_at(input, offset, marker, size >= 4 ? 8 : 4) &&
        (size < 4 || (memcmp(marker + 4, "fT19", 4) && memcmp(marker + 4, "fT21", 4))))
        return tb_stream_slice(input, offset + 4, size);
    if (packed > 16u * 1024u * 1024u || size > 16u * 1024u * 1024u)
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
