/* Public domain. No rights reserved. Wholly AI-authored adapter. */
/* ZIP methods 1-6 (Shrink, Reduce, Implode) through the unchanged HWZIP codec, which
   retains its own public-domain dedication. */
#include "storage/archives/container_internal.h"
#include "storage/archives/zip/zip_internal.h"
#include "storage/vendor/hwzip/implode.h"
#include "storage/vendor/hwzip/shrink.h"
#include "storage/vendor/hwzip/reduce.h"
#include "storage/vendor/libarchive/archive.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#define LEGACY_ZIP_LIMIT (16u * 1024u * 1024u)

TbReadStream *tb_zip_legacy_member(TbReadStream *parent, struct archive *archive, const char *name,
                                   uint64_t size, int *handled)
{
    *handled = 0;
    if ((archive_format(archive) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_ZIP)
        return NULL;
    /* libarchive's header-position hint can identify the preceding member
       when the seekable ZIP reader jumps from its central directory. Resolve
       the selected name against the checked central table instead. */
    TbZipMetadata metadata;
    if (!tb_zip_metadata(parent, name, &metadata) || metadata.method > 6)
        return NULL;
    *handled = 1;
    unsigned char header[30];
    if (!tb_stream_read_at(parent, metadata.position, header, sizeof header) ||
        memcmp(header, "PK\3\4", 4) || tb_le16(header + 8) != metadata.method ||
        ((tb_le16(header + 6) ^ metadata.flags) & 0x4f) || (metadata.flags & 0x41) ||
        size != metadata.size ||
        (!(metadata.flags & 8) &&
         (metadata.packed != tb_le32(header + 18) || metadata.crc != tb_le32(header + 14) ||
          metadata.size != tb_le32(header + 22))))
    {
        tb_archive_fail(EINVAL);
        return NULL;
    }
    uint32_t packed = metadata.packed, crc = metadata.crc;
    uint64_t start =
        metadata.position + sizeof header + tb_le16(header + 26) + tb_le16(header + 28);
    if (start > tb_stream_size(parent) || packed > tb_stream_size(parent) - start)
    {
        tb_archive_fail(EINVAL);
        return NULL;
    }
    if (!metadata.method)
    {
        if (packed != size)
        {
            tb_archive_fail(EINVAL);
            return NULL;
        }
        tb_media_session_stats()->direct_slices++;
        return tb_stream_slice(parent, start, size);
    }
    if (size > LEGACY_ZIP_LIMIT || packed > LEGACY_ZIP_LIMIT)
    {
        tb_archive_fail(EFBIG);
        return NULL;
    }
    unsigned char *input = malloc(packed ? packed : 1), *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(parent, start, input, packed);
    size_t used = 0;
    int large = (tb_le16(header + 6) & 2) != 0, literals = (tb_le16(header + 6) & 4) != 0;
    if (ok)
    {
        if (metadata.method == 1)
        {
            size_t written = 0;
            ok =
                hwunshrink(input, packed, &used, output, (size_t)size, &written) == HWUNSHRINK_OK &&
                written == size;
        }
        else if (metadata.method <= 5)
            ok = hwexpand(input, packed, (size_t)size, (int)metadata.method - 1, &used, output) ==
                 HWEXPAND_OK;
        else
        {
            ok = hwexplode(input, packed, (size_t)size, large, literals, 0, &used, output) ==
                 HWEXPLODE_OK;
            /* PKZIP 1.01/1.02 used the wrong minimum match length in two flag
               combinations. Retry only if complete length and CRC validate. */
            if (!ok || used != packed || (uint32_t)crc32(0, output, (uInt)size) != crc)
                ok = hwexplode(input, packed, (size_t)size, large, literals, 1, &used, output) ==
                     HWEXPLODE_OK;
        }
        ok = ok && used == packed && (uint32_t)crc32(0, output, (uInt)size) == crc;
    }
    free(input);
    if (!ok)
    {
        free(output);
        tb_archive_fail(EIO);
        return NULL;
    }
    tb_media_session_stats()->decompressed_bytes += size;
    return tb_stream_memory(output, (size_t)size);
}
