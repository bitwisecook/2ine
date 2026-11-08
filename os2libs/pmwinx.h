/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 exports use OS/2's caller-cleaned 32-bit system linkage. */
#ifndef TWOINE_OS2LIBS_PMWINX_H
#define TWOINE_OS2LIBS_PMWINX_H
#include "os2libs/os2types.h"
typedef LONG(OS2API *PFNWINMAIN)(ULONG instance, ULONG previous, PSZ command, LONG show);
#ifdef __cplusplus
extern "C"
{
#endif
    OS2EXPORT LONG OS2API WinCallWinMain(LONG argc, PSZ *argv, PFNWINMAIN entry, LONG show);
    OS2EXPORT BOOL OS2API DestroyCursor(ULONG cursor);
    OS2EXPORT VOID OS2API WinInitializeOpen32(ULONG reserved);
    OS2EXPORT VOID OS2API WinTerminateOpen32(ULONG reserved);
    OS2EXPORT BOOL OS2API WinXProcessMsg(PVOID message);
    OS2EXPORT ULONG OS2API DdeCreateStringHandle(ULONG id, PSZ text, LONG codepage);
    OS2EXPORT BOOL OS2API DdeFreeStringHandle(ULONG id, ULONG string);
    OS2EXPORT BOOL OS2API DdeKeepStringHandle(ULONG id, ULONG string);
    OS2EXPORT ULONG OS2API DdeQueryString(ULONG id, ULONG string, PSZ output, ULONG capacity,
                                          LONG codepage);
    OS2EXPORT LONG OS2API DdeCmpStringHandles(ULONG first, ULONG second);
    OS2EXPORT ULONG OS2API DdeNameService(ULONG id, ULONG string, ULONG reserved, ULONG command);
    OS2EXPORT ULONG OS2API DdeInitialize(PULONG id, ULONG callback, ULONG flags, ULONG reserved);
    OS2EXPORT ULONG OS2API DdeGetLastError(ULONG id);
    OS2EXPORT BOOL OS2API DdeUninitialize(ULONG id);
    OS2EXPORT ULONG OS2API GetLastError(void);
    OS2EXPORT BOOL OS2API GetSaveFileName(PVOID address);
    OS2EXPORT BOOL OS2API GetOpenFileName(PVOID address);
    OS2EXPORT ULONG OS2API CommDlgExtendedError(void);
    OS2EXPORT ULONG OS2API IsDlgButtonChecked(ULONG dialog, LONG identity);
    OS2EXPORT BOOL OS2API CheckDlgButton(ULONG dialog, LONG identity, ULONG check);
    OS2EXPORT VOID OS2API SetLastError(ULONG error);
    OS2EXPORT ULONG OS2API SetTimer(ULONG window, ULONG id, ULONG milliseconds, ULONG callback);
    OS2EXPORT BOOL OS2API KillTimer(ULONG window, ULONG id);
    OS2EXPORT VOID OS2API Sleep(ULONG milliseconds);
    OS2EXPORT ULONG OS2API GetModuleFileName(HMODULE module, PSZ output, ULONG capacity);
    OS2EXPORT HMODULE OS2API LoadLibrary(PSZ name);
    OS2EXPORT HMODULE OS2API GetModuleHandle(PSZ name);
    OS2EXPORT BOOL OS2API FreeLibrary(HMODULE module);
    OS2EXPORT PFN OS2API GetProcAddress(HMODULE module, PSZ name);
    OS2EXPORT ULONG OS2API FindResource(HMODULE module, PSZ name, PSZ type);
    OS2EXPORT ULONG OS2API LoadResource(HMODULE module, ULONG resource);
    OS2EXPORT ULONG OS2API LockResource(ULONG resource);
    OS2EXPORT ULONG OS2API SizeofResource(HMODULE module, ULONG resource);
    OS2EXPORT ULONG OS2API GetUserDefaultLCID(void);
    OS2EXPORT ULONG OS2API GetProfileInt(PSZ section, PSZ key, LONG fallback);
    OS2EXPORT ULONG OS2API GetProfileString(PSZ section, PSZ key, PSZ fallback, PSZ output,
                                            ULONG capacity);
    OS2EXPORT ULONG OS2API GetPrivateProfileString(PSZ section, PSZ key, PSZ fallback, PSZ output,
                                                   ULONG capacity, PSZ filename);
    OS2EXPORT BOOL OS2API WritePrivateProfileString(PSZ section, PSZ key, PSZ value, PSZ filename);
    OS2EXPORT ULONG OS2API GlobalAddAtom(PSZ name);
    OS2EXPORT ULONG OS2API GlobalFindAtom(PSZ name);
    OS2EXPORT ULONG OS2API GlobalDeleteAtom(ULONG atom);
    OS2EXPORT ULONG OS2API GlobalGetAtomName(ULONG atom, PSZ output, LONG capacity);
    OS2EXPORT ULONG OS2API RegisterClass(PVOID record);
    OS2EXPORT ULONG OS2API SetClassLong(ULONG window, LONG index, ULONG value);
    OS2EXPORT BOOL OS2API RegisterClassAlias(PSZ name, PSZ alias);
    OS2EXPORT BOOL OS2API UnregisterClass(PSZ name, ULONG instance);
    OS2EXPORT BOOL OS2API GetClassInfo(ULONG instance, PSZ name, PVOID output);
    OS2EXPORT ULONG OS2API CreateWindowEx(ULONG extended_style, PSZ class_name, PSZ title,
                                          ULONG style, LONG x, LONG y, LONG width, LONG height,
                                          ULONG parent, ULONG menu, ULONG instance,
                                          ULONG parameter);
    OS2EXPORT LONG OS2API DefWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second);
    OS2EXPORT BOOL OS2API DestroyWindow(ULONG handle);
    OS2EXPORT LONG OS2API GetWindowLong(ULONG handle, LONG index);
    OS2EXPORT LONG OS2API SetWindowLong(ULONG handle, LONG index, LONG value);
    OS2EXPORT ULONG OS2API GetOEMCP(void);
    OS2EXPORT ULONG OS2API GetACP(void);
    OS2EXPORT LONG OS2API MultiByteToWideChar(ULONG codepage, ULONG flags, PSZ input, LONG bytes,
                                              PVOID utf16, LONG capacity);
    OS2EXPORT LONG OS2API WideCharToMultiByte(ULONG codepage, ULONG flags, PVOID utf16, LONG units,
                                              PSZ output, LONG capacity, PSZ default_text,
                                              PULONG used_default);
    OS2EXPORT ULONG OS2API GetCommandLine(void);
    OS2EXPORT ULONG OS2API GetVersion(void);
    OS2EXPORT ULONG OS2API timeGetTime(void);
    OS2EXPORT BOOL OS2API OemToChar(ULONG source, ULONG destination);
    OS2EXPORT BOOL OS2API CharToOem(ULONG source, ULONG destination);
    OS2EXPORT BOOL OS2API OemToCharBuff(ULONG source, ULONG destination, ULONG count);
    OS2EXPORT BOOL OS2API CharToOemBuff(ULONG source, ULONG destination, ULONG count);
    OS2EXPORT ULONG OS2API LoadBitmap(HMODULE module, PSZ name);
    OS2EXPORT LONG OS2API GetObject(ULONG handle, LONG capacity, PVOID output);
    OS2EXPORT BOOL OS2API DeleteObject(ULONG handle);
    OS2EXPORT ULONG OS2API GetObjectType(ULONG handle);
    OS2EXPORT ULONG OS2API GetNearestColor(ULONG dc, ULONG color);
    OS2EXPORT ULONG OS2API SetMetaFileBitsEx(ULONG size, PVOID data);
    OS2EXPORT ULONG OS2API GetMetaFileBitsEx(ULONG handle, ULONG size, PVOID data);
    OS2EXPORT BOOL OS2API DeleteMetaFile(ULONG handle);
    OS2EXPORT LONG OS2API GetSystemMetrics(LONG index);
    OS2EXPORT BOOL OS2API SetWindowPos(ULONG handle, ULONG after, LONG x, LONG y, LONG width,
                                       LONG height, ULONG flags);
    OS2EXPORT BOOL OS2API MoveWindow(ULONG handle, LONG x, LONG y, LONG width, LONG height,
                                     BOOL repaint);
    OS2EXPORT BOOL OS2API ShowWindow(ULONG handle, LONG command);
    OS2EXPORT BOOL OS2API UpdateWindow(ULONG handle);
    OS2EXPORT ULONG OS2API GetDC(ULONG window);
    OS2EXPORT LONG OS2API GetDialogBaseUnits(void);
    OS2EXPORT LONG OS2API SaveDC(ULONG dc);
    OS2EXPORT BOOL OS2API RestoreDC(ULONG dc, LONG level);
    OS2EXPORT LONG OS2API ReleaseDC(ULONG window, ULONG dc);
    OS2EXPORT ULONG OS2API CreateCompatibleDC(ULONG source);
    OS2EXPORT BOOL OS2API DeleteDC(ULONG dc);
    OS2EXPORT ULONG OS2API SelectObject(ULONG dc, ULONG object);
    OS2EXPORT BOOL OS2API BitBlt(ULONG destination, LONG x, LONG y, LONG width, LONG height,
                                 ULONG source, LONG source_x, LONG source_y, ULONG operation);
    OS2EXPORT ULONG OS2API BeginPaint(ULONG window, PVOID record);
    OS2EXPORT BOOL OS2API EndPaint(ULONG window, PVOID record);
    OS2EXPORT LONG OS2API SetBkMode(ULONG dc, LONG mode);
    OS2EXPORT ULONG OS2API SetTextColor(ULONG dc, ULONG color);
    OS2EXPORT ULONG OS2API SetBkColor(ULONG dc, ULONG color);
    OS2EXPORT ULONG OS2API CreateFontIndirect(PVOID record);
    OS2EXPORT ULONG OS2API GetStockObject(LONG index);
    OS2EXPORT LONG OS2API DrawText(ULONG dc, PSZ text, LONG count, PVOID rectangle, ULONG flags);
    OS2EXPORT BOOL OS2API TextOut(ULONG dc, LONG x, LONG y, PSZ text, LONG count);
    OS2EXPORT ULONG OS2API CreateCompatibleBitmap(ULONG dc, LONG width, LONG height);
    OS2EXPORT ULONG OS2API GetPixel(ULONG dc, LONG x, LONG y);
    OS2EXPORT LONG OS2API OpenFile(PSZ name, PVOID record, ULONG flags);
    OS2EXPORT LONG OS2API _lclose(ULONG handle);
    OS2EXPORT LONG OS2API _llseek(ULONG handle, LONG distance, LONG origin);
    OS2EXPORT ULONG OS2API _lread(ULONG handle, PVOID output, ULONG size);
    OS2EXPORT ULONG OS2API _lwrite(ULONG handle, PVOID input, ULONG size);
    OS2EXPORT BOOL OS2API ReadFile(ULONG handle, PVOID output, ULONG size, PULONG actual,
                                   PVOID overlapped);
    OS2EXPORT BOOL OS2API WriteFile(ULONG handle, PVOID input, ULONG size, PULONG actual,
                                    PVOID overlapped);
    OS2EXPORT BOOL OS2API CloseHandle(ULONG handle);
    OS2EXPORT ULONG OS2API CreateFile(PSZ name, ULONG access, ULONG share, PVOID security,
                                      ULONG disposition, ULONG attributes, ULONG template);
    OS2EXPORT ULONG OS2API SetFilePointer(ULONG handle, LONG distance, PLONG high, ULONG origin);
    OS2EXPORT ULONG OS2API GetFileSize(ULONG handle, PULONG high);
    OS2EXPORT BOOL OS2API SetEndOfFile(ULONG handle);
    OS2EXPORT BOOL OS2API FileTimeToLocalFileTime(PVOID input, PVOID output);
    OS2EXPORT BOOL OS2API LocalFileTimeToFileTime(PVOID input, PVOID output);
    OS2EXPORT BOOL OS2API FileTimeToSystemTime(PVOID input, PVOID output);
    OS2EXPORT BOOL OS2API FlushFileBuffers(ULONG handle);
    OS2EXPORT ULONG OS2API GetFileType(ULONG handle);
    OS2EXPORT BOOL OS2API LockFile(ULONG handle, ULONG offset_low, ULONG offset_high,
                                   ULONG length_low, ULONG length_high);
    OS2EXPORT BOOL OS2API UnlockFile(ULONG handle, ULONG offset_low, ULONG offset_high,
                                     ULONG length_low, ULONG length_high);
    OS2EXPORT LONG OS2API wsprintf(PSZ output, PSZ format, ...);
    OS2EXPORT LONG OS2API wvsprintf(PSZ output, PSZ format, PVOID arguments);
    OS2EXPORT ULONG OS2API LoadCursor(HMODULE module, PSZ name);
    OS2EXPORT ULONG OS2API SetCursor(ULONG cursor);
    OS2EXPORT ULONG OS2API GetActiveWindow(void);
    OS2EXPORT ULONG OS2API SetActiveWindow(ULONG window);
    OS2EXPORT ULONG OS2API GetFocus(void);
    OS2EXPORT ULONG OS2API SetFocus(ULONG window);
    OS2EXPORT LONG OS2API GetDeviceCaps(ULONG dc, LONG index);
    OS2EXPORT BOOL OS2API GetTextMetrics(ULONG dc, PVOID output);
    OS2EXPORT LONG OS2API EnumFontFamilies(ULONG dc, PSZ family, ULONG procedure, ULONG parameter);
    OS2EXPORT ULONG OS2API LoadIcon(HMODULE module, PSZ name);
    OS2EXPORT BOOL OS2API DestroyIcon(ULONG handle);
    OS2EXPORT VOID OS2API InitializeCriticalSection(PVOID section);
    OS2EXPORT VOID OS2API EnterCriticalSection(PVOID section);
    OS2EXPORT VOID OS2API LeaveCriticalSection(PVOID section);
    OS2EXPORT VOID OS2API DeleteCriticalSection(PVOID section);
    OS2EXPORT ULONG OS2API GetCurrentThreadId(void);
    OS2EXPORT ULONG OS2API GetCurrentThread(void);
    OS2EXPORT ULONG OS2API CreateThread(PVOID security, ULONG stack, ULONG procedure,
                                        ULONG parameter, ULONG flags, PULONG id);
    OS2EXPORT BOOL OS2API GetExitCodeThread(ULONG handle, PULONG result);
    OS2EXPORT ULONG OS2API ResumeThread(ULONG handle);
    OS2EXPORT ULONG OS2API WaitForSingleObject(ULONG handle, ULONG timeout);
    OS2EXPORT ULONG OS2API CreateSemaphore(PVOID security, LONG initial, LONG maximum, PSZ name);
    OS2EXPORT ULONG OS2API OpenSemaphore(ULONG access, BOOL inherit, PSZ name);
    OS2EXPORT BOOL OS2API ReleaseSemaphore(ULONG handle, LONG count, PLONG previous);
    OS2EXPORT VOID OS2API ExitThread(ULONG result);
    OS2EXPORT BOOL OS2API SetThreadPriority(ULONG thread, LONG priority);
    OS2EXPORT ULONG OS2API GetSysColor(LONG index);
    OS2EXPORT ULONG OS2API CreateSolidBrush(ULONG color);
    OS2EXPORT LONG OS2API FillRect(ULONG dc, PVOID rectangle, ULONG brush);
    OS2EXPORT VOID OS2API GetSystemTime(PVOID output);
    OS2EXPORT BOOL OS2API SystemTimeToFileTime(PVOID input, PVOID output);
    OS2EXPORT LONG OS2API GetLocaleInfo(ULONG locale, ULONG type, PSZ output, LONG capacity);
    OS2EXPORT BOOL OS2API IsDBCSLeadByte(ULONG character);
    OS2EXPORT ULONG OS2API GetDriveType(PSZ root);
    OS2EXPORT ULONG OS2API GetLogicalDrives(void);
    OS2EXPORT BOOL OS2API GetVolumeInformation(PSZ root, PSZ label, ULONG label_capacity,
                                               PULONG serial, PULONG component_length, PULONG flags,
                                               PSZ filesystem, ULONG filesystem_capacity);
    OS2EXPORT ULONG OS2API GetLogicalDriveStrings(ULONG capacity, PSZ output);
    OS2EXPORT ULONG OS2API GetFullPathName(PSZ name, ULONG capacity, PSZ output, PSZ *filepart);
    OS2EXPORT ULONG OS2API GetWindowsDirectory(PSZ output, ULONG capacity);
    OS2EXPORT ULONG OS2API GetCurrentDirectory(ULONG capacity, PSZ output);
    OS2EXPORT BOOL OS2API SetCurrentDirectory(PSZ name);
    OS2EXPORT USHORT OS2API GetUserDefaultLangID(void);
    OS2EXPORT VOID OS2API GetLocalTime(PVOID output);
    OS2EXPORT ULONG OS2API CreatePalette(PVOID input);
    OS2EXPORT ULONG OS2API SelectPalette(ULONG dc, ULONG palette, BOOL background);
    OS2EXPORT ULONG OS2API RealizePalette(ULONG dc);
    OS2EXPORT ULONG OS2API GetPaletteEntries(ULONG palette, ULONG start, ULONG count, PVOID output);
    OS2EXPORT ULONG OS2API GetNearestPaletteIndex(ULONG palette, ULONG color);
    OS2EXPORT ULONG OS2API RegisterClipboardFormat(PSZ name);
    OS2EXPORT ULONG OS2API CreateMenu(void);
    OS2EXPORT ULONG OS2API CreatePopupMenu(void);
    OS2EXPORT BOOL OS2API DestroyMenu(ULONG menu);
    OS2EXPORT BOOL OS2API InsertMenu(ULONG menu, ULONG position, ULONG flags, ULONG id, PSZ text);
    OS2EXPORT BOOL OS2API AppendMenu(ULONG menu, ULONG flags, ULONG id, PSZ text);
    OS2EXPORT LONG OS2API GetMenuItemCount(ULONG menu);
    OS2EXPORT ULONG OS2API GetMenuItemID(ULONG menu, LONG position);
    OS2EXPORT ULONG OS2API GetSubMenu(ULONG menu, LONG position);
    OS2EXPORT ULONG OS2API GetMenuState(ULONG menu, ULONG id, ULONG flags);
    OS2EXPORT ULONG OS2API CheckMenuItem(ULONG menu, ULONG id, ULONG flags);
    OS2EXPORT ULONG OS2API EnableMenuItem(ULONG menu, ULONG id, ULONG flags);
    OS2EXPORT BOOL OS2API IsMenu(ULONG menu);
    OS2EXPORT BOOL OS2API RemoveMenu(ULONG menu, ULONG id, ULONG flags);
    OS2EXPORT BOOL OS2API DeleteMenu(ULONG menu, ULONG id, ULONG flags);
    OS2EXPORT BOOL OS2API MessageBeep(ULONG type);
    OS2EXPORT ULONG OS2API GetCapture(void);
    OS2EXPORT ULONG OS2API SetCapture(ULONG window);
    OS2EXPORT BOOL OS2API ReleaseCapture(void);
    OS2EXPORT BOOL OS2API GetClientRect(ULONG window, PVOID output);
    OS2EXPORT BOOL OS2API GetWindowRect(ULONG window, PVOID output);
    OS2EXPORT BOOL OS2API ClientToScreen(ULONG window, PVOID output);
    OS2EXPORT BOOL OS2API ScreenToClient(ULONG window, PVOID output);
    OS2EXPORT ULONG OS2API GetDesktopWindow(void);
    OS2EXPORT LONG OS2API SendMessage(ULONG window, ULONG message, ULONG first, ULONG second);
    OS2EXPORT LONG OS2API CallWindowProc(ULONG procedure, ULONG window, ULONG message, ULONG first,
                                         ULONG second);
    OS2EXPORT BOOL OS2API SetRect(PVOID output, LONG left, LONG top, LONG right, LONG bottom);
    OS2EXPORT BOOL OS2API SetProp(ULONG window, PSZ name, ULONG data);
    OS2EXPORT ULONG OS2API GetProp(ULONG window, PSZ name);
    OS2EXPORT ULONG OS2API RemoveProp(ULONG window, PSZ name);
    OS2EXPORT ULONG OS2API CreateIC(PSZ driver, PSZ device, PSZ output, PVOID mode);
    OS2EXPORT ULONG OS2API CreateDC(PSZ driver, PSZ device, PSZ output, PVOID mode);
    OS2EXPORT BOOL OS2API PrintDlg(PVOID data);
    OS2EXPORT LONG OS2API StartDoc(ULONG dc, PVOID info);
    OS2EXPORT LONG OS2API StartPage(ULONG dc);
    OS2EXPORT LONG OS2API EndPage(ULONG dc);
    OS2EXPORT LONG OS2API EndDoc(ULONG dc);
    OS2EXPORT LONG OS2API AbortDoc(ULONG dc);
    OS2EXPORT ULONG OS2API CreateRectRgn(LONG left, LONG top, LONG right, LONG bottom);
    OS2EXPORT BOOL OS2API SetRectRgn(ULONG region, LONG left, LONG top, LONG right, LONG bottom);
    OS2EXPORT LONG OS2API CombineRgn(ULONG destination, ULONG first, ULONG second, LONG mode);
    OS2EXPORT LONG OS2API SelectClipRgn(ULONG dc, ULONG region);
    OS2EXPORT LONG OS2API GetRgnBox(ULONG region, PVOID output);
    OS2EXPORT ULONG OS2API CreateRectRgnIndirect(PVOID input);
    OS2EXPORT ULONG OS2API CountClipboardFormats(void);
    OS2EXPORT BOOL OS2API OpenClipboard(ULONG window);
    OS2EXPORT BOOL OS2API CloseClipboard(void);
    OS2EXPORT BOOL OS2API EmptyClipboard(void);
    OS2EXPORT ULONG OS2API GetClipboardData(ULONG format);
    OS2EXPORT ULONG OS2API SetClipboardData(ULONG format, ULONG handle);
    OS2EXPORT ULONG OS2API EnumClipboardFormats(ULONG format);
    OS2EXPORT ULONG OS2API GetClipboardFormatName(ULONG format, PSZ output, LONG capacity);
    OS2EXPORT ULONG OS2API GetClipboardOwner(void);
    OS2EXPORT BOOL OS2API IsClipboardFormatAvailable(ULONG format);
    OS2EXPORT ULONG OS2API SetClipboardViewer(ULONG window);
    OS2EXPORT ULONG OS2API GetClipboardViewer(void);
    OS2EXPORT BOOL OS2API ChangeClipboardChain(ULONG window, ULONG next);
    OS2EXPORT ULONG OS2API RegisterWindowMessage(PSZ name);
    OS2EXPORT LONG OS2API DefFrameProc(ULONG frame, ULONG client, ULONG message, ULONG first,
                                       ULONG second);
    OS2EXPORT LONG OS2API DefMDIChildProc(ULONG child, ULONG message, ULONG first, ULONG second);
    OS2EXPORT ULONG OS2API SetWindowsHookEx(LONG type, ULONG procedure, ULONG module, ULONG thread);
    OS2EXPORT BOOL OS2API UnhookWindowsHookEx(ULONG hook);
    OS2EXPORT LONG OS2API CallNextHookEx(ULONG hook, LONG code, ULONG key, ULONG parameter);
    OS2EXPORT SHORT OS2API GetKeyState(LONG key);
    OS2EXPORT BOOL OS2API GetCursorPos(PVOID output);
    OS2EXPORT BOOL OS2API SetCursorPos(LONG x, LONG y);
    OS2EXPORT LONG OS2API GetMenuCheckMarkDimensions(void);
    OS2EXPORT ULONG OS2API WindowFromPoint(LONG x, LONG y);
    OS2EXPORT BOOL OS2API IsZoomed(ULONG window);
    OS2EXPORT BOOL OS2API IsIconic(ULONG window);
    OS2EXPORT BOOL OS2API GetWindowPlacement(ULONG window, PVOID output);
    OS2EXPORT BOOL OS2API SetWindowPlacement(ULONG window, PVOID input);
    OS2EXPORT ULONG OS2API GetParent(ULONG window);
    OS2EXPORT ULONG OS2API SetParent(ULONG window, ULONG parent);
    OS2EXPORT BOOL OS2API RedrawWindow(ULONG window, PVOID rectangle, ULONG region, ULONG flags);
    OS2EXPORT LONG OS2API GetMenuString(ULONG menu, ULONG item, PCHAR output, LONG capacity,
                                        ULONG flags);
    OS2EXPORT BOOL OS2API WinHelp(ULONG window, PSZ filename, ULONG command, ULONG data);
    OS2EXPORT ULONG OS2API GetMenu(ULONG window);
    OS2EXPORT BOOL OS2API SetMenu(ULONG window, ULONG menu);
    OS2EXPORT BOOL OS2API DrawMenuBar(ULONG window);
    OS2EXPORT ULONG OS2API FindFirstFile(PSZ name, PVOID output);
    OS2EXPORT BOOL OS2API FindNextFile(ULONG handle, PVOID output);
    OS2EXPORT BOOL OS2API FindClose(ULONG handle);
    OS2EXPORT ULONG OS2API CreateFont(LONG height, LONG width, LONG escapement, LONG orientation,
                                      LONG weight, ULONG italic, ULONG underline, ULONG strikeout,
                                      ULONG charset, ULONG out_precision, ULONG clip_precision,
                                      ULONG quality, ULONG pitch, PSZ face);
    OS2EXPORT BOOL OS2API SetWindowText(ULONG window, PSZ text);
    OS2EXPORT LONG OS2API GetWindowText(ULONG window, PCHAR text, LONG capacity);
    OS2EXPORT LONG OS2API GetWindowTextLength(ULONG window);
    OS2EXPORT ULONG OS2API LocalAlloc(ULONG flags, ULONG size);
    OS2EXPORT ULONG OS2API LocalFree(ULONG handle);
    OS2EXPORT ULONG OS2API LocalLock(ULONG handle);
    OS2EXPORT BOOL OS2API LocalUnlock(ULONG handle);
    OS2EXPORT ULONG OS2API LocalFlags(ULONG handle);
    OS2EXPORT ULONG OS2API LocalSize(ULONG handle);
    OS2EXPORT ULONG OS2API LocalHandle(ULONG pointer);
    OS2EXPORT ULONG OS2API LocalReAlloc(ULONG handle, ULONG size, ULONG flags);
    OS2EXPORT ULONG OS2API GlobalAlloc(ULONG flags, ULONG size);
    OS2EXPORT ULONG OS2API GlobalFree(ULONG handle);
    OS2EXPORT ULONG OS2API GlobalLock(ULONG handle);
    OS2EXPORT BOOL OS2API GlobalUnlock(ULONG handle);
    OS2EXPORT ULONG OS2API GlobalFlags(ULONG handle);
    OS2EXPORT ULONG OS2API GlobalSize(ULONG handle);
    OS2EXPORT ULONG OS2API GlobalHandle(ULONG pointer);
    OS2EXPORT ULONG OS2API GlobalReAlloc(ULONG handle, ULONG size, ULONG flags);
    OS2EXPORT LONG OS2API GetDIBits(ULONG dc, ULONG bitmap, ULONG start, ULONG count, PVOID bits,
                                    PVOID information, ULONG usage);
    OS2EXPORT LONG OS2API LoadString(HMODULE module, ULONG identity, PCHAR output, LONG capacity);
    OS2EXPORT ULONG OS2API lstrcpy(ULONG destination, ULONG source);
    OS2EXPORT ULONG OS2API lstrcat(ULONG destination, ULONG source);
    OS2EXPORT LONG OS2API lstrlen(ULONG source);
    OS2EXPORT LONG OS2API SetMapMode(ULONG dc, LONG mode);
    OS2EXPORT BOOL OS2API SetViewportOrgEx(ULONG dc, LONG x, LONG y, PVOID previous);
    OS2EXPORT BOOL OS2API SetWindowOrgEx(ULONG dc, LONG x, LONG y, PVOID previous);
    OS2EXPORT BOOL OS2API SetViewportExtEx(ULONG dc, LONG x, LONG y, PVOID previous);
    OS2EXPORT BOOL OS2API SetWindowExtEx(ULONG dc, LONG x, LONG y, PVOID previous);
    OS2EXPORT BOOL OS2API PlayMetaFile(ULONG dc, ULONG handle);
    OS2EXPORT BOOL OS2API EnumMetaFile(ULONG dc, ULONG handle, ULONG procedure, ULONG parameter);
    OS2EXPORT BOOL OS2API PlayMetaFileRecord(ULONG dc, PVOID table, PVOID record, ULONG count);
    OS2EXPORT LONG OS2API StretchDIBits(ULONG dc, LONG x, LONG y, LONG width, LONG height,
                                        LONG source_x, LONG source_y, LONG source_width,
                                        LONG source_height, PVOID bits, PVOID information,
                                        ULONG usage, ULONG operation);
    OS2EXPORT LONG OS2API GetMapMode(ULONG dc);
    OS2EXPORT LONG OS2API IntersectClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom);
    OS2EXPORT LONG OS2API ExcludeClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom);
    OS2EXPORT BOOL OS2API GetTextExtentPoint32(ULONG dc, PCHAR text, LONG count, PVOID size);
    OS2EXPORT BOOL OS2API GetTextExtentExPoint(ULONG dc, PCHAR text, LONG count, LONG maximum,
                                               PVOID fit, PVOID advances, PVOID size);
    OS2EXPORT ULONG OS2API GetTabbedTextExtent(ULONG dc, PCHAR text, LONG count, LONG tab_count,
                                               PVOID tabs);
    OS2EXPORT BOOL OS2API EnableWindow(ULONG window, BOOL enable);
    OS2EXPORT BOOL OS2API IsWindowEnabled(ULONG window);
    OS2EXPORT BOOL OS2API IsWindowVisible(ULONG window);
    OS2EXPORT BOOL OS2API IsWindow(ULONG window);
    OS2EXPORT BOOL OS2API InvalidateRect(ULONG window, PVOID rectangle, BOOL erase);
    OS2EXPORT BOOL OS2API ValidateRect(ULONG window, PVOID rectangle);
    OS2EXPORT BOOL OS2API InvalidateRgn(ULONG window, ULONG region, BOOL erase);
    OS2EXPORT BOOL OS2API ValidateRgn(ULONG window, ULONG region);
    OS2EXPORT BOOL OS2API GetUpdateRect(ULONG window, PVOID rectangle, BOOL erase);
    OS2EXPORT BOOL OS2API SetScrollRange(ULONG window, LONG bar, LONG minimum, LONG maximum,
                                         BOOL redraw);
    OS2EXPORT LONG OS2API SetScrollPos(ULONG window, LONG bar, LONG position, BOOL redraw);
    OS2EXPORT LONG OS2API GetScrollPos(ULONG window, LONG bar);
    OS2EXPORT BOOL OS2API ShowScrollBar(ULONG window, LONG bar, BOOL show);
    OS2EXPORT BOOL OS2API GetScrollRange(ULONG window, LONG bar, PVOID minimum, PVOID maximum);
    OS2EXPORT ULONG OS2API GetClassLong(ULONG window, LONG index);
    OS2EXPORT LONG OS2API GetClassName(ULONG window, PCHAR name, LONG capacity);
    OS2EXPORT ULONG OS2API GetWindow(ULONG window, ULONG command);
    OS2EXPORT BOOL OS2API CreateCaret(ULONG window, ULONG bitmap, LONG width, LONG height);
    OS2EXPORT BOOL OS2API DestroyCaret(void);
    OS2EXPORT BOOL OS2API HideCaret(ULONG window);
    OS2EXPORT BOOL OS2API ShowCaret(ULONG window);
    OS2EXPORT BOOL OS2API SetCaretPos(LONG x, LONG y);
    OS2EXPORT ULONG OS2API GetCaretBlinkTime(void);
    OS2EXPORT BOOL OS2API SetCaretBlinkTime(ULONG interval);
    OS2EXPORT BOOL OS2API GetCaretPos(PVOID point);
    OS2EXPORT BOOL OS2API BringWindowToTop(ULONG window);
    OS2EXPORT ULONG OS2API CreatePen(LONG style, LONG width, ULONG color);
    OS2EXPORT BOOL OS2API LineTo(ULONG dc, LONG x, LONG y);
    OS2EXPORT LONG OS2API SetROP2(ULONG dc, LONG mode);
    OS2EXPORT ULONG OS2API CreatePenIndirect(PVOID record);
    OS2EXPORT BOOL OS2API MoveToEx(ULONG dc, LONG x, LONG y, PVOID previous);
    OS2EXPORT LONG OS2API GetROP2(ULONG dc);
    OS2EXPORT BOOL OS2API Rectangle(ULONG dc, LONG left, LONG top, LONG right, LONG bottom);
    OS2EXPORT BOOL OS2API PeekMessage(PVOID message, ULONG window, ULONG first, ULONG last,
                                      ULONG flags);
    OS2EXPORT LONG OS2API GetMessage(PVOID message, ULONG window, ULONG first, ULONG last);
    OS2EXPORT BOOL OS2API TranslateMessage(PVOID message);
    OS2EXPORT LONG OS2API DispatchMessage(PVOID message);
    OS2EXPORT BOOL OS2API PostMessage(ULONG window, ULONG message, ULONG first, ULONG second);
    OS2EXPORT VOID OS2API PostQuitMessage(LONG status);
    OS2EXPORT LONG OS2API GetMessageTime(void);
    OS2EXPORT ULONG OS2API GetMessagePos(void);
    OS2EXPORT ULONG OS2API GetSystemMenu(ULONG window, BOOL revert);
    OS2EXPORT ULONG OS2API GetWindowThreadProcessId(ULONG window, PVOID process);
    OS2EXPORT ULONG OS2API GetCurrentProcessId(void);
    OS2EXPORT SHORT OS2API GetAsyncKeyState(LONG key);
    OS2EXPORT BOOL OS2API SetRectEmpty(PVOID output);
    OS2EXPORT BOOL OS2API CopyRect(PVOID output, PVOID input);
    OS2EXPORT BOOL OS2API IsRectEmpty(PVOID input);
    OS2EXPORT BOOL OS2API EqualRect(PVOID first, PVOID second);
    OS2EXPORT BOOL OS2API UnionRect(PVOID output, PVOID first, PVOID second);
    OS2EXPORT BOOL OS2API IntersectRect(PVOID output, PVOID first, PVOID second);
    OS2EXPORT BOOL OS2API SubtractRect(PVOID output, PVOID first, PVOID second);
    OS2EXPORT BOOL OS2API OffsetRect(PVOID rectangle, LONG x, LONG y);
    OS2EXPORT BOOL OS2API InflateRect(PVOID rectangle, LONG x, LONG y);
    OS2EXPORT BOOL OS2API PtInRect(PVOID rectangle, LONG x, LONG y);
    OS2EXPORT LONG OS2API GetClipBox(ULONG dc, PVOID rectangle);
    OS2EXPORT LONG OS2API FrameRect(ULONG dc, PVOID rectangle, ULONG brush);
    OS2EXPORT BOOL OS2API InvertRect(ULONG dc, PVOID rectangle);
    OS2EXPORT ULONG OS2API CreateBitmap(LONG width, LONG height, ULONG planes, ULONG depth,
                                        PVOID bits);
    OS2EXPORT LONG OS2API SetDIBits(ULONG dc, ULONG bitmap, ULONG start, ULONG count, PVOID bits,
                                    PVOID information, ULONG usage);
    OS2EXPORT BOOL OS2API GetTextExtentPoint(ULONG dc, PCHAR text, LONG count, PVOID size);
    OS2EXPORT BOOL OS2API ExtTextOut(ULONG dc, LONG x, LONG y, ULONG flags, PVOID rectangle,
                                     PCHAR text, ULONG count, PLONG advances);
    OS2EXPORT BOOL OS2API WaitMessage(void);
    OS2EXPORT ULONG OS2API GetCurrentProcess(void);
    OS2EXPORT BOOL OS2API SetPriorityClass(ULONG process, ULONG priority);
    OS2EXPORT ULONG OS2API GetPriorityClass(ULONG process);
    OS2EXPORT LONG OS2API InterlockedIncrement(PLONG value);
    OS2EXPORT LONG OS2API InterlockedDecrement(PLONG value);
    OS2EXPORT LONG OS2API InterlockedExchange(PLONG value, LONG replacement);
    OS2EXPORT ULONG OS2API LoadMenu(ULONG module, PSZ resource);
    OS2EXPORT BOOL OS2API AdjustWindowRectEx(PVOID rectangle, ULONG style, BOOL menu,
                                             ULONG extended_style);
    OS2EXPORT LONG OS2API GetTextFace(ULONG dc, LONG capacity, PCHAR output);
    OS2EXPORT BOOL OS2API EnumChildWindows(ULONG parent, ULONG procedure, ULONG parameter);
    OS2EXPORT BOOL OS2API WinTranslateDevicePoints(ULONG dc, ULONG window, PVOID points, LONG count,
                                                   LONG direction);
    OS2EXPORT LONG OS2API DialogBox(ULONG module, PSZ resource, ULONG owner, ULONG procedure);
    OS2EXPORT LONG OS2API DialogBoxParam(ULONG module, PSZ resource, ULONG owner, ULONG procedure,
                                         ULONG parameter);
    OS2EXPORT BOOL OS2API EndDialog(ULONG dialog, LONG result);
    OS2EXPORT ULONG OS2API GetDlgItem(ULONG dialog, LONG identity);
    OS2EXPORT BOOL OS2API SetDlgItemText(ULONG dialog, LONG identity, PSZ text);
    OS2EXPORT ULONG OS2API GetDlgItemText(ULONG dialog, LONG identity, PCHAR output, LONG capacity);
    OS2EXPORT LONG OS2API SendDlgItemMessage(ULONG dialog, LONG identity, ULONG message,
                                             ULONG first, ULONG second);
    OS2EXPORT BOOL OS2API PatBlt(ULONG dc, LONG x, LONG y, LONG width, LONG height,
                                 ULONG operation);
    OS2EXPORT ULONG OS2API CreatePatternBrush(ULONG bitmap);
    OS2EXPORT BOOL OS2API DPtoLP(ULONG dc, PVOID points, LONG count);
    OS2EXPORT BOOL OS2API LPtoDP(ULONG dc, PVOID points, LONG count);
    OS2EXPORT BOOL OS2API GetKeyboardState(PVOID output);
    OS2EXPORT BOOL OS2API Polygon(ULONG dc, PVOID points, LONG count);
    OS2EXPORT BOOL OS2API Polyline(ULONG dc, PVOID points, LONG count);
    OS2EXPORT ULONG OS2API SetTextAlign(ULONG dc, ULONG alignment);
    OS2EXPORT ULONG OS2API GetTextAlign(ULONG dc);
    OS2EXPORT BOOL OS2API SetBrushOrgEx(ULONG dc, LONG x, LONG y, PVOID previous);
    OS2EXPORT BOOL OS2API GetBrushOrgEx(ULONG dc, PVOID output);
    OS2EXPORT BOOL OS2API SetForegroundWindow(ULONG window);
    OS2EXPORT BOOL OS2API IsBadReadPtr(PVOID pointer, ULONG bytes);
    OS2EXPORT BOOL OS2API IsBadWritePtr(PVOID pointer, ULONG bytes);
    OS2EXPORT BOOL OS2API IsBadCodePtr(ULONG procedure);
    OS2EXPORT ULONG OS2API CharUpper(ULONG pointer);
    OS2EXPORT ULONG OS2API CharLower(ULONG pointer);
    OS2EXPORT ULONG OS2API CharUpperBuff(ULONG pointer, ULONG count);
    OS2EXPORT ULONG OS2API CharLowerBuff(ULONG pointer, ULONG count);
    OS2EXPORT ULONG OS2API CharNext(ULONG pointer);
    OS2EXPORT ULONG OS2API CharPrev(ULONG start, ULONG current);
    OS2EXPORT LONG OS2API lstrcmp(ULONG first, ULONG second);
    OS2EXPORT LONG OS2API lstrcmpi(ULONG first, ULONG second);
    OS2EXPORT ULONG OS2API GetFileAttributes(PSZ name);
    OS2EXPORT ULONG OS2API GetTickCount(void);
    OS2EXPORT ULONG OS2API GetCurrentTime(void);
    OS2EXPORT LONG OS2API MulDiv(LONG number, LONG numerator, LONG denominator);
    OS2EXPORT LONG OS2API RegCloseKey(ULONG key);
    OS2EXPORT LONG OS2API RegDeleteKey(ULONG key, PSZ name);
    OS2EXPORT LONG OS2API RegOpenKey(ULONG key, PSZ name, PULONG output);
    OS2EXPORT LONG OS2API RegOpenKeyEx(ULONG key, PSZ name, ULONG options, ULONG access,
                                       PULONG output);
    OS2EXPORT LONG OS2API RegCreateKey(ULONG key, PSZ name, PULONG output);
    OS2EXPORT LONG OS2API RegCreateKeyEx(ULONG key, PSZ name, ULONG reserved, PSZ class_name,
                                         ULONG options, ULONG access, PVOID security, PULONG output,
                                         PULONG disposition);
    OS2EXPORT LONG OS2API RegQueryValueEx(ULONG key, PSZ name, PULONG reserved, PULONG type,
                                          PVOID data, PULONG size);
    OS2EXPORT LONG OS2API RegEnumKey(ULONG key, ULONG index, PSZ name, ULONG capacity);
    OS2EXPORT LONG OS2API RegEnumKeyEx(ULONG key, ULONG index, PSZ name, PULONG name_size,
                                       PULONG reserved, PSZ class_name, PULONG class_size,
                                       PVOID last_write);
    OS2EXPORT LONG OS2API RegEnumValue(ULONG key, ULONG index, PSZ name, PULONG name_size,
                                       PULONG reserved, PULONG type, PVOID data, PULONG data_size);
    OS2EXPORT LONG OS2API RegDeleteValue(ULONG key, PSZ name);
    OS2EXPORT LONG OS2API RegSetValueEx(ULONG key, PSZ name, ULONG reserved, ULONG type, PVOID data,
                                        ULONG size);
    OS2EXPORT ULONG OS2API TlsAlloc(void);
    OS2EXPORT BOOL OS2API TlsFree(ULONG index);
    OS2EXPORT ULONG OS2API TlsGetValue(ULONG index);
    OS2EXPORT BOOL OS2API TlsSetValue(ULONG index, ULONG value);
    OS2EXPORT ULONG OS2API LoadAccelerators(HMODULE module, PSZ name);
    OS2EXPORT ULONG OS2API CreateAcceleratorTable(PVOID data, LONG count);
    OS2EXPORT LONG OS2API CopyAcceleratorTable(ULONG handle, PVOID data, LONG count);
    OS2EXPORT BOOL OS2API DestroyAcceleratorTable(ULONG handle);
    OS2EXPORT LONG OS2API TranslateAccelerator(ULONG window, ULONG table, PVOID message);
    OS2EXPORT BOOL OS2API AnimatePalette(ULONG a0, ULONG a1, ULONG a2, PVOID a3);
    OS2EXPORT BOOL OS2API Arc(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5, LONG a6,
                              LONG a7, LONG a8);
    OS2EXPORT BOOL OS2API BeginPath(ULONG a0);
    OS2EXPORT ULONG OS2API CloseMetaFile(ULONG a0);
    OS2EXPORT ULONG OS2API CopyMetaFile(ULONG a0, PSZ a1);
    OS2EXPORT ULONG OS2API CreateBrushIndirect(PVOID a0);
    OS2EXPORT ULONG OS2API CreateDIBitmap(ULONG a0, PVOID a1, ULONG a2, PVOID a3, PVOID a4,
                                          ULONG a5);
    OS2EXPORT ULONG OS2API CreateEllipticRgn(LONG a0, LONG a1, LONG a2, LONG a3);
    OS2EXPORT ULONG OS2API CreateEllipticRgnIndirect(PVOID a0);
    OS2EXPORT ULONG OS2API CreateHatchBrush(LONG a0, ULONG a1);
    OS2EXPORT ULONG OS2API CreateIcon(ULONG a0, LONG a1, LONG a2, ULONG a3, ULONG a4, PVOID a5,
                                      PVOID a6);
    OS2EXPORT ULONG OS2API CreateMetaFile(PSZ a0);
    OS2EXPORT ULONG OS2API CreatePolyPolygonRgn(PVOID a0, PVOID a1, LONG a2, LONG a3);
    OS2EXPORT ULONG OS2API CreatePolygonRgn(PVOID a0, LONG a1, LONG a2);
    OS2EXPORT ULONG OS2API CreateRoundRectRgn(LONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5);
    OS2EXPORT BOOL OS2API DeleteEnhMetaFile(ULONG a0);
    OS2EXPORT BOOL OS2API DrawFocusRect(ULONG a0, PVOID a1);
    OS2EXPORT BOOL OS2API DrawIcon(ULONG a0, LONG a1, LONG a2, ULONG a3);
    OS2EXPORT BOOL OS2API Ellipse(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4);
    OS2EXPORT BOOL OS2API EndPath(ULONG a0);
    OS2EXPORT BOOL OS2API EnumEnhMetaFile(ULONG a0, ULONG a1, ULONG a2, ULONG a3, PVOID a4);
    OS2EXPORT LONG OS2API EnumFonts(ULONG a0, PSZ a1, ULONG a2, ULONG a3);
    OS2EXPORT BOOL OS2API EqualRgn(ULONG a0, ULONG a1);
    OS2EXPORT LONG OS2API Escape(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4);
    OS2EXPORT ULONG OS2API ExtCreatePen(ULONG a0, ULONG a1, PVOID a2, ULONG a3, PVOID a4);
    OS2EXPORT LONG OS2API ExtSelectClipRgn(ULONG a0, ULONG a1, LONG a2);
    OS2EXPORT BOOL OS2API FillRgn(ULONG a0, ULONG a1, ULONG a2);
    OS2EXPORT BOOL OS2API FrameRgn(ULONG a0, ULONG a1, ULONG a2, LONG a3, LONG a4);
    OS2EXPORT LONG OS2API GetBitmapBits(ULONG a0, LONG a1, PVOID a2);
    OS2EXPORT ULONG OS2API GetBkColor(ULONG a0);
    OS2EXPORT LONG OS2API GetBkMode(ULONG a0);
    OS2EXPORT BOOL OS2API GetCharABCWidths(ULONG a0, ULONG a1, ULONG a2, PVOID a3);
    OS2EXPORT BOOL OS2API GetCharWidth(ULONG a0, ULONG a1, ULONG a2, PVOID a3);
    OS2EXPORT LONG OS2API GetClipRgn(ULONG a0, ULONG a1);
    OS2EXPORT ULONG OS2API GetCurrentObject(ULONG a0, ULONG a1);
    OS2EXPORT BOOL OS2API GetCurrentPositionEx(ULONG a0, PVOID a1);
    OS2EXPORT ULONG OS2API GetEnhMetaFile(PSZ a0);
    OS2EXPORT ULONG OS2API GetEnhMetaFileBits(ULONG a0, ULONG a1, PVOID a2);
    OS2EXPORT ULONG OS2API GetEnhMetaFileHeader(ULONG a0, ULONG a1, PVOID a2);
    OS2EXPORT ULONG OS2API GetKerningPairs(ULONG a0, ULONG a1, PVOID a2);
    OS2EXPORT ULONG OS2API GetMetaFile(PSZ a0);
    OS2EXPORT ULONG OS2API GetOutlineTextMetrics(ULONG a0, ULONG a1, PVOID a2);
    OS2EXPORT LONG OS2API GetPolyFillMode(ULONG a0);
    OS2EXPORT ULONG OS2API GetSystemPaletteEntries(ULONG a0, ULONG a1, ULONG a2, PVOID a3);
    OS2EXPORT ULONG OS2API GetTextColor(ULONG a0);
    OS2EXPORT BOOL OS2API GetViewportExtEx(ULONG a0, PVOID a1);
    OS2EXPORT BOOL OS2API GetViewportOrgEx(ULONG a0, PVOID a1);
    OS2EXPORT ULONG OS2API GetWinMetaFileBits(ULONG a0, ULONG a1, PVOID a2, LONG a3, ULONG a4);
    OS2EXPORT ULONG OS2API GetWindowDC(ULONG a0);
    OS2EXPORT BOOL OS2API GetWindowExtEx(ULONG a0, PVOID a1);
    OS2EXPORT BOOL OS2API GetWindowOrgEx(ULONG a0, PVOID a1);
    OS2EXPORT BOOL OS2API LineDDA(LONG a0, LONG a1, LONG a2, LONG a3, ULONG a4, ULONG a5);
    OS2EXPORT LONG OS2API OffsetRgn(ULONG a0, LONG a1, LONG a2);
    OS2EXPORT BOOL OS2API OffsetViewportOrgEx(ULONG a0, LONG a1, LONG a2, PVOID a3);
    OS2EXPORT BOOL OS2API OffsetWindowOrgEx(ULONG a0, LONG a1, LONG a2, PVOID a3);
    OS2EXPORT BOOL OS2API PaintRgn(ULONG a0, ULONG a1);
    OS2EXPORT BOOL OS2API Pie(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5, LONG a6,
                              LONG a7, LONG a8);
    OS2EXPORT BOOL OS2API PlayEnhMetaFile(ULONG a0, ULONG a1, PVOID a2);
    OS2EXPORT BOOL OS2API PlayEnhMetaFileRecord(ULONG a0, PVOID a1, PVOID a2, ULONG a3);
    OS2EXPORT BOOL OS2API PolyPolygon(ULONG a0, PVOID a1, PVOID a2, LONG a3);
    OS2EXPORT BOOL OS2API PolyPolyline(ULONG a0, PVOID a1, PVOID a2, ULONG a3);
    OS2EXPORT BOOL OS2API PtInRegion(ULONG a0, LONG a1, LONG a2);
    OS2EXPORT BOOL OS2API RectInRegion(ULONG a0, PVOID a1);
    OS2EXPORT BOOL OS2API RectVisible(ULONG a0, PVOID a1);
    OS2EXPORT BOOL OS2API ResetDC(ULONG a0, PVOID a1);
    OS2EXPORT BOOL OS2API ResizePalette(ULONG a0, ULONG a1);
    OS2EXPORT BOOL OS2API RoundRect(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5, LONG a6);
    OS2EXPORT BOOL OS2API ScaleViewportExtEx(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4,
                                             PVOID a5);
    OS2EXPORT BOOL OS2API ScaleWindowExtEx(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, PVOID a5);
    OS2EXPORT BOOL OS2API ScrollDC(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4, ULONG a5,
                                   PVOID a6);
    OS2EXPORT BOOL OS2API ScrollWindow(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4);
    OS2EXPORT LONG OS2API ScrollWindowEx(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4, ULONG a5,
                                         PVOID a6, ULONG a7);
    OS2EXPORT LONG OS2API SetBitmapBits(ULONG a0, ULONG a1, PVOID a2);
    OS2EXPORT LONG OS2API SetDIBitsToDevice(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5,
                                            LONG a6, ULONG a7, ULONG a8, PVOID a9, PVOID a10,
                                            ULONG a11);
    OS2EXPORT ULONG OS2API SetEnhMetaFileBits(ULONG a0, PVOID a1);
    OS2EXPORT ULONG OS2API SetPaletteEntries(ULONG a0, ULONG a1, ULONG a2, PVOID a3);
    OS2EXPORT ULONG OS2API SetPixel(ULONG a0, LONG a1, LONG a2, ULONG a3);
    OS2EXPORT LONG OS2API SetPolyFillMode(ULONG a0, LONG a1);
    OS2EXPORT LONG OS2API SetStretchBltMode(ULONG a0, LONG a1);
    OS2EXPORT LONG OS2API SetTextCharacterExtra(ULONG a0, LONG a1);
    OS2EXPORT BOOL OS2API SetTextJustification(ULONG a0, LONG a1, LONG a2);
    OS2EXPORT ULONG OS2API SetWinMetaFileBits(ULONG a0, PVOID a1, ULONG a2, PVOID a3);
    OS2EXPORT BOOL OS2API StretchBlt(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, ULONG a5,
                                     LONG a6, LONG a7, LONG a8, LONG a9, ULONG a10);
    OS2EXPORT BOOL OS2API StrokePath(ULONG a0);
    OS2EXPORT LONG OS2API TabbedTextOut(ULONG a0, LONG a1, LONG a2, PCHAR a3, LONG a4, LONG a5,
                                        PVOID a6, LONG a7);
    OS2EXPORT BOOL OS2API UnrealizeObject(ULONG a0);
    OS2EXPORT ULONG OS2API WindowFromDC(ULONG a0);
    OS2EXPORT ULONG OS2API DeviceCapabilities(PSZ a0, PSZ a1, ULONG a2, PVOID a3, PVOID a4);
    OS2EXPORT ULONG OS2API HPSToHDC(ULONG a0, ULONG a1, PSZ a2, PVOID a3);
    OS2EXPORT VOID OS2API DeleteHDCObjects(ULONG a0);
    OS2EXPORT LONG OS2API SetAbortProc(ULONG a0, ULONG a1);
    OS2EXPORT ULONG OS2API _CreateBitmapIndirect(PVOID a0);
    OS2EXPORT BOOL OS2API WinTranslateDeviceRects(ULONG a0, ULONG a1, PVOID a2, LONG a3, ULONG a4);
    OS2EXPORT ULONG OS2API WinTranslateGraphicsObjectHandle(ULONG a0, ULONG a1, ULONG a2);
    OS2EXPORT ULONG OS2API AddAtom(PSZ name);
    OS2EXPORT BOOL OS2API AdjustWindowRect(PVOID rectangle, ULONG style, BOOL menu);
    OS2EXPORT BOOL OS2API Beep(ULONG frequency, ULONG duration);
    OS2EXPORT ULONG OS2API BeginDeferWindowPos(LONG count);
    OS2EXPORT BOOL OS2API CheckRadioButton(ULONG dialog, LONG first, LONG last, LONG checked);
    OS2EXPORT ULONG OS2API ChildWindowFromPoint(ULONG parent, LONG x, LONG y);
    OS2EXPORT BOOL OS2API ChooseColor(PVOID record);
    OS2EXPORT BOOL OS2API ChooseFont(PVOID record);
    OS2EXPORT BOOL OS2API ClipCursor(PVOID rectangle);
    OS2EXPORT LONG OS2API CompareFileTime(PVOID first, PVOID second);
    OS2EXPORT BOOL OS2API CopyFile(PSZ source, PSZ destination, BOOL exclusive);
    OS2EXPORT ULONG OS2API CreateDialog(ULONG module, PSZ resource, ULONG owner, ULONG procedure);
    OS2EXPORT ULONG OS2API CreateDialogParam(ULONG module, PSZ resource, ULONG owner,
                                             ULONG procedure, ULONG parameter);
    OS2EXPORT BOOL OS2API CreateDirectory(PSZ path, PVOID security);
    OS2EXPORT ULONG OS2API CreateEvent(PVOID security, BOOL manual, BOOL initial, PSZ name);
    OS2EXPORT ULONG OS2API CreateMutex(PVOID security, BOOL owner, PSZ name);
    OS2EXPORT BOOL OS2API DdeAbandonTransaction(ULONG id, ULONG handle, ULONG transaction);
    OS2EXPORT ULONG OS2API DdeAccessData(ULONG handle, PVOID size);
    OS2EXPORT ULONG OS2API DdeAddData(ULONG handle, PVOID input, ULONG count, ULONG offset);
    OS2EXPORT ULONG OS2API DdeClientTransaction(ULONG address, ULONG size, ULONG handle, ULONG item,
                                                ULONG format, ULONG type, ULONG timeout,
                                                PVOID status);
    OS2EXPORT ULONG OS2API DdeConnect(ULONG id, ULONG service, ULONG topic, PVOID context);
    OS2EXPORT ULONG OS2API DdeCreateDataHandle(ULONG id, PVOID input, ULONG count, ULONG offset,
                                               ULONG item, ULONG format, ULONG flags);
    OS2EXPORT BOOL OS2API DdeDisconnect(ULONG handle);
    OS2EXPORT BOOL OS2API DdeFreeDataHandle(ULONG handle);
    OS2EXPORT ULONG OS2API DdeGetData(ULONG handle, PVOID output, ULONG capacity, ULONG offset);
    OS2EXPORT BOOL OS2API DdePostAdvise(ULONG id, ULONG topic, ULONG item);
    OS2EXPORT ULONG OS2API DdeQueryConvInfo(ULONG handle, ULONG transaction, PVOID output);
    OS2EXPORT BOOL OS2API DdeSetUserHandle(ULONG handle, ULONG transaction, ULONG user);
    OS2EXPORT BOOL OS2API DdeUnaccessData(ULONG handle);
    OS2EXPORT LONG OS2API DefDlgProc(ULONG window, ULONG message, ULONG first, ULONG second);
    OS2EXPORT ULONG OS2API DeferWindowPos(ULONG handle, ULONG window, ULONG after, LONG x, LONG y,
                                          LONG width, LONG height, ULONG flags);
    OS2EXPORT ULONG OS2API DeleteAtom(ULONG atom);
    OS2EXPORT BOOL OS2API DeleteFile(PSZ path);
    OS2EXPORT BOOL OS2API DlgDirSelectEx(ULONG dialog, PSZ output, LONG capacity, LONG id);
    OS2EXPORT BOOL OS2API DosDateTimeToFileTime(USHORT date, USHORT time, PVOID output);
    OS2EXPORT VOID OS2API DragAcceptFiles(ULONG window, BOOL accept);
    OS2EXPORT VOID OS2API DragFinish(ULONG drop);
    OS2EXPORT ULONG OS2API DragQueryFile(ULONG drop, ULONG index, PSZ output, ULONG capacity);
    OS2EXPORT BOOL OS2API DragQueryPoint(ULONG drop, PVOID output);
    OS2EXPORT BOOL OS2API EndDeferWindowPos(ULONG handle);
    OS2EXPORT BOOL OS2API EnumThreadWindows(ULONG thread, ULONG procedure, ULONG parameter);
    OS2EXPORT BOOL OS2API EnumWindows(ULONG procedure, ULONG parameter);
    OS2EXPORT BOOL OS2API FileTimeToDosDateTime(PVOID input, PVOID date, PVOID time);
    OS2EXPORT ULONG OS2API FindAtom(PSZ name);
    OS2EXPORT ULONG OS2API FindWindow(PSZ class_name, PSZ title);
    OS2EXPORT ULONG OS2API GetCursor(void);
    OS2EXPORT BOOL OS2API GetDiskFreeSpace(PSZ root, PVOID sectors, PVOID bytes,
                                           PVOID free_clusters, PVOID total_clusters);
    OS2EXPORT LONG OS2API GetDlgCtrlID(ULONG window);
    OS2EXPORT ULONG OS2API GetDlgItemInt(ULONG dialog, LONG id, PVOID translated, BOOL sign);
    OS2EXPORT ULONG OS2API GetDoubleClickTime(void);
    OS2EXPORT ULONG OS2API GetEnvironmentStrings(void);
    OS2EXPORT ULONG OS2API GetEnvironmentVariable(PSZ name, PSZ output, ULONG capacity);
    OS2EXPORT BOOL OS2API GetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write);
    OS2EXPORT SHORT OS2API GetFileTitle(PSZ path, PSZ output, USHORT capacity);
    OS2EXPORT ULONG OS2API GetForegroundWindow(void);
    OS2EXPORT LONG OS2API GetKeyNameText(ULONG parameter, PSZ output, LONG capacity);
    OS2EXPORT ULONG OS2API GetLastActivePopup(ULONG window);
    OS2EXPORT ULONG OS2API GetNextDlgTabItem(ULONG dialog, ULONG control, BOOL previous);
    OS2EXPORT ULONG OS2API GetNextWindow(ULONG window, ULONG direction);
    OS2EXPORT LONG OS2API GetPriorityClipboardFormat(PVOID formats, LONG count);
    OS2EXPORT ULONG OS2API GetPrivateProfileInt(PSZ section, PSZ key, LONG fallback, PSZ filename);
    OS2EXPORT ULONG OS2API GetSystemDirectory(PSZ output, ULONG capacity);
    OS2EXPORT ULONG OS2API GetTempFileName(PSZ path, PSZ prefix, ULONG unique, PSZ output);
    OS2EXPORT ULONG OS2API GetTempPath(ULONG capacity, PSZ output);
    OS2EXPORT ULONG OS2API GetTimeZoneInformation(PVOID output);
    OS2EXPORT ULONG OS2API GetTopWindow(ULONG parent);
    OS2EXPORT LONG OS2API GetUpdateRgn(ULONG window, ULONG output, BOOL erase);
    OS2EXPORT USHORT OS2API GetWindowWord(ULONG handle, LONG index);
    OS2EXPORT VOID OS2API GlobalMemoryStatus(PVOID output);
    OS2EXPORT BOOL OS2API IsBadHugeReadPtr(PVOID pointer, ULONG size);
    OS2EXPORT BOOL OS2API IsBadHugeWritePtr(PVOID pointer, ULONG size);
    OS2EXPORT BOOL OS2API IsBadStringPtr(ULONG address, ULONG maximum);
    OS2EXPORT BOOL OS2API IsCharAlpha(ULONG character);
    OS2EXPORT BOOL OS2API IsCharAlphaNumeric(ULONG character);
    OS2EXPORT BOOL OS2API IsCharUpper(ULONG character);
    OS2EXPORT BOOL OS2API IsChild(ULONG parent, ULONG child);
    OS2EXPORT BOOL OS2API IsDialogMessage(ULONG dialog, PVOID message);
    OS2EXPORT BOOL OS2API LockWindowUpdate(ULONG window);
    OS2EXPORT BOOL OS2API MapDialogRect(ULONG dialog, PVOID rectangle);
    OS2EXPORT LONG OS2API MapWindowPoints(ULONG from, ULONG to, PVOID points, ULONG count);
    OS2EXPORT LONG OS2API MessageBox(ULONG owner, PSZ text, PSZ caption, ULONG flags);
    OS2EXPORT BOOL OS2API ModifyMenu(ULONG handle, ULONG identity, ULONG flags, ULONG identifier,
                                     PSZ text);
    OS2EXPORT BOOL OS2API MoveFile(PSZ source, PSZ destination);
    OS2EXPORT VOID OS2API OutputDebugString(PSZ text);
    OS2EXPORT BOOL OS2API PostThreadMessage(ULONG thread, ULONG message, ULONG first, ULONG second);
    OS2EXPORT LONG OS2API RegQueryInfoKey(ULONG key, PSZ class_name, PULONG class_size,
                                          PULONG reserved, PULONG subkeys, PULONG max_subkey,
                                          PULONG max_class, PULONG values, PULONG max_value_name,
                                          PULONG max_value_data, PULONG security, PVOID last_write);
    OS2EXPORT LONG OS2API RegQueryValue(ULONG key, PSZ subkey, PSZ output, PULONG size);
    OS2EXPORT LONG OS2API RegSetValue(ULONG key, PSZ subkey, ULONG type, PVOID data, ULONG size);
    OS2EXPORT BOOL OS2API ReleaseMutex(ULONG handle);
    OS2EXPORT BOOL OS2API RemoveDirectory(PSZ path);
    OS2EXPORT BOOL OS2API AddFontResource(PSZ path);
    OS2EXPORT BOOL OS2API RemoveFontResource(PSZ path);
    OS2EXPORT BOOL OS2API ReplyMessage(LONG result);
    OS2EXPORT BOOL OS2API ResetEvent(ULONG handle);
    OS2EXPORT ULONG OS2API SearchPath(PSZ path, PSZ file, PSZ extension, ULONG capacity, PSZ output,
                                      PVOID part);
    OS2EXPORT BOOL OS2API SetDlgItemInt(ULONG dialog, LONG id, ULONG value, BOOL sign);
    OS2EXPORT BOOL OS2API SetEnvironmentVariable(PSZ name, PSZ value);
    OS2EXPORT BOOL OS2API SetEvent(ULONG handle);
    OS2EXPORT BOOL OS2API SetFileAttributes(PSZ path, ULONG attributes);
    OS2EXPORT BOOL OS2API SetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write);
    OS2EXPORT ULONG OS2API SetHandleCount(ULONG count);
    OS2EXPORT USHORT OS2API SetWindowWord(ULONG handle, LONG index, USHORT value);
    OS2EXPORT LONG OS2API ShowCursor(BOOL show);
    OS2EXPORT BOOL OS2API TrackPopupMenu(ULONG handle, ULONG flags, LONG x, LONG y, LONG reserved,
                                         ULONG owner, PVOID exclude);
    OS2EXPORT BOOL OS2API TranslateMDISysAccel(ULONG client, PVOID message);
    OS2EXPORT SHORT OS2API VkKeyScan(ULONG character);
    OS2EXPORT BOOL OS2API WriteProfileString(PSZ section, PSZ key, PSZ value);
    OS2EXPORT VOID OS2API ZeroMemory(PVOID output, ULONG count);
    OS2EXPORT LONG OS2API _lcreat(PSZ path, LONG attributes);
    OS2EXPORT LONG OS2API _lopen(PSZ path, LONG flags);
    OS2EXPORT ULONG OS2API MapVirtualKey(ULONG code, ULONG kind);
    OS2EXPORT ULONG OS2API GetMenuContextHelpId(ULONG handle);
    OS2EXPORT BOOL OS2API SetMenuContextHelpId(ULONG handle, ULONG help);
    OS2EXPORT ULONG OS2API ExtractIcon(ULONG instance, PSZ path, ULONG index);
    OS2EXPORT LONG OS2API lstrcmp2(PSZ first, PSZ second);
    OS2EXPORT ULONG OS2API GetKeyboardLayout(ULONG thread);
    OS2EXPORT BOOL OS2API GetStringTypeEx(ULONG locale, ULONG kind, PSZ input, LONG count,
                                          PVOID output);
    OS2EXPORT ULONG OS2API _CreateDialogIndirectParam(ULONG module, ULONG address, ULONG owner,
                                                      ULONG procedure, ULONG parameter);
    OS2EXPORT LONG OS2API _DialogBoxIndirect(ULONG module, ULONG address, ULONG owner,
                                             ULONG procedure);
    OS2EXPORT LONG OS2API _DialogBoxIndirectParam(ULONG module, ULONG address, ULONG owner,
                                                  ULONG procedure, ULONG parameter);
    OS2EXPORT BOOL OS2API WinXTranslateMsg(ULONG anchor, PVOID input, PVOID output);
    OS2EXPORT BOOL OS2API CreateProcess(PSZ application, PSZ command, PVOID process_security,
                                        PVOID thread_security, BOOL inherit, ULONG flags,
                                        PVOID environment, PSZ directory, PVOID startup,
                                        PVOID information);
    OS2EXPORT BOOL OS2API DuplicateHandle(ULONG source_process, ULONG source, ULONG target_process,
                                          PULONG output, ULONG access, BOOL inherit, ULONG options);
    OS2EXPORT VOID OS2API ExitProcess(ULONG code);
    OS2EXPORT BOOL OS2API ExitWindows(ULONG reserved, ULONG code);
    OS2EXPORT VOID OS2API FatalAppExit(ULONG action, PSZ message);
    OS2EXPORT VOID OS2API FatalExit(ULONG code);
    OS2EXPORT ULONG OS2API OpenProcess(ULONG access, BOOL inherit, ULONG id);
    OS2EXPORT ULONG OS2API SuspendThread(ULONG handle);
    OS2EXPORT BOOL OS2API TerminateThread(ULONG handle, ULONG code);
    OS2EXPORT ULONG OS2API WinExec(PSZ command, ULONG show);
    OS2EXPORT BOOL OS2API GetExitCodeProcess(ULONG handle, PULONG code);
    OS2EXPORT ULONG OS2API ShellExecute(ULONG window, PSZ operation, PSZ file, PSZ parameters,
                                        PSZ directory, LONG show);
    OS2EXPORT ULONG OS2API WaitForInputIdle(ULONG handle, ULONG timeout);
#ifdef __cplusplus
}
#endif
#endif
