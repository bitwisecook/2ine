/* Public domain. No rights reserved. Wholly AI-authored. */
/* Boot sector, partition table, HPFS superblock and ISO 9660 descriptor sniffing. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/volume/probe.h"
#include "storage/filesystems/fat/fat12_16.h"
#include "storage/filesystems/hpfs/hpfs_internal.h"
#include "storage/filesystems/iso9660/iso9660.h"
#include "storage/common/host_stdio.h"
#include "formats/endian.h"
#include <errno.h>
#include <string.h>

/* x86 jump opcodes that start a bootable boot sector. */
enum
{
    X86_JMP_SHORT = 0xeb,
    X86_JMP_NEAR = 0xe9
};

/* exFAT boot sector: file system name and log2 of the bytes per sector. */
#define EXFAT_NAME "EXFAT   "

enum
{
    EXFAT_NAME_OFFSET = 3,
    EXFAT_NAME_BYTES = 8,
    EXFAT_SECTOR_SHIFT = 108,
    EXFAT_SECTOR_SHIFT_MIN = 9,
    EXFAT_SECTOR_SHIFT_MAX = 12
};

#define ISO_DESCRIPTOR_OFFSET ((uint64_t)ISO_DESCRIPTOR_SECTOR * ISO_SECTOR_BYTES)

static int valid_sector_bytes(unsigned bytes)
{
    return bytes == 512 || bytes == 1024 || bytes == 2048 || bytes == 4096;
}

static void boot_facts(TbVolumeProbe *probe)
{
    const unsigned char *b = probe->boot;
    probe->boot_signature = tb_le16(b + FAT_BOOT_SIGNATURE_OFFSET) == FAT_BOOT_SIGNATURE;
    probe->x86_jump = b[0] == X86_JMP_SHORT || b[0] == X86_JMP_NEAR;
    probe->exfat = !memcmp(b + EXFAT_NAME_OFFSET, EXFAT_NAME, EXFAT_NAME_BYTES);
    unsigned bpb_sector = tb_le16(b + FAT_BPB_SECTOR_BYTES);
    probe->fat32 = probe->boot_signature && !tb_le16(b + FAT_BPB_ROOT_ENTRIES) &&
                   !tb_le16(b + FAT_BPB_FAT_SECTORS) && bpb_sector >= 512;
    unsigned shift = b[EXFAT_SECTOR_SHIFT];
    unsigned sector =
        probe->exfat && shift >= EXFAT_SECTOR_SHIFT_MIN && shift <= EXFAT_SECTOR_SHIFT_MAX
            ? 1u << shift
            : bpb_sector;
    probe->sector_bytes = valid_sector_bytes(sector) ? sector : 0;
    probe->mbr = tb_mbr_partitions(b, probe->partitions);
}

void tb_volume_probe(TbReadAt read, void *context, uint64_t bytes, TbVolumeProbe *probe)
{
    int saved = errno;
    memset(probe, 0, sizeof *probe);
    probe->bytes = bytes;
    size_t boot = bytes < PROBE_SECTOR_BYTES ? (size_t)bytes : PROBE_SECTOR_BYTES;
    if (boot && !read(context, 0, probe->boot, boot))
    {
        probe->boot_error = errno;
        memset(probe->boot, 0, sizeof probe->boot);
    }
    else
        probe->boot_bytes = boot;
    if (probe->boot_bytes == PROBE_SECTOR_BYTES)
        boot_facts(probe);
    /* The superblock is followed by the spare block, so both sectors must fit. */
    if (bytes >= (uint64_t)HPFS_FIRST_DATA_SECTOR * PROBE_SECTOR_BYTES &&
        read(context, (uint64_t)HPFS_SUPERBLOCK_SECTOR * PROBE_SECTOR_BYTES, probe->hpfs_super,
             PROBE_SECTOR_BYTES))
        probe->hpfs = tb_le32(probe->hpfs_super) == HPFS_SUPERBLOCK_SIGNATURE;
    if (bytes > ISO_DESCRIPTOR_OFFSET)
    {
        unsigned char descriptor[ISO_SECTOR_BYTES];
        size_t n = bytes - ISO_DESCRIPTOR_OFFSET < sizeof descriptor
                       ? (size_t)(bytes - ISO_DESCRIPTOR_OFFSET)
                       : sizeof descriptor;
        if (read(context, ISO_DESCRIPTOR_OFFSET, descriptor, n))
        {
            probe->iso_bytes = n;
            probe->iso_standard_id = n >= ISO_STANDARD_ID_OFFSET + ISO_STANDARD_ID_BYTES &&
                                     !memcmp(descriptor + ISO_STANDARD_ID_OFFSET, ISO_STANDARD_ID,
                                             ISO_STANDARD_ID_BYTES);
        }
    }
    errno = saved;
}

static int file_read(void *context, uint64_t offset, void *data, size_t size)
{
    FILE *file = context;
    if (offset > INT64_MAX || fseeko(file, (int64_t)offset, SEEK_SET) ||
        fread(data, 1, size, file) != size)
    {
        errno = EIO;
        return 0;
    }
    return 1;
}

void tb_volume_probe_file(FILE *file, uint64_t bytes, TbVolumeProbe *probe)
{
    tb_volume_probe(file_read, file, bytes, probe);
}
