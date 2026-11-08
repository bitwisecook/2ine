/* Public domain. No rights reserved. Wholly AI-authored adapter. */
/* The unchanged HWZIP codec retains its own public-domain dedication. */
#include "ball/vendor/hwzip/implode.h"
#include "ball/vendor/hwzip/shrink.h"
#include "ball/vendor/hwzip/reduce.h"
#include <zlib.h>
#define LEGACY_ZIP_LIMIT (16u * 1024u * 1024u)

typedef struct LegacyZipMetadata
{
    uint64_t position;
    uint32_t packed, size, crc;
    unsigned flags, method;
} LegacyZipMetadata;

static int legacy_zip_metadata(TbReadStream *parent, const char *name, LegacyZipMetadata *result)
{
    uint64_t length = tb_stream_size(parent);
    size_t amount = length < 65557 ? (size_t)length : 65557;
    unsigned char *tail = malloc(amount ? amount : 1);
    if (amount < 22 || !tail || !tb_stream_read_at(parent, length - amount, tail, amount))
    {
        free(tail);
        return 0;
    }
    size_t at = amount - 22;
    int found = 0;
    for (;;)
    {
        if (!memcmp(tail + at, "PK\5\6", 4) && u16(tail + at + 20) == amount - at - 22)
        {
            found = 1;
            break;
        }
        if (!at--)
            break;
    }
    if (!found)
    {
        free(tail);
        return bad(EINVAL);
    }
    unsigned char end[22];
    memcpy(end, tail + at, sizeof end);
    uint64_t end_at = length - amount + at;
    free(tail);
    uint64_t central = u32(end + 16), central_size = u32(end + 12);
    unsigned count = u16(end + 10);
    if (u16(end + 4) || u16(end + 6) || u16(end + 8) != count || count == 65535 ||
        central_size > end_at || central > end_at - central_size)
        return bad(EINVAL);
    unsigned char header[46];
    uint64_t adjustment = 0;
    if (!tb_stream_read_at(parent, central, header, 4) || memcmp(header, "PK\1\2", 4))
    {
        /* Old self-extractors can prepend their executable without updating
           the original ZIP-relative offsets. */
        adjustment = end_at - central_size - central;
        central += adjustment;
    }
    uint64_t limit = central + central_size;
    for (unsigned i = 0; i < count; i++)
    {
        if (central > limit || sizeof header > limit - central ||
            !tb_stream_read_at(parent, central, header, sizeof header) ||
            memcmp(header, "PK\1\2", 4))
            return bad(EINVAL);
        unsigned name_length = u16(header + 28);
        uint64_t span = sizeof header + name_length + u16(header + 30) + u16(header + 32);
        if (span > limit - central)
            return bad(EINVAL);
        char *raw = malloc((size_t)name_length + 1);
        if (!raw || !tb_stream_read_at(parent, central + sizeof header, raw, name_length))
        {
            free(raw);
            return 0;
        }
        raw[name_length] = 0;
        char *normalized = memchr(raw, 0, name_length) ? NULL : member_name(raw, 0);
        free(raw);
        int matched = normalized && !strcmp(normalized, name);
        free(normalized);
        if (matched)
        {
            if (u16(header + 34) || u32(header + 24) == UINT32_MAX ||
                u32(header + 20) == UINT32_MAX || u32(header + 42) == UINT32_MAX)
                return bad(EINVAL);
            *result = (LegacyZipMetadata){(uint64_t)u32(header + 42) + adjustment,
                                          u32(header + 20),
                                          u32(header + 24),
                                          u32(header + 16),
                                          u16(header + 8),
                                          u16(header + 10)};
            return 1;
        }
        central += span;
    }
    return bad(ENOENT);
}

static TbReadStream *legacy_zip_member(TbReadStream *parent, struct archive *archive,
                                       const char *name, uint64_t size, int *handled)
{
    *handled = 0;
    if ((archive_format(archive) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_ZIP)
        return NULL;
    /* libarchive's header-position hint can identify the preceding member
       when the seekable ZIP reader jumps from its central directory. Resolve
       the selected name against the checked central table instead. */
    LegacyZipMetadata metadata;
    if (!legacy_zip_metadata(parent, name, &metadata) || metadata.method > 6)
        return NULL;
    *handled = 1;
    unsigned char header[30];
    if (!tb_stream_read_at(parent, metadata.position, header, sizeof header) ||
        memcmp(header, "PK\3\4", 4) || u16(header + 8) != metadata.method ||
        ((u16(header + 6) ^ metadata.flags) & 0x4f) || (metadata.flags & 0x41) ||
        size != metadata.size ||
        (!(metadata.flags & 8) &&
         (metadata.packed != u32(header + 18) || metadata.crc != u32(header + 14) ||
          metadata.size != u32(header + 22))))
    {
        bad(EINVAL);
        return NULL;
    }
    uint32_t packed = metadata.packed, crc = metadata.crc;
    uint64_t start = metadata.position + sizeof header + u16(header + 26) + u16(header + 28);
    if (start > tb_stream_size(parent) || packed > tb_stream_size(parent) - start)
    {
        bad(EINVAL);
        return NULL;
    }
    if (!metadata.method)
    {
        if (packed != size)
        {
            bad(EINVAL);
            return NULL;
        }
        media_session.stats.direct_slices++;
        return tb_stream_slice(parent, start, size);
    }
    if (size > LEGACY_ZIP_LIMIT || packed > LEGACY_ZIP_LIMIT)
    {
        bad(EFBIG);
        return NULL;
    }
    unsigned char *input = malloc(packed ? packed : 1), *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(parent, start, input, packed);
    size_t used = 0;
    int large = (u16(header + 6) & 2) != 0, literals = (u16(header + 6) & 4) != 0;
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
        bad(EIO);
        return NULL;
    }
    media_session.stats.decompressed_bytes += size;
    return tb_stream_memory(output, (size_t)size);
}
