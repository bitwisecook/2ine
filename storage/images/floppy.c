/* Public domain. No rights reserved. Wholly AI-authored. */
/* Formats blank FAT12 floppy images in the standard PC geometries. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/volume.h"
#include "storage/common/host.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int tb_image_create_floppy(const char *path, unsigned kib)
{
    const struct Geometry
    {
        unsigned kib, sectors, cluster, fat, roots, track, media;
    } formats[] = {{360, 720, 2, 2, 112, 9, 0xfd},
                   {720, 1440, 2, 3, 112, 9, 0xf9},
                   {1200, 2400, 1, 7, 224, 15, 0xf9},
                   {1440, 2880, 1, 9, 224, 18, 0xf0},
                   {2880, 5760, 2, 9, 240, 36, 0xf0}};
    const struct Geometry *g = NULL;
    for (size_t i = 0; i < sizeof formats / sizeof formats[0]; i++)
        if (formats[i].kib == kib)
            g = &formats[i];
    if (!g)
    {
        errno = EINVAL;
        return 0;
    }
    size_t size = (size_t)g->sectors * 512;
    unsigned char *bytes = calloc(size, 1);
    if (!bytes)
        return 0;
    memcpy(bytes,
           "\xeb\x3c\x90"
           "2INEFAT ",
           11);
    tb_put_le16(bytes + 11, 512);
    bytes[13] = g->cluster;
    tb_put_le16(bytes + 14, 1);
    bytes[16] = 2;
    tb_put_le16(bytes + 17, g->roots);
    tb_put_le16(bytes + 19, g->sectors);
    bytes[21] = g->media;
    tb_put_le16(bytes + 22, g->fat);
    tb_put_le16(bytes + 24, g->track);
    tb_put_le16(bytes + 26, 2);
    bytes[38] = 0x29;
    tb_put_le32(bytes + 39, (uint32_t)time(NULL));
    memcpy(bytes + 43, "NO NAME    FAT12   ", 19);
    /* A formatted data disk; its boot sector stops instead of executing zeros. */
    memcpy(bytes + 62, "\xfa\xf4\xeb\xfd", 4);
    bytes[510] = 0x55;
    bytes[511] = 0xaa;
    for (unsigned i = 0; i < 2; i++)
    {
        size_t at = (1 + i * g->fat) * 512;
        bytes[at] = g->media;
        bytes[at + 1] = bytes[at + 2] = 0xff;
    }
    FILE *file = tb_host_fopen(path, "wbx");
    int ok = file != NULL;
    if (file)
    {
        ok = fwrite(bytes, 1, size, file) == size;
        if (fclose(file))
            ok = 0;
        if (!ok)
            remove(path);
    }
    free(bytes);
    return ok;
}
