/* Public domain. No rights reserved. Wholly AI-authored. */
/* Metafile objects and helpers shared by WMF and EMF playback, recording and conversion. */
#ifndef TWOINE_PMWINX_METAFILE_OBJECTS_H
#define TWOINE_PMWINX_METAFILE_OBJECTS_H
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include "os2/ps/awe/gdi/dc.h"

typedef struct Open32Metafile
{
    struct Open32Metafile *next;
    ULONG handle, size;
    BOOL enhanced;
    UCHAR data[];
} Open32Metafile;

extern LxHostOnce metafile_once;

extern LxHostMonitor *metafile_monitor;

extern Open32Metafile *metafile_files;

LX_PRIVATE void metafile_initialize(void);

LX_PRIVATE void metafile_destroy_file(void *resource);

LX_PRIVATE BOOL metafile_valid_wmf(const UCHAR *data, ULONG size);

LX_PRIVATE LONG metafile_word(const UCHAR *bytes);

LX_PRIVATE BOOL metafile_save(PSZ name, const uint8 *bytes, ULONG size);

LX_PRIVATE ULONG metafile_read(PSZ name, BOOL enhanced);
#endif
