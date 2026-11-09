/* Public domain. No rights reserved. Wholly AI-authored. */
/* Stored ZIP members as direct slices of the parent reader. */
#include "storage/archives/container_internal.h"
#include "storage/archives/zip/zip_internal.h"
#include "third_party/libarchive/archive.h"
#include "formats/endian.h"
#include <string.h>

/* ZIP_STORED members can be addressed directly in the parent reader. Local
   header spans are checked independently of central-directory size metadata. */
TbReadStream *tb_zip_stored_member(TbReadStream *parent, struct archive *archive, uint64_t size)
{
    if ((archive_format(archive) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_ZIP)
        return NULL;
    la_int64_t position = archive_read_header_position(archive);
    unsigned char h[ZIP_LOCAL_HEADER_BYTES];
    if (position < 0 || !tb_stream_read_at(parent, (uint64_t)position, h, sizeof h) ||
        memcmp(h, ZIP_LOCAL_SIGNATURE, ZIP_SIGNATURE_BYTES) ||
        tb_le16(h + ZIP_LOCAL_METHOD) != ZIP_METHOD_STORED ||
        (tb_le16(h + ZIP_LOCAL_FLAGS) & ZIP_FLAG_ENCRYPTED))
        return NULL;
    uint64_t start = (uint64_t)position + tb_zip_local_data(h);
    TbReadStream *stream = tb_stream_slice(parent, start, size);
    if (stream)
        tb_media_session_stats()->direct_slices++;
    return stream;
}
