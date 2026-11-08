/* Public domain. No rights reserved. Wholly AI-authored. */
/* Boot sector, MBR, HPFS superblock and ISO 9660 descriptor probing of in-memory media. */
#include "storage/volume/probe.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum
{
    HPFS_SUPER_AT = 16 * 512,
    ISO_DESCRIPTOR_AT = 16 * 2048,
    MEDIUM_BYTES = ISO_DESCRIPTOR_AT + 2048
};

static unsigned char medium[MEDIUM_BYTES];
static int fail_reads;

static int memory_read(void *context, uint64_t offset, void *data, size_t size)
{
    (void)context;
    if (fail_reads || offset > sizeof medium || size > sizeof medium - offset)
    {
        errno = EIO;
        return 0;
    }
    memcpy(data, medium + offset, size);
    return 1;
}

static void put16(unsigned at, unsigned value)
{
    medium[at] = (unsigned char)value;
    medium[at + 1] = (unsigned char)(value >> 8);
}

static void put32(unsigned at, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        medium[at + i] = (unsigned char)(value >> (8 * i));
}

static void probe(uint64_t bytes, TbVolumeProbe *result)
{
    errno = 0;
    tb_volume_probe(memory_read, NULL, bytes, result);
    CHECK(errno == 0);
    CHECK(result->bytes == bytes);
}

static void boot_sectors(void)
{
    TbVolumeProbe p;
    memset(medium, 0, sizeof medium);
    probe(0, &p);
    CHECK(!p.boot_bytes && !p.boot_error && !p.boot_signature && !p.mbr && !p.iso_bytes);

    /* A FAT12 floppy boot sector: short jump, BPB with a fixed root directory. */
    medium[0] = 0xeb;
    put16(11, 512);
    put16(17, 224);
    put16(22, 9);
    put16(510, 0xaa55);
    probe(300, &p);
    CHECK(p.boot_bytes == 300 && !p.boot_signature && !p.x86_jump && !p.sector_bytes);
    CHECK(p.boot[0] == 0xeb && p.boot[299] == 0 && p.boot[510] == 0);
    probe(1440 * 1024 < MEDIUM_BYTES ? 1440 * 1024 : MEDIUM_BYTES, &p);
    CHECK(p.boot_bytes == 512 && p.boot_signature && p.x86_jump && !p.fat32 && !p.exfat);
    CHECK(p.sector_bytes == 512 && p.mbr && !p.hpfs);

    /* FAT32 layout: no fixed root directory and no 16-bit FAT size. */
    put16(17, 0);
    put16(22, 0);
    put16(11, 4096);
    probe(MEDIUM_BYTES, &p);
    CHECK(p.fat32 && p.sector_bytes == 4096);
    put16(11, 256);
    probe(MEDIUM_BYTES, &p);
    CHECK(!p.fat32 && !p.sector_bytes);
    put16(11, 512);
    put16(510, 0);
    probe(MEDIUM_BYTES, &p);
    CHECK(!p.fat32 && !p.boot_signature && !p.mbr);

    /* exFAT names itself and gives the sector size as a power of two. */
    memcpy(medium + 3, "EXFAT   ", 8);
    put16(11, 0);
    medium[108] = 12;
    probe(MEDIUM_BYTES, &p);
    CHECK(p.exfat && p.sector_bytes == 4096 && !p.boot_signature);
    medium[108] = 13;
    probe(MEDIUM_BYTES, &p);
    CHECK(p.exfat && !p.sector_bytes);

    fail_reads = 1;
    probe(MEDIUM_BYTES, &p);
    fail_reads = 0;
    CHECK(!p.boot_bytes && p.boot_error == EIO && !p.exfat && !p.boot[3]);
}

static void partitions(void)
{
    TbVolumeProbe p;
    memset(medium, 0, sizeof medium);
    put16(510, 0xaa55);
    medium[446 + 4] = 0x07;
    put32(446 + 8, 63);
    put32(446 + 12, 40000);
    medium[446 + 3 * 16 + 4] = 0x0c;
    put32(446 + 3 * 16 + 8, 0x01020304);
    put32(446 + 3 * 16 + 12, 0x05060708);
    probe(MEDIUM_BYTES, &p);
    CHECK(p.mbr && !p.x86_jump);
    CHECK(p.partitions[0].type == 0x07 && p.partitions[0].first_sector == 63 &&
          p.partitions[0].sectors == 40000);
    CHECK(!p.partitions[1].type && !p.partitions[2].type);
    CHECK(p.partitions[3].type == 0x0c && p.partitions[3].first_sector == 0x01020304 &&
          p.partitions[3].sectors == 0x05060708);

    TbMbrPartition table[MBR_PARTITIONS] = {{0x55, 1, 2}};
    put16(510, 0x55aa);
    CHECK(!tb_mbr_partitions(medium, table) && table[0].type == 0x55);
}

static void hpfs_superblock(void)
{
    TbVolumeProbe p;
    memset(medium, 0, sizeof medium);
    put32(HPFS_SUPER_AT, 0xf995e849u);
    medium[HPFS_SUPER_AT + 4] = 0x5a;
    probe(18 * 512, &p);
    CHECK(p.hpfs && p.hpfs_super[4] == 0x5a);
    /* The spare block at sector 17 must also fit. */
    probe(18 * 512 - 1, &p);
    CHECK(!p.hpfs);
    put32(HPFS_SUPER_AT, 0xf995e848u);
    probe(18 * 512, &p);
    CHECK(!p.hpfs);
}

static void iso_descriptor(void)
{
    TbVolumeProbe p;
    memset(medium, 0, sizeof medium);
    medium[ISO_DESCRIPTOR_AT] = 1;
    memcpy(medium + ISO_DESCRIPTOR_AT + 1, "CD001", 5);
    medium[ISO_DESCRIPTOR_AT + 6] = 1;
    probe(MEDIUM_BYTES, &p);
    CHECK(p.iso_bytes == 2048 && p.iso_standard_id);
    /* A truncated descriptor reports how much of it the medium holds. */
    probe(ISO_DESCRIPTOR_AT + 7, &p);
    CHECK(p.iso_bytes == 7 && p.iso_standard_id);
    probe(ISO_DESCRIPTOR_AT + 6, &p);
    CHECK(p.iso_bytes == 6 && p.iso_standard_id);
    probe(ISO_DESCRIPTOR_AT + 5, &p);
    CHECK(p.iso_bytes == 5 && !p.iso_standard_id);
    probe(ISO_DESCRIPTOR_AT, &p);
    CHECK(!p.iso_bytes && !p.iso_standard_id);
    medium[ISO_DESCRIPTOR_AT + 5] = '2';
    probe(MEDIUM_BYTES, &p);
    CHECK(p.iso_bytes == 2048 && !p.iso_standard_id);
}

int main(void)
{
    boot_sectors();
    partitions();
    hpfs_superblock();
    iso_descriptor();
    puts("volume probe tests passed");
    return 0;
}
