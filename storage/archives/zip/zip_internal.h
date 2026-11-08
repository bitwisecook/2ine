/* Public domain. No rights reserved. Wholly AI-authored. */
/* The checked ZIP central directory shared by the ZIP member readers. */
#ifndef TB_STORAGE_ZIP_INTERNAL_H
#define TB_STORAGE_ZIP_INTERNAL_H
#include "storage/read_stream.h"
#include <stdint.h>

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
