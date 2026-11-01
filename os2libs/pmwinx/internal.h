/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PMWINX_INTERNAL_H
#define TWOINE_PMWINX_INTERNAL_H
#include "os2libs/pmwinx.h"
void open32SetError(ULONG error);
BOOL open32EraseBackground(ULONG window, ULONG dc);
void *open32DosService(const char *name);
BOOL open32CloseThread(ULONG handle);
void open32CriticalCleanup(void);
void open32IconsCleanup(void);
ULONG open32Presentation(void);
void open32ContextCleanup(void);
LONG open32FormatGuest(PSZ format, ULONG arguments, PSZ output);
void open32WindowsCleanup(void);
void open32DrawingCleanup(void);
void open32ClassesCleanup(void);
#endif
