/* Public domain. No rights reserved. Wholly AI-authored. */
/* WarpIN .WPI installation archives: the install script and package file directory. */
#include "storage/archives/container_internal.h"
#include "formats/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    WPI_SIGNATURE_BYTES = 4,
    /* WIArcHeader, at the start of the file or after an executable stub. */
    WPI_REVISION = 4,
    WPI_PACKAGES = 522,
    WPI_SCRIPT_SIZE = 524,   /* uncompressed install script */
    WPI_SCRIPT_PACKED = 526, /* compressed install script, 0 when absent */
    WPI_EXTENDED_BYTES = 528,
    WPI_HEADER_BYTES = 532,
    /* Revision 4 extension, after WIArcHeader. */
    WPI_EXTENSION_SIZE = 0, /* bytes in the extension */
    WPI_EXTENSION_DATA = 4, /* script offset, less the header and extension sizes */
    WPI_EXTENSION_MIN = 40,
    /* A trailing header starts on a 512-byte boundary within the last 572 bytes. */
    WPI_TAIL_ALIGNMENT = 512,
    WPI_TAIL_BYTES = WPI_HEADER_BYTES + WPI_EXTENSION_MIN,
    /* A leading header is searched for within an executable stub of this size. */
    WPI_STUB_LIMIT = 200000,
    /* WIPackHeader */
    WPI_PACKAGE_FILES = 2,
    WPI_PACKAGE_FIRST = 4,
    WPI_PACKAGE_BYTES = 48,
    /* WIFileHeader */
    WPI_FILE_MAGIC = 0,
    WPI_FILE_METHOD = 4,
    WPI_FILE_SIZE = 8,
    WPI_FILE_PACKED = 12,
    WPI_FILE_NAME = 20,
    WPI_FILE_NAME_BYTES = 256,
    WPI_FILE_BYTES = 285,
    WPI_FILE_SIGNATURE = 0xf012,
    WPI_METHOD_STORED = 0,
    WPI_METHOD_BZIP2 = 1
};

