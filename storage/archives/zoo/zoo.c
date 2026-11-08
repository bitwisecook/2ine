/* Public domain. No rights reserved. Wholly AI-authored. */
/* Zoo archives: the linked directory walk and member decoding.
   Zoo directory/codec contracts cross-checked against Zoo 2.10 and Deark. */
#define _POSIX_C_SOURCE 200809L
#include "storage/archives/container_internal.h"
#include "storage/archives/codecs/codecs.h"
#include "storage/common/file_time.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>

int tb_zoo_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    unsigned char global[34];
    if (!tb_stream_read_at(file, 0, global, sizeof global) ||
        tb_le32(global + 20) != TB_ZOO_SIGNATURE ||
        tb_le32(global + 28) != (uint32_t)(0u - tb_le32(global + 24)))
        return tb_archive_fail(EINVAL);
    uint64_t at = tb_le32(global + 24), length = tb_stream_size(file);
    uint64_t *seen = calloc(262144, sizeof *seen);
    if (!seen)
        return 0;
    int result = 0;
    for (unsigned count = 0; count < TB_MEDIA_ENTRY_LIMIT; count++)
    {
        unsigned char h[56];
        if (at < 34 || !tb_stream_read_at(file, at, h, 51) || tb_le32(h) != TB_ZOO_SIGNATURE)
            break;
        size_t slot = (size_t)((at * UINT64_C(11400714819323198485)) >> 46);
        while (seen[slot] && seen[slot] != at)
            slot = (slot + 1) & 262143;
        if (seen[slot] == at)
            break;
        seen[slot] = at;
        uint32_t next = tb_le32(h + 6), data = tb_le32(h + 10), packed = tb_le32(h + 24);
        if (!next)
        {
            result = 1;
            break;
        }
        if (h[4] < 1 || h[4] > 2 || h[5] > 2 || next >= length || data > length ||
            packed > length - data)
            break;
        char short_name[14];
        memcpy(short_name, h + 38, 13);
        short_name[13] = 0;
        char *name = NULL;
        int name_attempted = 0;
        if (h[4] == 2)
        {
            if (!tb_stream_read_at(file, at, h, 56))
                break;
            unsigned variable = tb_le16(h + 51);
            unsigned char *header = malloc(56 + (size_t)variable);
            int ok = header && tb_stream_read_at(file, at, header, 56 + (size_t)variable);
            if (ok)
            {
                uint16_t expected = tb_le16(header + 54);
                header[54] = header[55] = 0;
                ok = tb_crc16_arc(header, 56 + variable) == expected;
            }
            if (ok && variable >= 2)
            {
                unsigned names = header[56], dirs = header[57];
                if (names + dirs > variable - 2)
                    ok = 0;
                else
                {
                    char joined[515];
                    size_t used = 0;
                    if (dirs)
                    {
                        size_t n = strnlen((char *)header + 58 + names, dirs);
                        memcpy(joined, header + 58 + names, n);
                        used = n;
                        if (used && joined[used - 1] != '/' && joined[used - 1] != '\\')
                            joined[used++] = '/';
                    }
                    const char *base = names ? (char *)header + 58 : short_name;
                    size_t n = names ? strnlen(base, names) : strlen(base);
                    memcpy(joined + used, base, n);
                    joined[used + n] = 0;
                    name_attempted = 1;
                    name = tb_media_sanitise_member(joined, 0);
                }
            }
            free(header);
            if (!ok)
                break;
        }
        if (!name_attempted)
            name = tb_media_sanitise_member(short_name, 0);
        if (!h[30] && name && *name)
        {
            int rc = visit(context, name, tb_le32(h + 20), 0, file, at, packed,
                           TB_ARCHIVE_ZOO + h[5], tb_dos_time(tb_le16(h + 14), tb_le16(h + 16)));
            free(name);
            if (rc != 1)
            {
                result = rc;
                break;
            }
        }
        else
            free(name);
        at = next;
    }
    free(seen);
    return result ? result : tb_archive_fail(EINVAL);
}

TbReadStream *tb_zoo_member(TbReadStream *file, uint64_t offset, uint64_t packed, uint64_t size,
                            TbArchiveKind kind)
{
    unsigned char h[51];
    if (packed > TB_LEGACY_ARCHIVE_LIMIT || size > TB_LEGACY_ARCHIVE_LIMIT)
    {
        tb_archive_fail(EFBIG);
        return NULL;
    }
    if (!tb_stream_read_at(file, offset, h, sizeof h) || tb_le32(h) != TB_ZOO_SIGNATURE ||
        tb_le32(h + 24) != packed || tb_le32(h + 20) != size)
        return NULL;
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output && tb_stream_read_at(file, tb_le32(h + 10), input, (size_t)packed);
    if (ok && kind == TB_ARCHIVE_ZOO)
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && kind == TB_ARCHIVE_ZOO + 1)
        ok = tb_lzd_decode(input, (size_t)packed, output, (size_t)size);
    else if (ok && kind == TB_ARCHIVE_ZOO + 2)
        ok = tb_lzh_decode(input, (size_t)packed, output, (size_t)size, 13);
    else
        ok = 0;
    free(input);
    if (!ok || tb_crc16_arc(output, (size_t)size) != tb_le16(h + 18))
    {
        free(output);
        tb_archive_fail(EIO);
        return NULL;
    }
    tb_media_session_stats()->decompressed_bytes += size;
    return tb_stream_memory(output, (size_t)size);
}
