/* Public domain. No rights reserved. Wholly AI-authored. */
/* ISO 9660 and Joliet reader used by the built-in volume layer. */
#ifndef TB_STORAGE_ISO9660_H
#define TB_STORAGE_ISO9660_H
#include "storage/volume/volume_disk.h"
#define ISO_SECTOR_BYTES 2048u

/* Reports whether a volume descriptor set starts at sector 16. */
int iso_present(VolumeDisk *d);
/* Reads the descriptor set and selects the Joliet or primary root directory. */
int iso_identify(VolumeDisk *d);
int iso_read_entries(VolumeDisk *d, const unsigned char *data, size_t size,
                     VolumeEntryCallback callback, void *context);
#endif
