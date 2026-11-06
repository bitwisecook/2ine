/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_BATCH_PATHS_H
#define TWOINE_OPEN32_BATCH_PATHS_H
struct Open32DC;
int open32PathCapture(struct Open32DC *dc, PVOID points, LONG count, BOOL close);
int open32PathLine(struct Open32DC *dc, LONG x, LONG y, BOOL move);
void open32PathCleanup(struct Open32DC *dc);
BOOL open32PathClose(struct Open32DC *dc);
BOOL open32PathFill(ULONG handle, BOOL stroke);
BOOL open32Chord(ULONG handle, LONG left, LONG top, LONG right, LONG bottom, LONG start_x,
                 LONG start_y, LONG end_x, LONG end_y);
#endif
