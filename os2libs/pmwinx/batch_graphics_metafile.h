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
void open32MetafileText(ULONG dc, LONG x, LONG y, PCHAR text, ULONG count);
void open32MetafileDCCleanup(ULONG dc);
#endif
