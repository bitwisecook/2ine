/* Public domain. No rights reserved. Wholly AI-authored. */
/* ISO 9660 and Joliet volume descriptors and directory records. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/filesystems/iso9660/iso9660.h"
#include "storage/common/file_time.h"
#include "formats/endian.h"
#include <errno.h>
#include <string.h>

/* Descriptors examined before the set is treated as unterminated. */
#define ISO_DESCRIPTOR_LIMIT 144u

/* Volume descriptor fields (ECMA-119 8). */
enum
{
    ISO_VD_TYPE = 0,
    ISO_VD_VERSION = 6,
    ISO_VD_VOLUME_ID = 40,
    ISO_VD_VOLUME_SPACE = 80, /* both-endian; the little-endian half comes first */
    ISO_VD_ESCAPES = 88,      /* Joliet: "%/@", "%/C" or "%/E" for UCS-2 levels 1-3 */
    ISO_VD_BLOCK_BYTES = 128,
    ISO_VD_ROOT_RECORD = 156,
    ISO_VD_PRIMARY = 1,
    ISO_VD_SUPPLEMENTARY = 2,
    ISO_VD_TERMINATOR = 255,
    ISO_LABEL_BYTES = 11 /* the part of the 32-byte volume identifier kept as a label */
};

/* Directory record fields (ECMA-119 9.1). */
enum
{
    ISO_RECORD_LENGTH = 0,
    ISO_RECORD_EXTENDED_LENGTH = 1,
    ISO_RECORD_EXTENT = 2,
    ISO_RECORD_DATA_LENGTH = 10,
    ISO_RECORD_DATE = 18,
    ISO_RECORD_FLAGS = 25,
    ISO_RECORD_UNIT_SIZE = 26,
    ISO_RECORD_INTERLEAVE = 27,
    ISO_RECORD_NAME_LENGTH = 32,
    ISO_RECORD_NAME = 33,
    ISO_RECORD_MINIMUM = 34, /* fixed fields and a one-byte name */
    ISO_FLAG_DIRECTORY = 0x02,
    ISO_FLAG_MULTI_EXTENT = 0x80
};

static int iso_node(VolumeDisk *d, const unsigned char *r, size_t length, VolumeNode *node)
{
    unsigned record = r[ISO_RECORD_LENGTH];
    if (length < ISO_RECORD_MINIMUM || record < ISO_RECORD_MINIMUM || record > length ||
        r[ISO_RECORD_NAME_LENGTH] > record - ISO_RECORD_NAME)
        return volume_failure(EINVAL);
    /* Interleaved and multi-extent files are not contiguous. */
    if (r[ISO_RECORD_UNIT_SIZE] || r[ISO_RECORD_INTERLEAVE] ||
        (r[ISO_RECORD_FLAGS] & ISO_FLAG_MULTI_EXTENT))
        return volume_failure(ENOTSUP);
    /* An extended attribute record, if any, fills the first sectors of the extent. */
    node->offset = ((uint64_t)tb_le32(r + ISO_RECORD_EXTENT) + r[ISO_RECORD_EXTENDED_LENGTH]) *
                   ISO_SECTOR_BYTES;
    node->size = tb_le32(r + ISO_RECORD_DATA_LENGTH);
    node->directory = !!(r[ISO_RECORD_FLAGS] & ISO_FLAG_DIRECTORY);
    node->cluster = 0;
    node->modified = tb_iso_time(r + ISO_RECORD_DATE);
    if (node->offset > d->bytes || node->size > d->bytes - node->offset)
        return volume_failure(EIO);
    return 1;
}

