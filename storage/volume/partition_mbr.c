/* Public domain. No rights reserved. Wholly AI-authored. */
/* Master boot record partition-table scan shared by the volume probe and filesystems. */
#include "storage/volume/partition_mbr.h"
#include "formats/endian.h"

int tb_mbr_partitions(const unsigned char *sector, TbMbrPartition partitions[MBR_PARTITIONS])
{
    if (tb_le16(sector + MBR_SIGNATURE_OFFSET) != MBR_SIGNATURE)
        return 0;
    for (unsigned i = 0; i < MBR_PARTITIONS; i++)
    {
        const unsigned char *entry = sector + MBR_PARTITION_TABLE + i * MBR_ENTRY_BYTES;
        partitions[i] = (TbMbrPartition){.type = entry[MBR_ENTRY_TYPE],
                                         .first_sector = tb_le32(entry + MBR_ENTRY_FIRST_SECTOR),
                                         .sectors = tb_le32(entry + MBR_ENTRY_SECTORS)};
    }
    return 1;
}
