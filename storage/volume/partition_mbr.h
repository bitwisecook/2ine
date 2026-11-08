/* Public domain. No rights reserved. Wholly AI-authored. */
/* Master boot record partition table. */
#ifndef TB_STORAGE_PARTITION_MBR_H
#define TB_STORAGE_PARTITION_MBR_H
#include <stdint.h>

/* Sector 0 layout: boot code, four 16-byte primary entries, then bytes 55 AA. */
enum
{
    MBR_SECTOR_BYTES = 512,
    MBR_PARTITION_TABLE = 446,
    MBR_ENTRY_BYTES = 16,
    MBR_PARTITIONS = 4,
    MBR_ENTRY_TYPE = 4,
    MBR_ENTRY_FIRST_SECTOR = 8,
    MBR_ENTRY_SECTORS = 12,
    MBR_SIGNATURE_OFFSET = 510
};

#define MBR_SIGNATURE 0xaa55u

/* Partition type bytes. Type 07 is shared by HPFS, NTFS and exFAT. */
enum
{
    MBR_TYPE_IFS = 0x07,
    MBR_TYPE_FAT32 = 0x0b,
    MBR_TYPE_FAT32_LBA = 0x0c
};

/* One primary entry; first_sector and sectors count 512-byte sectors. */
typedef struct TbMbrPartition
{
    unsigned type;
    uint32_t first_sector, sectors;
} TbMbrPartition;

/* Reads the four primary entries of a sector that ends in 55 AA, empty entries (type 0)
   included. Returns 0 and leaves partitions unchanged when the signature is missing. */
int tb_mbr_partitions(const unsigned char *sector, TbMbrPartition partitions[MBR_PARTITIONS]);
#endif
