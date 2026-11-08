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

static int iso_node(VolumeDisk *d, const unsigned char *r, size_t length, VolumeNode *node)
{
    if (length < 34 || r[0] < 34 || r[0] > length || r[32] > r[0] - 33)
        return volume_failure(EINVAL);
    if (r[26] || r[27] || (r[25] & 0x80))
        return volume_failure(ENOTSUP);
    node->offset = ((uint64_t)tb_le32(r + 2) + r[1]) * ISO_SECTOR_BYTES;
    node->size = tb_le32(r + 10);
    node->directory = !!(r[25] & 2);
    node->cluster = 0;
    node->modified = tb_iso_time(r + 18);
    if (node->offset > d->bytes || node->size > d->bytes - node->offset)
        return volume_failure(EIO);
    return 1;
}

int iso_present(VolumeDisk *d)
{
    unsigned char b[ISO_SECTOR_BYTES];
    return d->bytes >= 17 * ISO_SECTOR_BYTES &&
           volume_read_at(d, 16 * ISO_SECTOR_BYTES, b, sizeof b) && !memcmp(b + 1, "CD001", 5);
}

int iso_identify(VolumeDisk *d)
{
    unsigned char b[ISO_SECTOR_BYTES];
    int primary = 0;
    for (unsigned i = 16; i < 144 && (uint64_t)(i + 1) * ISO_SECTOR_BYTES <= d->bytes; i++)
    {
        if (!volume_read_at(d, (uint64_t)i * ISO_SECTOR_BYTES, b, sizeof b))
            return 0;
        if (memcmp(b + 1, "CD001", 5) || b[6] != 1)
            return volume_failure(EINVAL);
        if (b[0] == 255)
            break;
        int joliet = b[0] == 2 && b[88] == '%' && b[89] == '/' &&
                     (b[90] == '@' || b[90] == 'C' || b[90] == 'E');
        if (b[0] != 1 && !joliet)
            continue;
        uint64_t volume_bytes = (uint64_t)tb_le32(b + 80) * ISO_SECTOR_BYTES;
        if (tb_le16(b + 128) != ISO_SECTOR_BYTES || !volume_bytes || volume_bytes > d->bytes)
            return volume_failure(EINVAL);
        d->bytes = volume_bytes;
        if ((b[0] == 1 && !d->joliet) || joliet)
        {
            if (!iso_node(d, b + 156, ISO_SECTOR_BYTES - 156, &d->root) || !d->root.directory)
                return volume_failure(EINVAL);
            d->joliet = joliet;
        }
        if (b[0] == 1)
        {
            primary = 1;
            memcpy(d->label, b + 40, 11);
            d->label[11] = 0;
            for (int j = 10; j >= 0 && d->label[j] == ' '; j--)
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
        unsigned length = data[at];
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
        unsigned n = data[at + 32];
        const unsigned char *raw = data + at + 33;
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
            uint16_t units[128];
            for (unsigned i = 0; i < n / 2; i++)
                units[i] = (uint16_t)(raw[i * 2] << 8 | raw[i * 2 + 1]);
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
