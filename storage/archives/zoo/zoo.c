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

/* The archive header points at a linked list of directory entries. */
enum
{
    ZOO_GLOBAL_FIRST = 24, /* first directory entry */
    ZOO_GLOBAL_CHECK = 28, /* negated ZOO_GLOBAL_FIRST */
    ZOO_GLOBAL_BYTES = 34,
    /* Directory entry; type 1 entries end after the short name. */
    ZOO_ENTRY_SIGNATURE = 0,
    ZOO_ENTRY_TYPE = 4,
    ZOO_ENTRY_METHOD = 5,
    ZOO_ENTRY_NEXT = 6, /* 0 in the terminating entry */
    ZOO_ENTRY_DATA = 10,
    ZOO_ENTRY_DATE = 14,
    ZOO_ENTRY_TIME = 16,
    ZOO_ENTRY_CRC = 18, /* CRC-16/ARC of the member */
    ZOO_ENTRY_SIZE = 20,
    ZOO_ENTRY_PACKED = 24,
    ZOO_ENTRY_DELETED = 30,
    ZOO_ENTRY_SHORT_NAME = 38,
    ZOO_ENTRY_SHORT_NAME_BYTES = 13,
    ZOO_ENTRY_V1_BYTES = 51,
    /* Type 2 entries add a variable part covered by a header CRC. */
    ZOO_ENTRY_VARIABLE_BYTES = 51,
    ZOO_ENTRY_HEADER_CRC = 54,
    ZOO_ENTRY_V2_BYTES = 56,
    ZOO_VARIABLE_NAME_BYTES = 56, /* long name length */
    ZOO_VARIABLE_DIRECTORY_BYTES = 57,
    ZOO_VARIABLE_NAMES = 58, /* long name, then directory */
    ZOO_VARIABLE_LENGTHS = 2,
    ZOO_TYPE_FIRST = 1,
    ZOO_TYPE_LAST = 2,
    ZOO_TYPE_LONG_NAMES = 2,
    ZOO_METHOD_STORED = 0,
    ZOO_METHOD_LZD = 1,
    ZOO_METHOD_LZH = 2,
    ZOO_LZH_DICTIONARY_BITS = 13,
    /* Visited entry offsets, an open-addressed set that detects directory cycles. */
    ZOO_SEEN_BITS = 18,
    ZOO_SEEN_SLOTS = 1 << ZOO_SEEN_BITS
};

