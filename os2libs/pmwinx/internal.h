/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PMWINX_INTERNAL_H
#define TWOINE_PMWINX_INTERNAL_H
#include <stddef.h>
#include "os2libs/pmwinx.h"
void open32SetError(ULONG error);
BOOL open32ValidProcedure(ULONG address);
ULONG open32BuiltinWindowProc(ULONG window, ULONG message, ULONG first, ULONG second);
BOOL open32EraseBackground(ULONG window, ULONG dc);
void *open32DosService(const char *name);
BOOL open32CloseThread(ULONG handle);
void open32CriticalCleanup(void);
void open32IconsCleanup(void);
ULONG open32Presentation(void);
void open32ContextCleanup(void);
LONG open32FormatGuest(PSZ format, ULONG arguments, PSZ output);
void open32MenusCleanup(void);
void open32ClipboardWindowDestroyed(ULONG window);
void open32ClipboardCleanup(void);
LONG open32RunKeyboardHooks(LONG code, ULONG key, ULONG parameter);
void open32HooksCleanup(void);
void open32CaretCleanup(void);
void open32CaretPaint(ULONG window, BOOL begin);
void open32CaretWindowDestroyed(ULONG window);
void open32MemoryCleanup(void);
BOOL open32BitmapBytes(LONG width, LONG height, ULONG planes, ULONG depth, size_t *bytes);
void open32BitmapPalette(ULONG depth, uint32_t colors[256]);
BOOL open32DibBuffers(PVOID information, ULONG start, ULONG count, ULONG usage,
                      size_t *information_bytes, size_t *pixel_bytes);
ULONG open32KeyboardMessage(ULONG window, ULONG flags, ULONG data);
void open32WindowsCleanup(void);
void open32DrawingCleanup(void);
void open32ClassesCleanup(void);
#endif
