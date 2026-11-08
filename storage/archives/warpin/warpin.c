/* Public domain. No rights reserved. Wholly AI-authored. */
/* WarpIN .WPI installation archives: the install script and package file directory. */
#include "storage/archives/container_internal.h"
#include "formats/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WarpIN revisions 3/4: WIArcHeader (0x214), WIPackHeader (48), WIFileHeader (285). */
int tb_warpin_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    if (tb_stream_seek(file, 0, SEEK_END))
        return 0;
    int64_t length = tb_stream_tell(file);
    if (length < 532)
        return tb_archive_fail(ENOTSUP);
    unsigned char head[572];
    int64_t at = 0;
    int64_t tail = length >= 572 ? ((length - 572) / 512) * 512 : 0;
    if (tb_stream_seek(file, tail, SEEK_SET) || tb_stream_read(head, 1, 532, file) != 532)
        return tb_archive_fail(EIO);
    if (memcmp(head, "\x77\x04\x02\xbe", 4))
    {
        size_t n = (size_t)(length < 200532 ? length : 200532);
        unsigned char *front = malloc(n);
        if (!front)
            return 0;
        tb_stream_seek(file, 0, SEEK_SET);
        if (tb_stream_read(front, 1, n, file) != n)
        {
            free(front);
            return tb_archive_fail(EIO);
        }
        int found = 0;
        for (size_t i = 0; i + 532 <= n; i++)
            if (!memcmp(front + i, "\x77\x04\x02\xbe", 4))
            {
                at = (int64_t)i;
                memcpy(head, front + i, 532);
                found = 1;
                break;
            }
        free(front);
        if (!found)
            return tb_archive_fail(ENOTSUP);
    }
    else
        at = tail;
    unsigned revision = tb_le16(head + 4), packages = tb_le16(head + 522);
    uint64_t data = (uint64_t)at + 532;
    if (revision == 4)
    {
        if (tb_stream_seek(file, at + 532, SEEK_SET) ||
            tb_stream_read(head + 532, 1, 40, file) != 40)
            return tb_archive_fail(EIO);
        unsigned ext = tb_le32(head + 532);
        if (ext < 40)
            return tb_archive_fail(EINVAL);
        data = (uint64_t)tb_le32(head + 536) + 532 + ext;
    }
    else if (revision != 3)
        return tb_archive_fail(ENOTSUP);
    uint64_t script = tb_le16(head + 526), orig = tb_le16(head + 524);
    if (script)
    {
        if (data + script > (uint64_t)length)
            return tb_archive_fail(EINVAL);
        int rc = visit(context, "install.wis", orig, 0, file, data, script,
                       TB_ARCHIVE_FILTERED_SLICE, INT64_MIN);
        if (rc != 1)
            return rc;
    }
    data += script + tb_le32(head + 528);
    if (packages > TB_MEDIA_ENTRY_LIMIT || data + (uint64_t)packages * 48 > (uint64_t)length)
        return tb_archive_fail(EINVAL);
    unsigned count = 0;
    for (unsigned p = 0; p < packages; p++)
    {
        unsigned char package[48];
        if (tb_stream_seek(file, (int64_t)(data + (uint64_t)p * 48), SEEK_SET) ||
            tb_stream_read(package, 1, 48, file) != 48)
            return tb_archive_fail(EIO);
        unsigned files = tb_le16(package + 2);
        uint64_t pos = tb_le32(package + 4);
        for (unsigned f = 0; f < files; f++)
        {
            unsigned char h[285];
            if (++count > TB_MEDIA_ENTRY_LIMIT || pos + 285 > (uint64_t)length ||
                tb_stream_seek(file, (int64_t)pos, SEEK_SET) ||
                tb_stream_read(h, 1, 285, file) != 285)
                return tb_archive_fail(EINVAL);
            uint64_t packed = tb_le32(h + 12), size = tb_le32(h + 8);
            unsigned method = tb_le16(h + 4);
            if (tb_le16(h) != 0xf012 || method > 1 || pos + 285 + packed > (uint64_t)length ||
                !memchr(h + 20, 0, 256))
                return tb_archive_fail(EINVAL);
            char *name = tb_media_sanitise_member((char *)h + 20, 0);
            if (name && *name)
            {
                int rc =
                    visit(context, name, size, 0, file, pos + 285, packed,
                          method ? TB_ARCHIVE_FILTERED_SLICE : TB_ARCHIVE_STORED_SLICE, INT64_MIN);
                free(name);
                if (rc != 1)
                    return rc;
            }
            else
                free(name);
            pos += 285 + packed;
        }
    }
    return 1;
}
