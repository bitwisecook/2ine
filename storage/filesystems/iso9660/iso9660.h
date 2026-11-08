/* Public domain. No rights reserved. Wholly AI-authored. */
/* ISO 9660 and Joliet reader used by the built-in volume layer. */
#ifndef TB_STORAGE_ISO9660_H
#define TB_STORAGE_ISO9660_H
#include "storage/volume/volume_disk.h"
#define ISO_SECTOR_BYTES 2048u
/* The volume descriptor set starts after the 16-sector system area. */
#define ISO_DESCRIPTOR_SECTOR 16u
/* Every volume descriptor starts with a type byte, this identifier and a version byte. */
#define ISO_STANDARD_ID "CD001"
#define ISO_STANDARD_ID_OFFSET 1u
#define ISO_STANDARD_ID_BYTES 5u
#define ISO_DESCRIPTOR_HEADER_BYTES 7u

/* Reads the descriptor set and selects the Joliet or primary root directory. */
int iso_identify(VolumeDisk *d);
int iso_read_entries(VolumeDisk *d, const unsigned char *data, size_t size,
                     VolumeEntryCallback callback, void *context);
#endif
