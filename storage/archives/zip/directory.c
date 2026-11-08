/* Public domain. No rights reserved. Wholly AI-authored. */
/* ZIP end-of-central-directory and central-directory lookup. */
#include "storage/archives/container_internal.h"
#include "storage/archives/zip/zip_internal.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>

int tb_zip_metadata(TbReadStream *parent, const char *name, TbZipMetadata *result)
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
        if (!memcmp(tail + at, "PK\5\6", 4) && tb_le16(tail + at + 20) == amount - at - 22)
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
        return tb_archive_fail(EINVAL);
    }
    unsigned char end[22];
    memcpy(end, tail + at, sizeof end);
    uint64_t end_at = length - amount + at;
    free(tail);
    uint64_t central = tb_le32(end + 16), central_size = tb_le32(end + 12);
    unsigned count = tb_le16(end + 10);
    if (tb_le16(end + 4) || tb_le16(end + 6) || tb_le16(end + 8) != count || count == 65535 ||
        central_size > end_at || central > end_at - central_size)
        return tb_archive_fail(EINVAL);
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
            return tb_archive_fail(EINVAL);
        unsigned name_length = tb_le16(header + 28);
        uint64_t span = sizeof header + name_length + tb_le16(header + 30) + tb_le16(header + 32);
        if (span > limit - central)
            return tb_archive_fail(EINVAL);
        char *raw = malloc((size_t)name_length + 1);
        if (!raw || !tb_stream_read_at(parent, central + sizeof header, raw, name_length))
        {
            free(raw);
            return 0;
        }
        raw[name_length] = 0;
        char *normalized = memchr(raw, 0, name_length) ? NULL : tb_media_sanitise_member(raw, 0);
        free(raw);
        int matched = normalized && !strcmp(normalized, name);
        free(normalized);
        if (matched)
        {
            if (tb_le16(header + 34) || tb_le32(header + 24) == UINT32_MAX ||
                tb_le32(header + 20) == UINT32_MAX || tb_le32(header + 42) == UINT32_MAX)
                return tb_archive_fail(EINVAL);
            *result = (TbZipMetadata){(uint64_t)tb_le32(header + 42) + adjustment,
                                      tb_le32(header + 20),
                                      tb_le32(header + 24),
                                      tb_le32(header + 16),
                                      tb_le16(header + 8),
                                      tb_le16(header + 10)};
            return 1;
        }
        central += span;
    }
    return tb_archive_fail(ENOENT);
}
