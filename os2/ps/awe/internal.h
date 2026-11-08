/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PMWINX_INTERNAL_H
#define TWOINE_PMWINX_INTERNAL_H
#include <stddef.h>
#include "os2/include/os2win.h"
void open32SetError(ULONG error);
void open32CommonDialogError(ULONG error);
ULONG open32ScrollWindowProc(ULONG window, ULONG message, ULONG first, ULONG second);
BOOL open32NativeDialogWindow(ULONG handle);
BOOL open32NativeControlOwned(ULONG handle);
void open32SetNativeClassName(ULONG handle, const char *name);
const char *open32NativeClassName(ULONG handle);
BOOL open32CreateNativeControl(const char *name, const char *title, ULONG style, LONG x, LONG y,
                               LONG width, LONG height, ULONG parent, ULONG id, ULONG *handle);
BOOL open32FileDialogWindow(ULONG handle);
struct Open32Window *open32PropertyWindow(ULONG handle);
LONG open32NativeControlMessage(ULONG handle, ULONG message, ULONG first, ULONG second,
                                BOOL original);
LONG open32NativeWindowLong(ULONG handle, LONG index, LONG value, BOOL set);
void open32NativeControlsPrune(void);
ULONG open32ControlNotification(ULONG handle, ULONG packed);
/* A numeric, #n or RT_RESNAMES-named resource; zero when the name is not found. */
ULONG open32ResourceIdentity(ULONG module, PSZ name, ULONG type);
/* Converts a guest DLGTEMPLATE or DLGTEMPLATEEX to a caller-freed PM template. */
PVOID open32ConvertDialog(ULONG address);
/* Maps Win32 control styles to PM; a BS_GROUPBOX button changes *kind to the static class. */
ULONG open32ControlStyle(ULONG style, unsigned *kind);
int open32FileIO(ULONG handle, PVOID data, ULONG size, PULONG actual, BOOL writing);
BOOL open32CloseFile(ULONG handle);
void open32FilesCleanup(void);
BOOL open32ValidProcedure(ULONG address);
ULONG open32BuiltinWindowProc(ULONG window, ULONG message, ULONG first, ULONG second);
BOOL open32EraseBackground(ULONG window, ULONG dc);
void *open32DosService(const char *name);
BOOL open32CloseThread(ULONG handle);
BOOL open32CloseSemaphore(ULONG handle);
int open32WaitSemaphore(ULONG handle, ULONG timeout, ULONG *result);
void open32CriticalCleanup(void);
void open32TlsCleanup(void);
void open32IconsCleanup(void);
ULONG open32Presentation(void);
void open32ContextCleanup(void);
void open32DdeCleanup(void);
void open32DdeSetError(ULONG id, ULONG error);
/* Runs DDE work queued for the calling thread. A thread packet carrying
   OPEN32_DDE_PACKET asks its target to do so. */
#define OPEN32_DDE_PACKET 0x7FFDu
void open32DdeDeliver(void);
BOOL open32PostThreadPacket(ULONG thread, ULONG message);
LONG open32FormatGuest(PSZ format, ULONG arguments, PSZ output);
void open32MenusCleanup(void);
void open32AcceleratorsCleanup(void);
void open32MetafilesCleanup(void);
void open32ClipboardWindowDestroyed(ULONG window);
void open32ClipboardCleanup(void);
LONG open32RunKeyboardHooks(LONG code, ULONG key, ULONG parameter);
void open32HooksCleanup(void);
void open32CaretCleanup(void);
BOOL open32PostTimer(ULONG window, ULONG id, ULONG callback);
void open32TimersCleanup(void);
void open32TimersWindowDestroyed(ULONG window);
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
