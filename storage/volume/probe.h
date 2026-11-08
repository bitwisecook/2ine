/* Public domain. No rights reserved. Wholly AI-authored. */
/* One read of the first sectors of a medium, reporting the boot sector, partition table,
   HPFS superblock and ISO 9660 descriptor found there. Callers apply their own acceptance
   rules to the report. */
#ifndef TB_STORAGE_PROBE_H
#define TB_STORAGE_PROBE_H
#include "storage/read_stream.h"
#include "storage/volume/partition_mbr.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Boot sector and HPFS sector size. */
#define PROBE_SECTOR_BYTES 512u

typedef struct TbVolumeProbe
{
    uint64_t bytes;
    /* Sector 0, zero-padded past the end of the medium or after a failed read. boot_bytes
       counts the bytes read; boot_error is errno from a failed read, otherwise 0. */
    unsigned char boot[PROBE_SECTOR_BYTES];
    size_t boot_bytes;
    int boot_error;
    /* The following boot-sector facts are set only for a complete 512-byte sector 0. */
    int boot_signature; /* bytes 55 AA at offset 510 */
    int x86_jump;       /* starts with a short or near x86 jump */
    int exfat;          /* "EXFAT   " file system name */
    /* Boot signature, a BPB with no fixed root directory and no 16-bit FAT size (the FAT32
       layout), and at least 512 bytes per sector. */
    int fat32;
    /* Bytes per sector from the exFAT shift or the BPB, when 512, 1024, 2048 or 4096;
       otherwise 0. */
    unsigned sector_bytes;
    /* The partition table, read whenever the boot signature is present. */
    int mbr;
    TbMbrPartition partitions[MBR_PARTITIONS];
    /* An HPFS superblock at 512-byte sector 16 of a medium holding sectors 0-17. */
    int hpfs;
    unsigned char hpfs_super[PROBE_SECTOR_BYTES];
    /* Bytes read of the first ISO 9660 volume descriptor (2048-byte sector 16), which is
       truncated at the end of the medium, and whether they carry the "CD001" identifier. */
    size_t iso_bytes;
    int iso_standard_id;
} TbVolumeProbe;

/* read returns 1 after reading every requested byte, or 0 with errno. */
void tb_volume_probe(TbReadAt read, void *context, uint64_t bytes, TbVolumeProbe *probe);
/* Probes a stdio file of the given size from its start. */
void tb_volume_probe_file(FILE *file, uint64_t bytes, TbVolumeProbe *probe);
#endif