int iso_identify(VolumeDisk *d)
{
    unsigned char b[ISO_SECTOR_BYTES];
    int primary = 0;
    for (unsigned i = ISO_DESCRIPTOR_SECTOR;
         i < ISO_DESCRIPTOR_LIMIT && (uint64_t)(i + 1) * ISO_SECTOR_BYTES <= d->bytes; i++)
    {
        if (!volume_read_at(d, (uint64_t)i * ISO_SECTOR_BYTES, b, sizeof b))
            return 0;
        if (memcmp(b + ISO_STANDARD_ID_OFFSET, ISO_STANDARD_ID, ISO_STANDARD_ID_BYTES) ||
            b[ISO_VD_VERSION] != 1)
            return volume_failure(EINVAL);
        unsigned type = b[ISO_VD_TYPE];
        if (type == ISO_VD_TERMINATOR)
            break;
        const unsigned char *escapes = b + ISO_VD_ESCAPES;
        int joliet = type == ISO_VD_SUPPLEMENTARY && escapes[0] == '%' && escapes[1] == '/' &&
                     (escapes[2] == '@' || escapes[2] == 'C' || escapes[2] == 'E');
        if (type != ISO_VD_PRIMARY && !joliet)
            continue;
        uint64_t volume_bytes = (uint64_t)tb_le32(b + ISO_VD_VOLUME_SPACE) * ISO_SECTOR_BYTES;
        if (tb_le16(b + ISO_VD_BLOCK_BYTES) != ISO_SECTOR_BYTES || !volume_bytes ||
            volume_bytes > d->bytes)
            return volume_failure(EINVAL);
        d->bytes = volume_bytes;
        /* A Joliet root replaces the primary one, for its Unicode long names. */
        if ((type == ISO_VD_PRIMARY && !d->joliet) || joliet)
        {
            if (!iso_node(d, b + ISO_VD_ROOT_RECORD, ISO_SECTOR_BYTES - ISO_VD_ROOT_RECORD,
                          &d->root) ||
                !d->root.directory)
                return volume_failure(EINVAL);
            d->joliet = joliet;
        }
        if (type == ISO_VD_PRIMARY)
        {
            primary = 1;
            memcpy(d->label, b + ISO_VD_VOLUME_ID, ISO_LABEL_BYTES);
            d->label[ISO_LABEL_BYTES] = 0;
            for (int j = ISO_LABEL_BYTES - 1; j >= 0 && d->label[j] == ' '; j--)
                d->label[j] = 0;
        }
    }
    return primary ? 1 : volume_failure(EINVAL);
}

int iso_read_entries(VolumeDisk *d, const unsigned char *data, size_t size,
                     VolumeEntryCallback callback, void *context)
{
    int ok = 1;
    for (size_t at = 0; at < size;)
    {
        char name[1024];
        VolumeNode node = {0};
        unsigned length = data[at + ISO_RECORD_LENGTH];
        /* Records never span sectors; zero padding fills the rest of one. */
        if (!length)
        {
            at += ISO_SECTOR_BYTES - at % ISO_SECTOR_BYTES;
            continue;
        }
        if (length > size - at || length > ISO_SECTOR_BYTES - at % ISO_SECTOR_BYTES ||
            !iso_node(d, data + at, length, &node))
        {
            ok = volume_failure(EINVAL);
            break;
        }
        unsigned n = data[at + ISO_RECORD_NAME_LENGTH];
        const unsigned char *raw = data + at + ISO_RECORD_NAME;
        /* Single-byte names 0 and 1 are the "." and ".." entries. */
        if (n == 1 && raw[0] < 2)
        {
            at += length;
            continue;
        }
        if (d->joliet)
        {
            if (n % 2)
            {
                ok = volume_failure(EINVAL);
                break;
            }
            /* Joliet names are big-endian UCS-2. */
            uint16_t units[128];
            for (unsigned i = 0; i < n / 2; i++)
                units[i] = tb_be16(raw + i * 2);
            if (!volume_utf16_name(units, n / 2, name, sizeof name))
            {
                ok = 0;
                break;
            }
        }
        else
        {
            memcpy(name, raw, n);
            name[n] = 0;
        }
        /* Drop the ";1" version suffix and the dot of an extensionless name. */
        char *version = strrchr(name, ';');
        if (version)
            *version = 0;
        size_t len = strlen(name);
        if (len && name[len - 1] == '.')
            name[len - 1] = 0;
        at += length;
        if (!volume_safe_name(name))
            continue;
        int result = callback(context, name, &node);
        if (result <= 0)
        {
            ok = result == 0;
            break;
        }
    }
    return ok;
}