int tb_zoo_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    unsigned char global[ZOO_GLOBAL_BYTES];
    if (!tb_stream_read_at(file, 0, global, sizeof global) ||
        tb_le32(global + TB_ZOO_SIGNATURE_AT) != TB_ZOO_SIGNATURE ||
        tb_le32(global + ZOO_GLOBAL_CHECK) != (uint32_t)(0u - tb_le32(global + ZOO_GLOBAL_FIRST)))
        return tb_archive_fail(EINVAL);
    uint64_t at = tb_le32(global + ZOO_GLOBAL_FIRST), length = tb_stream_size(file);
    uint64_t *seen = calloc(ZOO_SEEN_SLOTS, sizeof *seen);
    if (!seen)
        return 0;
    int result = 0;
    for (unsigned count = 0; count < TB_MEDIA_ENTRY_LIMIT; count++)
    {
        unsigned char h[ZOO_ENTRY_V2_BYTES];
        if (at < ZOO_GLOBAL_BYTES || !tb_stream_read_at(file, at, h, ZOO_ENTRY_V1_BYTES) ||
            tb_le32(h + ZOO_ENTRY_SIGNATURE) != TB_ZOO_SIGNATURE)
            break;
        /* Fibonacci hashing: the top bits of the offset times 2^64 / phi. */
        size_t slot = (size_t)((at * UINT64_C(11400714819323198485)) >> (64 - ZOO_SEEN_BITS));
        while (seen[slot] && seen[slot] != at)
            slot = (slot + 1) & (ZOO_SEEN_SLOTS - 1);
        if (seen[slot] == at)
            break;
        seen[slot] = at;
        uint32_t next = tb_le32(h + ZOO_ENTRY_NEXT), data = tb_le32(h + ZOO_ENTRY_DATA),
                 packed = tb_le32(h + ZOO_ENTRY_PACKED);
        if (!next)
        {
            result = 1;
            break;
        }
        if (h[ZOO_ENTRY_TYPE] < ZOO_TYPE_FIRST || h[ZOO_ENTRY_TYPE] > ZOO_TYPE_LAST ||
            h[ZOO_ENTRY_METHOD] > ZOO_METHOD_LZH || next >= length || data > length ||
            packed > length - data)
            break;
        char short_name[ZOO_ENTRY_SHORT_NAME_BYTES + 1];
        memcpy(short_name, h + ZOO_ENTRY_SHORT_NAME, ZOO_ENTRY_SHORT_NAME_BYTES);
        short_name[ZOO_ENTRY_SHORT_NAME_BYTES] = 0;
        char *name = NULL;
        int name_attempted = 0;
        if (h[ZOO_ENTRY_TYPE] == ZOO_TYPE_LONG_NAMES)
        {
            if (!tb_stream_read_at(file, at, h, ZOO_ENTRY_V2_BYTES))
                break;
            unsigned variable = tb_le16(h + ZOO_ENTRY_VARIABLE_BYTES);
            unsigned char *header = malloc(ZOO_ENTRY_V2_BYTES + (size_t)variable);
            int ok = header &&
                     tb_stream_read_at(file, at, header, ZOO_ENTRY_V2_BYTES + (size_t)variable);
            if (ok)
            {
                uint16_t expected = tb_le16(header + ZOO_ENTRY_HEADER_CRC);
                header[ZOO_ENTRY_HEADER_CRC] = header[ZOO_ENTRY_HEADER_CRC + 1] = 0;
                ok = tb_crc16_arc(header, ZOO_ENTRY_V2_BYTES + variable) == expected;
            }
            if (ok && variable >= ZOO_VARIABLE_LENGTHS)
            {
                unsigned names = header[ZOO_VARIABLE_NAME_BYTES],
                         dirs = header[ZOO_VARIABLE_DIRECTORY_BYTES];
                if (names + dirs > variable - ZOO_VARIABLE_LENGTHS)
                    ok = 0;
                else
                {
                    char joined[515];
                    size_t used = 0;
                    if (dirs)
                    {
                        const char *directory = (char *)header + ZOO_VARIABLE_NAMES + names;
                        size_t n = strnlen(directory, dirs);
                        memcpy(joined, directory, n);
                        used = n;
                        if (used && joined[used - 1] != '/' && joined[used - 1] != '\\')
                            joined[used++] = '/';
                    }
                    const char *base = names ? (char *)header + ZOO_VARIABLE_NAMES : short_name;
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
        if (!h[ZOO_ENTRY_DELETED] && name && *name)
        {
            int rc = visit(context, name, tb_le32(h + ZOO_ENTRY_SIZE), 0, file, at, packed,
                           TB_ARCHIVE_ZOO + h[ZOO_ENTRY_METHOD],
                           tb_dos_time(tb_le16(h + ZOO_ENTRY_DATE), tb_le16(h + ZOO_ENTRY_TIME)));
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
    unsigned char h[ZOO_ENTRY_V1_BYTES];
    if (packed > TB_LEGACY_ARCHIVE_LIMIT || size > TB_LEGACY_ARCHIVE_LIMIT)
    {
        tb_archive_fail(EFBIG);
        return NULL;
    }
    if (!tb_stream_read_at(file, offset, h, sizeof h) ||
        tb_le32(h + ZOO_ENTRY_SIGNATURE) != TB_ZOO_SIGNATURE ||
        tb_le32(h + ZOO_ENTRY_PACKED) != packed || tb_le32(h + ZOO_ENTRY_SIZE) != size)
        return NULL;
    unsigned char *input = malloc(packed ? (size_t)packed : 1),
                  *output = malloc(size ? (size_t)size : 1);
    int ok = input && output &&
             tb_stream_read_at(file, tb_le32(h + ZOO_ENTRY_DATA), input, (size_t)packed);
    if (ok && kind == TB_ARCHIVE_ZOO + ZOO_METHOD_STORED)
    {
        ok = packed == size;
        if (ok)
            memcpy(output, input, (size_t)size);
    }
    else if (ok && kind == TB_ARCHIVE_ZOO + ZOO_METHOD_LZD)
        ok = tb_lzd_decode(input, (size_t)packed, output, (size_t)size);
    else if (ok && kind == TB_ARCHIVE_ZOO + ZOO_METHOD_LZH)
        ok = tb_lzh_decode(input, (size_t)packed, output, (size_t)size, ZOO_LZH_DICTIONARY_BITS);
    else
        ok = 0;
    free(input);
    if (!ok || tb_crc16_arc(output, (size_t)size) != tb_le16(h + ZOO_ENTRY_CRC))
    {
        free(output);
        tb_archive_fail(EIO);
        return NULL;
    }
    tb_media_session_stats()->decompressed_bytes += size;
    return tb_stream_memory(output, (size_t)size);
}
