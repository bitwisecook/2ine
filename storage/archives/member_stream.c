/* Public domain. No rights reserved. Wholly AI-authored. */
/* Opens one named member as a private stream, dispatching on the walker's member kind. */
#include "storage/archives/container_internal.h"
#include "storage/volume.h"
#include <stdlib.h>
#include <string.h>

TbReadStream *tb_legacy_member_result(unsigned char *output, uint64_t size, int ok)
{
    if (!ok)
    {
        free(output);
        if (errno != ENOTSUP)
            tb_archive_fail(EIO);
        return NULL;
    }
    tb_media_session_stats()->decompressed_bytes += size;
    return tb_stream_memory(output, (size_t)size);
}

int tb_archive_extract_visit(void *opaque, const char *name, uint64_t size, int directory,
                             void *input, uint64_t offset, uint64_t packed, TbArchiveKind kind,
                             int64_t modified)
{
    (void)modified;
    TbArchiveExtract *e = opaque;
    if (strcmp(e->name, name))
        return 1;
    if (directory || size > TB_MEDIA_MEMBER_LIMIT)
        return tb_archive_fail(directory ? EISDIR : EFBIG);
    if (kind == TB_ARCHIVE_DISK_IMAGE)
        e->out = tb_image_reader_member(input, name);
    else if (kind == TB_ARCHIVE_STORED_SLICE)
        e->out = packed == size ? tb_stream_slice(input, offset, size) : NULL;
    else if (kind == TB_ARCHIVE_FILTERED_SLICE)
    {
        TbReadStream *slice = tb_stream_slice(input, offset, packed);
        if (slice)
        {
            e->out = tb_libarchive_member(slice, name, size, 1);
            tb_stream_close(slice);
        }
    }
    else if (kind >= TB_ARCHIVE_ZOO)
        e->out = tb_zoo_member(input, offset, packed, size, kind);
    else if (kind >= TB_ARCHIVE_ARJ)
        e->out = tb_arj_member(input, offset, packed, size, kind);
    else if (kind >= TB_ARCHIVE_ARC)
        e->out = tb_arc_member(input, offset, packed, size, kind);
    else if (kind == TB_ARCHIVE_IBM_PACK || kind == TB_ARCHIVE_IBM_PACK2)
        e->out = tb_ibm_pack_member(input, offset, packed, size, kind);
    else
    {
        int legacy = 0;
        e->out = tb_zip_legacy_member(e->parent, input, name, size, &legacy);
        if (legacy)
            return e->out ? 2 : 0;
        int indexed = 0;
        e->out = tb_zip_indexed_member(e->parent, input, name, size, &indexed);
        if (indexed)
            return e->out ? 2 : 0;
        e->out = tb_zip_stored_member(e->parent, input, size);
        if (!e->out)
            e->out = tb_libarchive_member(e->parent, name, size, 0);
    }
    return e->out ? 2 : 0;
}
