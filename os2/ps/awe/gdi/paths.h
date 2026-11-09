/* Public domain. No rights reserved. Wholly AI-authored. */
/* GDI paths: recording figures between BeginPath and EndPath, and filling, stroking, widening
   and clipping with them. */
#ifndef TWOINE_PMWINX_PATHS_H
#define TWOINE_PMWINX_PATHS_H
struct Open32DC;
int open32PathCapture(struct Open32DC *dc, PVOID points, LONG count, BOOL close);
int open32PathLine(struct Open32DC *dc, LONG x, LONG y, BOOL move);
void open32PathCleanup(struct Open32DC *dc);
BOOL open32PathClose(struct Open32DC *dc);
BOOL open32PathFill(ULONG handle, BOOL stroke);
BOOL open32PathWiden(struct Open32DC *dc);
BOOL open32PathClip(ULONG handle, LONG mode);
/* Whether the DC is between BeginPath and EndPath. */
BOOL open32PathRecording(const struct Open32DC *dc);
#endif
