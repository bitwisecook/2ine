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
    size_t amount = length < ZIP_END_SEARCH_BYTES ? (size_t)length : ZIP_END_SEARCH_BYTES;
    unsigned char *tail = malloc(amount ? amount : 1);
    if (amount < ZIP_END_BYTES || !tail ||
        !tb_stream_read_at(parent, length - amount, tail, amount))
    {
        free(tail);
        return 0;
    }
    size_t at = amount - ZIP_END_BYTES;
    int found = 0;
    for (;;)
    {
        if (!memcmp(tail + at, ZIP_END_SIGNATURE, ZIP_SIGNATURE_BYTES) &&
            tb_le16(tail + at + ZIP_END_COMMENT_BYTES) == amount - at - ZIP_END_BYTES)
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
    unsigned char end[ZIP_END_BYTES];
    memcpy(end, tail + at, sizeof end);
    uint64_t end_at = length - amount + at;
    free(tail);
    uint64_t central = tb_le32(end + ZIP_END_CENTRAL_OFFSET),
             central_size = tb_le32(end + ZIP_END_CENTRAL_BYTES);
    unsigned count = tb_le16(end + ZIP_END_ENTRIES);
    if (tb_le16(end + ZIP_END_DISK) || tb_le16(end + ZIP_END_CENTRAL_DISK) ||
        tb_le16(end + ZIP_END_DISK_ENTRIES) != count || count == ZIP_ZIP64_ENTRIES ||
        central_size > end_at || central > end_at - central_size)
        return tb_archive_fail(EINVAL);
    unsigned char header[ZIP_CENTRAL_HEADER_BYTES];
    uint64_t adjustment = 0;
    if (!tb_stream_read_at(parent, central, header, ZIP_SIGNATURE_BYTES) ||
        memcmp(header, ZIP_CENTRAL_SIGNATURE, ZIP_SIGNATURE_BYTES))
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
            memcmp(header, ZIP_CENTRAL_SIGNATURE, ZIP_SIGNATURE_BYTES))
            return tb_archive_fail(EINVAL);
        unsigned name_length = tb_le16(header + ZIP_CENTRAL_NAME_BYTES);
        uint64_t span = sizeof header + name_length + tb_le16(header + ZIP_CENTRAL_EXTRA_BYTES) +
                        tb_le16(header + ZIP_CENTRAL_COMMENT_BYTES);
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
            /* ZIP64 and multi-disk entries are not read. */
            if (tb_le16(header + ZIP_CENTRAL_DISK) ||
                tb_le32(header + ZIP_CENTRAL_SIZE) == UINT32_MAX ||
                tb_le32(header + ZIP_CENTRAL_PACKED) == UINT32_MAX ||
                tb_le32(header + ZIP_CENTRAL_LOCAL_HEADER) == UINT32_MAX)
                return tb_archive_fail(EINVAL);
            *result = (TbZipMetadata){
                .position = (uint64_t)tb_le32(header + ZIP_CENTRAL_LOCAL_HEADER) + adjustment,
                .packed = tb_le32(header + ZIP_CENTRAL_PACKED),
                .size = tb_le32(header + ZIP_CENTRAL_SIZE),
                .crc = tb_le32(header + ZIP_CENTRAL_CRC),
                .flags = tb_le16(header + ZIP_CENTRAL_FLAGS),
                .method = tb_le16(header + ZIP_CENTRAL_METHOD)};
            return 1;
        }
        central += span;
    }
    return tb_archive_fail(ENOENT);
}