/* WarpIN revisions 3/4: WIArcHeader (0x214), WIPackHeader (48), WIFileHeader (285). */
int tb_warpin_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    if (tb_stream_seek(file, 0, SEEK_END))
        return 0;
    int64_t length = tb_stream_tell(file);
    if (length < WPI_HEADER_BYTES)
        return tb_archive_fail(ENOTSUP);
    unsigned char head[WPI_TAIL_BYTES];
    int64_t at = 0;
    int64_t tail = length >= WPI_TAIL_BYTES
                       ? ((length - WPI_TAIL_BYTES) / WPI_TAIL_ALIGNMENT) * WPI_TAIL_ALIGNMENT
                       : 0;
    if (tb_stream_seek(file, tail, SEEK_SET) ||
        tb_stream_read(head, 1, WPI_HEADER_BYTES, file) != WPI_HEADER_BYTES)
        return tb_archive_fail(EIO);
    if (memcmp(head, TB_WARPIN_SIGNATURE, WPI_SIGNATURE_BYTES))
    {
        size_t n = (size_t)(length < WPI_STUB_LIMIT + WPI_HEADER_BYTES
                                ? length
                                : WPI_STUB_LIMIT + WPI_HEADER_BYTES);
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
        for (size_t i = 0; i + WPI_HEADER_BYTES <= n; i++)
            if (!memcmp(front + i, TB_WARPIN_SIGNATURE, WPI_SIGNATURE_BYTES))
            {
                at = (int64_t)i;
                memcpy(head, front + i, WPI_HEADER_BYTES);
                found = 1;
                break;
            }
        free(front);
        if (!found)
            return tb_archive_fail(ENOTSUP);
    }
    else
        at = tail;
    unsigned revision = tb_le16(head + WPI_REVISION), packages = tb_le16(head + WPI_PACKAGES);
    uint64_t data = (uint64_t)at + WPI_HEADER_BYTES;
    if (revision == 4)
    {
        if (tb_stream_seek(file, at + WPI_HEADER_BYTES, SEEK_SET) ||
            tb_stream_read(head + WPI_HEADER_BYTES, 1, WPI_EXTENSION_MIN, file) !=
                WPI_EXTENSION_MIN)
            return tb_archive_fail(EIO);
        unsigned ext = tb_le32(head + WPI_HEADER_BYTES + WPI_EXTENSION_SIZE);
        if (ext < WPI_EXTENSION_MIN)
            return tb_archive_fail(EINVAL);
        data = (uint64_t)tb_le32(head + WPI_HEADER_BYTES + WPI_EXTENSION_DATA) + WPI_HEADER_BYTES +
               ext;
    }
    else if (revision != 3)
        return tb_archive_fail(ENOTSUP);
    uint64_t script = tb_le16(head + WPI_SCRIPT_PACKED), orig = tb_le16(head + WPI_SCRIPT_SIZE);
    if (script)
    {
        if (data + script > (uint64_t)length)
            return tb_archive_fail(EINVAL);
        int rc = visit(context, "install.wis", orig, 0, file, data, script,
                       TB_ARCHIVE_FILTERED_SLICE, INT64_MIN);
        if (rc != 1)
            return rc;
    }
    data += script + tb_le32(head + WPI_EXTENDED_BYTES);
    if (packages > TB_MEDIA_ENTRY_LIMIT ||
        data + (uint64_t)packages * WPI_PACKAGE_BYTES > (uint64_t)length)
        return tb_archive_fail(EINVAL);
    unsigned count = 0;
    for (unsigned p = 0; p < packages; p++)
    {
        unsigned char package[WPI_PACKAGE_BYTES];
        if (tb_stream_seek(file, (int64_t)(data + (uint64_t)p * WPI_PACKAGE_BYTES), SEEK_SET) ||
            tb_stream_read(package, 1, WPI_PACKAGE_BYTES, file) != WPI_PACKAGE_BYTES)
            return tb_archive_fail(EIO);
        unsigned files = tb_le16(package + WPI_PACKAGE_FILES);
        uint64_t pos = tb_le32(package + WPI_PACKAGE_FIRST);
        for (unsigned f = 0; f < files; f++)
        {
            unsigned char h[WPI_FILE_BYTES];
            if (++count > TB_MEDIA_ENTRY_LIMIT || pos + WPI_FILE_BYTES > (uint64_t)length ||
                tb_stream_seek(file, (int64_t)pos, SEEK_SET) ||
                tb_stream_read(h, 1, WPI_FILE_BYTES, file) != WPI_FILE_BYTES)
                return tb_archive_fail(EINVAL);
            uint64_t packed = tb_le32(h + WPI_FILE_PACKED), size = tb_le32(h + WPI_FILE_SIZE);
            unsigned method = tb_le16(h + WPI_FILE_METHOD);
            if (tb_le16(h + WPI_FILE_MAGIC) != WPI_FILE_SIGNATURE || method > WPI_METHOD_BZIP2 ||
                pos + WPI_FILE_BYTES + packed > (uint64_t)length ||
                !memchr(h + WPI_FILE_NAME, 0, WPI_FILE_NAME_BYTES))
                return tb_archive_fail(EINVAL);
            char *name = tb_media_sanitise_member((char *)h + WPI_FILE_NAME, 0);
            if (name && *name)
            {
                int rc = visit(context, name, size, 0, file, pos + WPI_FILE_BYTES, packed,
                               method == WPI_METHOD_STORED ? TB_ARCHIVE_STORED_SLICE
                                                           : TB_ARCHIVE_FILTERED_SLICE,
                               INT64_MIN);
                free(name);
                if (rc != 1)
                    return rc;
            }
            else
                free(name);
            pos += WPI_FILE_BYTES + packed;
        }
    }
    return 1;
}
