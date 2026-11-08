/* Public domain. No rights reserved. Wholly AI-authored. */
/* The checked ZIP central directory shared by the ZIP member readers. */
#ifndef TB_STORAGE_ZIP_INTERNAL_H
#define TB_STORAGE_ZIP_INTERNAL_H
#include "storage/read_stream.h"
#include "formats/endian.h"
#include <stdint.h>

#define ZIP_LOCAL_SIGNATURE "PK\3\4"
#define ZIP_CENTRAL_SIGNATURE "PK\1\2"
#define ZIP_END_SIGNATURE "PK\5\6"

enum
{
    ZIP_SIGNATURE_BYTES = 4,
    /* Local file header; the name and extra field follow it, then the data. */
    ZIP_LOCAL_FLAGS = 6,
    ZIP_LOCAL_METHOD = 8,
    ZIP_LOCAL_CRC = 14,
    ZIP_LOCAL_PACKED = 18,
    ZIP_LOCAL_SIZE = 22,
    ZIP_LOCAL_NAME_BYTES = 26,
    ZIP_LOCAL_EXTRA_BYTES = 28,
    ZIP_LOCAL_HEADER_BYTES = 30,
    /* Central directory file header. */
    ZIP_CENTRAL_FLAGS = 8,
    ZIP_CENTRAL_METHOD = 10,
    ZIP_CENTRAL_CRC = 16,
    ZIP_CENTRAL_PACKED = 20,
    ZIP_CENTRAL_SIZE = 24,
    ZIP_CENTRAL_NAME_BYTES = 28,
    ZIP_CENTRAL_EXTRA_BYTES = 30,
    ZIP_CENTRAL_COMMENT_BYTES = 32,
    ZIP_CENTRAL_DISK = 34,
    ZIP_CENTRAL_LOCAL_HEADER = 42,
    ZIP_CENTRAL_HEADER_BYTES = 46,
    /* End of central directory record, followed by a comment of up to 65535 bytes. */
    ZIP_END_DISK = 4,
    ZIP_END_CENTRAL_DISK = 6,
    ZIP_END_DISK_ENTRIES = 8,
    ZIP_END_ENTRIES = 10,
    ZIP_END_CENTRAL_BYTES = 12,
    ZIP_END_CENTRAL_OFFSET = 16,
    ZIP_END_COMMENT_BYTES = 20,
    ZIP_END_BYTES = 22,
    ZIP_END_SEARCH_BYTES = ZIP_END_BYTES + 65535,
    ZIP_ZIP64_ENTRIES = 65535, /* entry count of a ZIP64 archive */

    /* General-purpose flags. */
    ZIP_FLAG_ENCRYPTED = 0x01,
    ZIP_FLAG_IMPLODE_8K_WINDOW = 0x02,
    ZIP_FLAG_IMPLODE_LITERAL_TREE = 0x04,
    ZIP_FLAG_DATA_DESCRIPTOR = 0x08, /* local sizes and CRC follow the data */
    ZIP_FLAG_STRONG_ENCRYPTION = 0x40,
    /* Flags the local and central headers must agree on. */
    ZIP_FLAGS_MATCHED = 0x4f,
    ZIP_FLAGS_UNSUPPORTED = ZIP_FLAG_ENCRYPTED | ZIP_FLAG_STRONG_ENCRYPTION,

    ZIP_METHOD_STORED = 0,
    ZIP_METHOD_SHRINK = 1,
    ZIP_METHOD_REDUCE_LAST = 5, /* methods 2-5 reduce with compression factors 1-4 */
    ZIP_METHOD_IMPLODE = 6,
    ZIP_METHOD_DEFLATE = 8
};

/* Offset of a local header's data from the header's start. */
static inline uint64_t tb_zip_local_data(const unsigned char *header)
{
    return ZIP_LOCAL_HEADER_BYTES + (uint64_t)tb_le16(header + ZIP_LOCAL_NAME_BYTES) +
           tb_le16(header + ZIP_LOCAL_EXTRA_BYTES);
}

typedef struct TbZipMetadata
{
    uint64_t position; /* local header, corrected for a prepended self-extractor */
    uint32_t packed, size, crc;
    unsigned flags, method;
} TbZipMetadata;

/* Finds name in the central directory. Returns 1, or 0 with errno (ENOENT when absent);
   ZIP64 and multi-disk archives fail with EINVAL. */
int tb_zip_metadata(TbReadStream *parent, const char *name, TbZipMetadata *result);
#endif
