/* Public domain. No rights reserved. Wholly AI-authored. */
/* Stored ZIP members as direct slices of the parent reader. */
#include "storage/archives/container_internal.h"
#include "storage/vendor/libarchive/archive.h"
#include "formats/endian.h"
#include <string.h>

/* ZIP_STORED members can be addressed directly in the parent reader. Local
   header spans are checked independently of central-directory size metadata. */
TbReadStream *tb_zip_stored_member(TbReadStream *parent, struct archive *archive, uint64_t size)
{
    if ((archive_format(archive) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_ZIP)
        return NULL;
    la_int64_t position = archive_read_header_position(archive);
    unsigned char h[30];
    if (position < 0 || !tb_stream_read_at(parent, (uint64_t)position, h, sizeof h) ||
        memcmp(h, "PK\3\4", 4) || tb_le16(h + 8) != 0 || (tb_le16(h + 6) & 1))
        return NULL;
    uint64_t start = (uint64_t)position + sizeof h + tb_le16(h + 26) + tb_le16(h + 28);
    TbReadStream *stream = tb_stream_slice(parent, start, size);
    if (stream)
        tb_media_session_stats()->direct_slices++;
    return stream;
}
