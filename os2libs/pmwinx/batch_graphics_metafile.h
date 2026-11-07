/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_BATCH_METAFILE_H
#define TWOINE_OPEN32_BATCH_METAFILE_H
BOOL open32MetafilePolyPolygon(ULONG dc, PVOID points, PVOID counts, LONG polygons);
BOOL open32IsMetafile(ULONG dc);
void open32MetafileDib(ULONG dc, LONG x, LONG y, LONG w, LONG h, LONG sx, LONG sy, LONG sw, LONG sh,
                       PVOID bits, PVOID information, ULONG usage, ULONG operation);
void open32MetafilePatBlt(ULONG dc, LONG x, LONG y, LONG w, LONG h, ULONG operation);
void open32MetafilePixel(ULONG dc, LONG x, LONG y, ULONG color);
void open32MetafileColor(ULONG dc, USHORT function, ULONG color);
void open32MetafileWords(ULONG dc, USHORT function, size_t count, const LONG *words);
void open32MetafilePoints(ULONG dc, USHORT function, PVOID points, LONG count);
void open32MetafileObject(ULONG dc, ULONG object);
void open32MetafileExtText(ULONG dc, LONG x, LONG y, ULONG flags, PVOID rectangle, PCHAR text,
                           ULONG count, PLONG advances);
void open32MetafileClip(ULONG dc, ULONG region);
void open32MetafilePalette(ULONG dc, ULONG palette);
void open32MetafileRealize(ULONG dc);
void open32MetafilePaletteRecord(ULONG dc, USHORT function, ULONG first, ULONG count,
                                 PVOID entries);
void open32MetafileDCCleanup(ULONG dc);
#endif
