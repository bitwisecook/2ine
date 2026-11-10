/* Public domain. No rights reserved. Wholly AI-authored. */
/* Metafile objects and helpers shared by WMF and EMF playback, recording and conversion. */
#ifndef TWOINE_PMWINX_METAFILE_OBJECTS_H
#define TWOINE_PMWINX_METAFILE_OBJECTS_H
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include "os2/ps/awe/gdi/dc.h"

enum
{
    /* Larger metafiles, and recordings that would grow past this, are refused. */
    METAFILE_SIZE_MAX = 64 * 1024 * 1024,
    /* Larger point and polygon counts in a record are refused. */
    METAFILE_POINTS_MAX = 65536,
    /* EnumMetaFile and EnumEnhMetaFile call the guest through a thunk (push imm32 ... mov
       eax, imm32; call eax); these are the offsets of its two immediates. */
    ENUM_THUNK_PARAMETER = 4,
    ENUM_THUNK_PROCEDURE = 21
};

/* Windows metafile (WMF) layout: a METAHEADER, then METARECORDs. Header and record
   sizes count 16-bit words; record parameters are 16-bit words stored in reverse
   call order. Types and versions are MS-WMF's MetafileType and MetafileVersion. */
enum
{
    WIN32_METAHEADER_SIZE = 18,
    WIN32_METAHEADER_TYPE = 0,
    WIN32_METAHEADER_HEADER_WORDS = 2,
    WIN32_METAHEADER_VERSION = 4,
    WIN32_METAHEADER_FILE_WORDS = 6,
    WIN32_METAHEADER_OBJECTS = 10,
    WIN32_METAHEADER_MAX_RECORD_WORDS = 12,
    WIN32_METAHEADER_PARAMETERS = 16,
    WIN32_MEMORYMETAFILE = 1,
    WIN32_DISKMETAFILE = 2,
    WIN32_METAVERSION100 = 0x0100,
    WIN32_METAVERSION300 = 0x0300,
    WIN32_METARECORD_WORDS = 0,
    WIN32_METARECORD_FUNCTION = 4,
    WIN32_METARECORD_PARAMETERS = 6,
    /* The end record has no parameters: three words. */
    WIN32_METARECORD_MIN_WORDS = 3,
    /* The smallest metafile: a header and the end record. */
    WMF_SIZE_MIN = WIN32_METAHEADER_SIZE + 2 * WIN32_METARECORD_MIN_WORDS,
    /* META_CREATEREGION's Region object (MS-WMF): a 22-byte header with the object type,
       byte size, scan count and longest scan, then scans of a count, top, bottom, x pairs
       and the count again. */
    WMF_REGION_SIZE = 22,
    WMF_REGION_OBJECT_TYPE = 2,
    WMF_REGION_BYTES = 8,
    WMF_REGION_SCANS = 10,
    WMF_REGION_MAX_SCAN = 12,
    WMF_REGION_TYPE = 6,
    /* WMF LOGFONT: the five size fields are 16-bit, then lfItalic onward as in LOGFONT. */
    WIN32_LOGFONT16_SIZE = 50,
    WIN32_LOGFONT16_ITALIC = 10,
    WIN32_LOGFONT16_FACE_NAME = 18,
    /* An object index with bit 15 set names a stock object; for META_SELECTCLIPREGION it
       selects no region. */
    WMF_STOCK_OBJECT_INDEX = 0x8000,
    /* The record number played and converted as ScaleWindowExtEx. Windows writes
       META_SCALEWINDOWEXT (0x0410) for that call. */
    WMF_SCALEWINDOWEXT_RECORD = 0x0400
};

/* Enhanced metafile (EMF) layout: an ENHMETAHEADER record, then EMR records ending
   with EMR_EOF. Record sizes count bytes and are multiples of four. */
enum
{
    WIN32_EMR_SIZE = 8,
    WIN32_EMR_TYPE = 0,
    WIN32_EMR_BYTES = 4,
    WIN32_ENHMETAHEADER_SIZE = 88,
    WIN32_ENHMETAHEADER_HEADER_BYTES = 4,
    WIN32_ENHMETAHEADER_BOUNDS = 8,
    WIN32_ENHMETAHEADER_FRAME = 24,
    WIN32_ENHMETAHEADER_SIGNATURE = 40,
    WIN32_ENHMETAHEADER_VERSION = 44,
    WIN32_ENHMETAHEADER_FILE_BYTES = 48,
    WIN32_ENHMETAHEADER_RECORDS = 52,
    WIN32_ENHMETAHEADER_HANDLES = 56,
    WIN32_ENHMETAHEADER_DEVICE = 72,
    WIN32_ENHMETAHEADER_MILLIMETERS = 80,
    WIN32_EMREOF_SIZE = 20,
    /* The smallest enhanced metafile: a header and the end record. */
    EMF_SIZE_MIN = WIN32_ENHMETAHEADER_SIZE + WIN32_EMREOF_SIZE,
    WIN32_EMREOF_SIZE_LAST = 16,
    /* EMR_GDICOMMENT: cbData, then the comment; public comments start with
       GDICOMMENT_IDENTIFIER and a comment type. */
    WIN32_EMRGDICOMMENT_DATA_BYTES = 8,
    WIN32_EMRGDICOMMENT_DATA = 12,
    WIN32_EMRGDICOMMENT_TYPE = 16,
    /* GDICOMMENT_MULTIFORMATS: rclOutput, nFormats, then EMRFORMAT entries whose offData
       counts from the comment data. */
    WIN32_EMRGDICOMMENT_MULTIFORMATS_OUTPUT = 20,
    WIN32_EMRGDICOMMENT_MULTIFORMATS_FORMATS = 36,
    WIN32_EMRGDICOMMENT_MULTIFORMATS_SIZE = 40,
    WIN32_EMRFORMAT_SIZE = 16,
    WIN32_EMRFORMAT_SIGNATURE = 0,
    WIN32_EMRFORMAT_DATA_BYTES = 8,
    WIN32_EMRFORMAT_DATA_OFFSET = 12,
    /* GDICOMMENT_WINDOWS_METAFILE: version, checksum, flags, then the WMF bytes. */
    WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_VERSION = 20,
    WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_BYTES = 32,
    WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_DATA = 36,
    /* EMR_POLYLINE and kin: rclBounds, cptl, then the points. EMR_POLYPOLYLINE and kin:
       rclBounds, nPolys, cptl, the per-polygon counts, then the points. */
    WIN32_EMRPOLYLINE_COUNT = 24,
    WIN32_EMRPOLYLINE_POINTS = 28,
    WIN32_EMRPOLYPOLYLINE_POLYGONS = 24,
    WIN32_EMRPOLYPOLYLINE_COUNT = 28,
    WIN32_EMRPOLYPOLYLINE_COUNTS = 32,
    /* EMR_EXTCREATEFONTINDIRECTW: ihFont, then a LOGFONTW. */
    WIN32_EMREXTCREATEFONTINDIRECTW_LOGFONT = 12,
    /* RGNDATA: a 32-byte RGNDATAHEADER with nCount at 8, then RECTs. */
    WIN32_RGNDATAHEADER_SIZE = 32,
    WIN32_RGNDATAHEADER_COUNT = 8
};

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
