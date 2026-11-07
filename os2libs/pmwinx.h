/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 exports use OS/2's caller-cleaned 32-bit system linkage. */
#ifndef TWOINE_OS2LIBS_PMWINX_H
#define TWOINE_OS2LIBS_PMWINX_H
#include "os2types.h"
typedef LONG(OS2API *PFNWINMAIN)(ULONG instance, ULONG previous, PSZ command, LONG show);
#ifdef __cplusplus
extern "C"
{
#endif
    OS2EXPORT LONG OS2API WinCallWinMain(LONG argc, PSZ *argv, PFNWINMAIN entry, LONG show)
        OS2APIINFO(ord = 6000, name = WINCALLWINMAIN);
    OS2EXPORT BOOL OS2API DestroyCursor(ULONG cursor) OS2APIINFO(ord = 143, name = DESTROYCURSOR);
    OS2EXPORT VOID OS2API WinInitializeOpen32(ULONG reserved)
        OS2APIINFO(ord = 6009, name = WININITIALIZEOPEN32);
    OS2EXPORT VOID OS2API WinTerminateOpen32(ULONG reserved)
        OS2APIINFO(ord = 6010, name = WINTERMINATEOPEN32);
    OS2EXPORT BOOL OS2API WinXProcessMsg(PVOID message)
        OS2APIINFO(ord = 6007, name = WINXPROCESSMSG);
    OS2EXPORT ULONG OS2API DdeCreateStringHandle(ULONG id, PSZ text, LONG codepage)
        OS2APIINFO(ord = 109, name = DDECREATESTRINGHANDLE);
    OS2EXPORT BOOL OS2API DdeFreeStringHandle(ULONG id, ULONG string)
        OS2APIINFO(ord = 114, name = DDEFREESTRINGHANDLE);
    OS2EXPORT BOOL OS2API DdeKeepStringHandle(ULONG id, ULONG string)
        OS2APIINFO(ord = 118, name = DDEKEEPSTRINGHANDLE);
    OS2EXPORT ULONG OS2API DdeQueryString(ULONG id, ULONG string, PSZ output, ULONG capacity,
                                          LONG codepage)
        OS2APIINFO(ord = 123, name = DDEQUERYSTRING);
    OS2EXPORT LONG OS2API DdeCmpStringHandles(ULONG first, ULONG second)
        OS2APIINFO(ord = 105, name = DDECMPSTRINGHANDLES);
    OS2EXPORT ULONG OS2API DdeNameService(ULONG id, ULONG string, ULONG reserved, ULONG command)
        OS2APIINFO(ord = 119, name = DDENAMESERVICE);
    OS2EXPORT ULONG OS2API DdeInitialize(PULONG id, ULONG callback, ULONG flags, ULONG reserved)
        OS2APIINFO(ord = 117, name = DDEINITIALIZE);
    OS2EXPORT ULONG OS2API DdeGetLastError(ULONG id) OS2APIINFO(ord = 116, name = DDEGETLASTERROR);
    OS2EXPORT BOOL OS2API DdeUninitialize(ULONG id) OS2APIINFO(ord = 127, name = DDEUNINITIALIZE);
    OS2EXPORT ULONG OS2API GetLastError(void) OS2APIINFO(ord = 300, name = GETLASTERROR);
    OS2EXPORT BOOL OS2API GetSaveFileName(PVOID address)
        OS2APIINFO(ord = 350, name = GETSAVEFILENAME);
    OS2EXPORT BOOL OS2API GetOpenFileName(PVOID address)
        OS2APIINFO(ord = 329, name = GETOPENFILENAME);
    OS2EXPORT ULONG OS2API CommDlgExtendedError(void)
        OS2APIINFO(ord = 48, name = COMMDLGEXTENDEDERROR);
    OS2EXPORT ULONG OS2API IsDlgButtonChecked(ULONG dialog, LONG identity)
        OS2APIINFO(ord = 444, name = ISDLGBUTTONCHECKED);
    OS2EXPORT BOOL OS2API CheckDlgButton(ULONG dialog, LONG identity, ULONG check)
        OS2APIINFO(ord = 32, name = CHECKDLGBUTTON);
    OS2EXPORT VOID OS2API SetLastError(ULONG error) OS2APIINFO(ord = 624, name = SETLASTERROR);
    OS2EXPORT ULONG OS2API SetTimer(ULONG window, ULONG id, ULONG milliseconds, ULONG callback)
        OS2APIINFO(ord = 653, name = SETTIMER);
    OS2EXPORT BOOL OS2API KillTimer(ULONG window, ULONG id) OS2APIINFO(ord = 452, name = KILLTIMER);
    OS2EXPORT VOID OS2API Sleep(ULONG milliseconds) OS2APIINFO(ord = 673, name = SLEEP);
    OS2EXPORT ULONG OS2API GetModuleFileName(HMODULE module, PSZ output, ULONG capacity)
        OS2APIINFO(ord = 318, name = GETMODULEFILENAME);
    OS2EXPORT HMODULE OS2API LoadLibrary(PSZ name) OS2APIINFO(ord = 461, name = LOADLIBRARY);
    OS2EXPORT HMODULE OS2API GetModuleHandle(PSZ name)
        OS2APIINFO(ord = 319, name = GETMODULEHANDLE);
    OS2EXPORT BOOL OS2API FreeLibrary(HMODULE module) OS2APIINFO(ord = 223, name = FREELIBRARY);
    OS2EXPORT PFN OS2API GetProcAddress(HMODULE module, PSZ name)
        OS2APIINFO(ord = 341, name = GETPROCADDRESS);
    OS2EXPORT ULONG OS2API FindResource(HMODULE module, PSZ name, PSZ type)
        OS2APIINFO(ord = 213, name = FINDRESOURCE);
    OS2EXPORT ULONG OS2API LoadResource(HMODULE module, ULONG resource)
        OS2APIINFO(ord = 5007, name = _LOADRESOURCE);
    OS2EXPORT ULONG OS2API LockResource(ULONG resource) OS2APIINFO(ord = 476, name = LOCKRESOURCE);
    OS2EXPORT ULONG OS2API SizeofResource(HMODULE module, ULONG resource)
        OS2APIINFO(ord = 672, name = SIZEOFRESOURCE);
    OS2EXPORT ULONG OS2API GetUserDefaultLCID(void)
        OS2APIINFO(ord = 2066, name = GETUSERDEFAULTLCID);
    OS2EXPORT ULONG OS2API GetProfileInt(PSZ section, PSZ key, LONG fallback)
        OS2APIINFO(ord = 342, name = GETPROFILEINT);
    OS2EXPORT ULONG OS2API GetProfileString(PSZ section, PSZ key, PSZ fallback, PSZ output,
                                            ULONG capacity)
        OS2APIINFO(ord = 343, name = GETPROFILESTRING);
    OS2EXPORT ULONG OS2API GetPrivateProfileString(PSZ section, PSZ key, PSZ fallback, PSZ output,
                                                   ULONG capacity, PSZ filename)
        OS2APIINFO(ord = 340, name = GETPRIVATEPROFILESTRING);
    OS2EXPORT BOOL OS2API WritePrivateProfileString(PSZ section, PSZ key, PSZ value, PSZ filename)
        OS2APIINFO(ord = 718, name = WRITEPRIVATEPROFILESTRING);
    OS2EXPORT ULONG OS2API GlobalAddAtom(PSZ name) OS2APIINFO(ord = 395, name = GLOBALADDATOM);
    OS2EXPORT ULONG OS2API GlobalFindAtom(PSZ name) OS2APIINFO(ord = 399, name = GLOBALFINDATOM);
    OS2EXPORT ULONG OS2API GlobalDeleteAtom(ULONG atom)
        OS2APIINFO(ord = 397, name = GLOBALDELETEATOM);
    OS2EXPORT ULONG OS2API GlobalGetAtomName(ULONG atom, PSZ output, LONG capacity)
        OS2APIINFO(ord = 402, name = GLOBALGETATOMNAME);
    OS2EXPORT ULONG OS2API RegisterClass(PVOID record) OS2APIINFO(ord = 556, name = REGISTERCLASS);
    OS2EXPORT ULONG OS2API SetClassLong(ULONG window, LONG index, ULONG value)
        OS2APIINFO(ord = 600, name = SETCLASSLONG);
    OS2EXPORT BOOL OS2API RegisterClassAlias(PSZ name, PSZ alias)
        OS2APIINFO(ord = 729, name = REGISTERCLASSALIAS);
    OS2EXPORT BOOL OS2API UnregisterClass(PSZ name, ULONG instance)
        OS2APIINFO(ord = 705, name = UNREGISTERCLASS);
    OS2EXPORT BOOL OS2API GetClassInfo(ULONG instance, PSZ name, PVOID output)
        OS2APIINFO(ord = 240, name = GETCLASSINFO);
    OS2EXPORT ULONG OS2API CreateWindowEx(ULONG extended_style, PSZ class_name, PSZ title,
                                          ULONG style, LONG x, LONG y, LONG width, LONG height,
                                          ULONG parent, ULONG menu, ULONG instance, ULONG parameter)
        OS2APIINFO(ord = 99, name = CREATEWINDOWEX);
    OS2EXPORT LONG OS2API DefWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
        OS2APIINFO(ord = 131, name = DEFWINDOWPROC);
    OS2EXPORT BOOL OS2API DestroyWindow(ULONG handle) OS2APIINFO(ord = 146, name = DESTROYWINDOW);
    OS2EXPORT LONG OS2API GetWindowLong(ULONG handle, LONG index)
        OS2APIINFO(ord = 385, name = GETWINDOWLONG);
    OS2EXPORT LONG OS2API SetWindowLong(ULONG handle, LONG index, LONG value)
        OS2APIINFO(ord = 659, name = SETWINDOWLONG);
    OS2EXPORT ULONG OS2API GetOEMCP(void) OS2APIINFO(ord = 325, name = GETOEMCP);
    OS2EXPORT ULONG OS2API GetACP(void) OS2APIINFO(ord = 224, name = GETACP);
    OS2EXPORT LONG OS2API MultiByteToWideChar(ULONG codepage, ULONG flags, PSZ input, LONG bytes,
                                              PVOID utf16, LONG capacity)
        OS2APIINFO(ord = 2073, name = MULTIBYTETOWIDECHAR);
    OS2EXPORT LONG OS2API WideCharToMultiByte(ULONG codepage, ULONG flags, PVOID utf16, LONG units,
                                              PSZ output, LONG capacity, PSZ default_text,
                                              PULONG used_default)
        OS2APIINFO(ord = 2079, name = WIDECHARTOMULTIBYTE);
    OS2EXPORT ULONG OS2API GetCommandLine(void) OS2APIINFO(ord = 252, name = GETCOMMANDLINE);
    OS2EXPORT ULONG OS2API GetVersion(void) OS2APIINFO(ord = 1019, name = GETVERSION);
    OS2EXPORT ULONG OS2API timeGetTime(void) OS2APIINFO(ord = 691, name = TIMEGETTIME);
    OS2EXPORT BOOL OS2API OemToChar(ULONG source, ULONG destination)
        OS2APIINFO(ord = 495, name = OEMTOCHAR);
    OS2EXPORT BOOL OS2API CharToOem(ULONG source, ULONG destination)
        OS2APIINFO(ord = 28, name = CHARTOOEM);
    OS2EXPORT BOOL OS2API OemToCharBuff(ULONG source, ULONG destination, ULONG count)
        OS2APIINFO(ord = 496, name = OEMTOCHARBUFF);
    OS2EXPORT BOOL OS2API CharToOemBuff(ULONG source, ULONG destination, ULONG count)
        OS2APIINFO(ord = 29, name = CHARTOOEMBUFF);
    OS2EXPORT ULONG OS2API LoadBitmap(HMODULE module, PSZ name)
        OS2APIINFO(ord = 458, name = LOADBITMAP);
    OS2EXPORT LONG OS2API GetObject(ULONG handle, LONG capacity, PVOID output)
        OS2APIINFO(ord = 326, name = GETOBJECT);
    OS2EXPORT BOOL OS2API DeleteObject(ULONG handle) OS2APIINFO(ord = 140, name = DELETEOBJECT);
    OS2EXPORT ULONG OS2API GetObjectType(ULONG handle) OS2APIINFO(ord = 327, name = GETOBJECTTYPE);
    OS2EXPORT ULONG OS2API GetNearestColor(ULONG dc, ULONG color)
        OS2APIINFO(ord = 320, name = GETNEARESTCOLOR);
    OS2EXPORT ULONG OS2API SetMetaFileBitsEx(ULONG size, PVOID data)
        OS2APIINFO(ord = 630, name = SETMETAFILEBITSEX);
    OS2EXPORT ULONG OS2API GetMetaFileBitsEx(ULONG handle, ULONG size, PVOID data)
        OS2APIINFO(ord = 316, name = GETMETAFILEBITSEX);
    OS2EXPORT BOOL OS2API DeleteMetaFile(ULONG handle) OS2APIINFO(ord = 139, name = DELETEMETAFILE);
    OS2EXPORT LONG OS2API GetSystemMetrics(LONG index)
        OS2APIINFO(ord = 359, name = GETSYSTEMMETRICS);
    OS2EXPORT BOOL OS2API SetWindowPos(ULONG handle, ULONG after, LONG x, LONG y, LONG width,
                                       LONG height, ULONG flags)
        OS2APIINFO(ord = 662, name = SETWINDOWPOS);
    OS2EXPORT BOOL OS2API MoveWindow(ULONG handle, LONG x, LONG y, LONG width, LONG height,
                                     BOOL repaint) OS2APIINFO(ord = 492, name = MOVEWINDOW);
    OS2EXPORT BOOL OS2API ShowWindow(ULONG handle, LONG command)
        OS2APIINFO(ord = 671, name = SHOWWINDOW);
    OS2EXPORT BOOL OS2API UpdateWindow(ULONG handle) OS2APIINFO(ord = 706, name = UPDATEWINDOW);
    OS2EXPORT ULONG OS2API GetDC(ULONG window) OS2APIINFO(ord = 263, name = GETDC);
    OS2EXPORT LONG OS2API GetDialogBaseUnits(void) OS2APIINFO(ord = 270, name = GETDIALOGBASEUNITS);
    OS2EXPORT LONG OS2API SaveDC(ULONG dc) OS2APIINFO(ord = 576, name = SAVEDC);
    OS2EXPORT BOOL OS2API RestoreDC(ULONG dc, LONG level) OS2APIINFO(ord = 572, name = RESTOREDC);
    OS2EXPORT LONG OS2API ReleaseDC(ULONG window, ULONG dc) OS2APIINFO(ord = 560, name = RELEASEDC);
    OS2EXPORT ULONG OS2API CreateCompatibleDC(ULONG source)
        OS2APIINFO(ord = 62, name = CREATECOMPATIBLEDC);
    OS2EXPORT BOOL OS2API DeleteDC(ULONG dc) OS2APIINFO(ord = 135, name = DELETEDC);
    OS2EXPORT ULONG OS2API SelectObject(ULONG dc, ULONG object)
        OS2APIINFO(ord = 585, name = SELECTOBJECT);
    OS2EXPORT BOOL OS2API BitBlt(ULONG destination, LONG x, LONG y, LONG width, LONG height,
                                 ULONG source, LONG source_x, LONG source_y, ULONG operation)
        OS2APIINFO(ord = 18, name = BITBLT);
    OS2EXPORT ULONG OS2API BeginPaint(ULONG window, PVOID record)
        OS2APIINFO(ord = 16, name = BEGINPAINT);
    OS2EXPORT BOOL OS2API EndPaint(ULONG window, PVOID record)
        OS2APIINFO(ord = 173, name = ENDPAINT);
    OS2EXPORT LONG OS2API SetBkMode(ULONG dc, LONG mode) OS2APIINFO(ord = 594, name = SETBKMODE);
    OS2EXPORT ULONG OS2API SetTextColor(ULONG dc, ULONG color)
        OS2APIINFO(ord = 649, name = SETTEXTCOLOR);
    OS2EXPORT ULONG OS2API SetBkColor(ULONG dc, ULONG color)
        OS2APIINFO(ord = 593, name = SETBKCOLOR);
    OS2EXPORT ULONG OS2API CreateFontIndirect(PVOID record)
        OS2APIINFO(ord = 76, name = CREATEFONTINDIRECT);
    OS2EXPORT ULONG OS2API GetStockObject(LONG index) OS2APIINFO(ord = 353, name = GETSTOCKOBJECT);
    OS2EXPORT LONG OS2API DrawText(ULONG dc, PSZ text, LONG count, PVOID rectangle, ULONG flags)
        OS2APIINFO(ord = 162, name = DRAWTEXT);
    OS2EXPORT BOOL OS2API TextOut(ULONG dc, LONG x, LONG y, PSZ text, LONG count)
        OS2APIINFO(ord = 689, name = TEXTOUT);
    OS2EXPORT ULONG OS2API CreateCompatibleBitmap(ULONG dc, LONG width, LONG height)
        OS2APIINFO(ord = 61, name = CREATECOMPATIBLEBITMAP);
    OS2EXPORT ULONG OS2API GetPixel(ULONG dc, LONG x, LONG y)
        OS2APIINFO(ord = 335, name = GETPIXEL);
    OS2EXPORT LONG OS2API OpenFile(PSZ name, PVOID record, ULONG flags)
        OS2APIINFO(ord = 504, name = OPENFILE);
    OS2EXPORT LONG OS2API _lclose(ULONG handle) OS2APIINFO(ord = 723, name = _LCLOSE);
    OS2EXPORT LONG OS2API _llseek(ULONG handle, LONG distance, LONG origin)
        OS2APIINFO(ord = 725, name = _LLSEEK);
    OS2EXPORT ULONG OS2API _lread(ULONG handle, PVOID output, ULONG size)
        OS2APIINFO(ord = 727, name = _LREAD);
    OS2EXPORT ULONG OS2API _lwrite(ULONG handle, PVOID input, ULONG size)
        OS2APIINFO(ord = 728, name = _LWRITE);
    OS2EXPORT BOOL OS2API ReadFile(ULONG handle, PVOID output, ULONG size, PULONG actual,
                                   PVOID overlapped) OS2APIINFO(ord = 535, name = READFILE);
    OS2EXPORT BOOL OS2API WriteFile(ULONG handle, PVOID input, ULONG size, PULONG actual,
                                    PVOID overlapped) OS2APIINFO(ord = 717, name = WRITEFILE);
    OS2EXPORT BOOL OS2API CloseHandle(ULONG handle) OS2APIINFO(ord = 44, name = CLOSEHANDLE);
    OS2EXPORT ULONG OS2API CreateFile(PSZ name, ULONG access, ULONG share, PVOID security,
                                      ULONG disposition, ULONG attributes, ULONG template)
        OS2APIINFO(ord = 74, name = CREATEFILE);
    OS2EXPORT ULONG OS2API SetFilePointer(ULONG handle, LONG distance, PLONG high, ULONG origin)
        OS2APIINFO(ord = 618, name = SETFILEPOINTER);
    OS2EXPORT ULONG OS2API GetFileSize(ULONG handle, PULONG high)
        OS2APIINFO(ord = 286, name = GETFILESIZE);
    OS2EXPORT BOOL OS2API SetEndOfFile(ULONG handle) OS2APIINFO(ord = 613, name = SETENDOFFILE);
    OS2EXPORT BOOL OS2API FileTimeToLocalFileTime(PVOID input, PVOID output)
        OS2APIINFO(ord = 204, name = FILETIMETOLOCALFILETIME);
    OS2EXPORT BOOL OS2API LocalFileTimeToFileTime(PVOID input, PVOID output)
        OS2APIINFO(ord = 467, name = LOCALFILETIMETOFILETIME);
    OS2EXPORT BOOL OS2API FileTimeToSystemTime(PVOID input, PVOID output)
        OS2APIINFO(ord = 205, name = FILETIMETOSYSTEMTIME);
    OS2EXPORT BOOL OS2API FlushFileBuffers(ULONG handle)
        OS2APIINFO(ord = 219, name = FLUSHFILEBUFFERS);
    OS2EXPORT ULONG OS2API GetFileType(ULONG handle) OS2APIINFO(ord = 289, name = GETFILETYPE);
    OS2EXPORT BOOL OS2API LockFile(ULONG handle, ULONG offset_low, ULONG offset_high,
                                   ULONG length_low, ULONG length_high)
        OS2APIINFO(ord = 475, name = LOCKFILE);
    OS2EXPORT BOOL OS2API UnlockFile(ULONG handle, ULONG offset_low, ULONG offset_high,
                                     ULONG length_low, ULONG length_high)
        OS2APIINFO(ord = 702, name = UNLOCKFILE);
    OS2EXPORT LONG OS2API wsprintf(PSZ output, PSZ format, ...)
        OS2APIINFO(ord = 720, name = WSPRINTF);
    OS2EXPORT LONG OS2API wvsprintf(PSZ output, PSZ format, PVOID arguments)
        OS2APIINFO(ord = 721, name = WVSPRINTF);
    OS2EXPORT ULONG OS2API LoadCursor(HMODULE module, PSZ name)
        OS2APIINFO(ord = 459, name = LOADCURSOR);
    OS2EXPORT ULONG OS2API SetCursor(ULONG cursor) OS2APIINFO(ord = 605, name = SETCURSOR);
    OS2EXPORT ULONG OS2API GetActiveWindow(void) OS2APIINFO(ord = 225, name = GETACTIVEWINDOW);
    OS2EXPORT ULONG OS2API SetActiveWindow(ULONG window)
        OS2APIINFO(ord = 589, name = SETACTIVEWINDOW);
    OS2EXPORT ULONG OS2API GetFocus(void) OS2APIINFO(ord = 290, name = GETFOCUS);
    OS2EXPORT ULONG OS2API SetFocus(ULONG window) OS2APIINFO(ord = 620, name = SETFOCUS);
    OS2EXPORT LONG OS2API GetDeviceCaps(ULONG dc, LONG index)
        OS2APIINFO(ord = 269, name = GETDEVICECAPS);
    OS2EXPORT BOOL OS2API GetTextMetrics(ULONG dc, PVOID output)
        OS2APIINFO(ord = 371, name = GETTEXTMETRICS);
    OS2EXPORT LONG OS2API EnumFontFamilies(ULONG dc, PSZ family, ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 179, name = ENUMFONTFAMILIES);
    OS2EXPORT ULONG OS2API LoadIcon(HMODULE module, PSZ name)
        OS2APIINFO(ord = 460, name = LOADICON);
    OS2EXPORT BOOL OS2API DestroyIcon(ULONG handle) OS2APIINFO(ord = 144, name = DESTROYICON);
    OS2EXPORT VOID OS2API InitializeCriticalSection(PVOID section)
        OS2APIINFO(ord = 419, name = INITIALIZECRITICALSECTION);
    OS2EXPORT VOID OS2API EnterCriticalSection(PVOID section)
        OS2APIINFO(ord = 175, name = ENTERCRITICALSECTION);
    OS2EXPORT VOID OS2API LeaveCriticalSection(PVOID section)
        OS2APIINFO(ord = 454, name = LEAVECRITICALSECTION);
    OS2EXPORT VOID OS2API DeleteCriticalSection(PVOID section)
        OS2APIINFO(ord = 134, name = DELETECRITICALSECTION);
    OS2EXPORT ULONG OS2API GetCurrentThreadId(void)
        OS2APIINFO(ord = 259, name = GETCURRENTTHREADID);
    OS2EXPORT ULONG OS2API GetCurrentThread(void) OS2APIINFO(ord = 258, name = GETCURRENTTHREAD);
    OS2EXPORT ULONG OS2API CreateThread(PVOID security, ULONG stack, ULONG procedure,
                                        ULONG parameter, ULONG flags, PULONG id)
        OS2APIINFO(ord = 98, name = CREATETHREAD);
    OS2EXPORT BOOL OS2API GetExitCodeThread(ULONG handle, PULONG result)
        OS2APIINFO(ord = 1006, name = GETEXITCODETHREAD);
    OS2EXPORT ULONG OS2API ResumeThread(ULONG handle) OS2APIINFO(ord = 573, name = RESUMETHREAD);
    OS2EXPORT ULONG OS2API WaitForSingleObject(ULONG handle, ULONG timeout)
        OS2APIINFO(ord = 711, name = WAITFORSINGLEOBJECT);
    OS2EXPORT ULONG OS2API CreateSemaphore(PVOID security, LONG initial, LONG maximum, PSZ name)
        OS2APIINFO(ord = 96, name = CREATESEMAPHORE);
    OS2EXPORT ULONG OS2API OpenSemaphore(ULONG access, BOOL inherit, PSZ name)
        OS2APIINFO(ord = 507, name = OPENSEMAPHORE);
    OS2EXPORT BOOL OS2API ReleaseSemaphore(ULONG handle, LONG count, PLONG previous)
        OS2APIINFO(ord = 562, name = RELEASESEMAPHORE);
    OS2EXPORT VOID OS2API ExitThread(ULONG result) OS2APIINFO(ord = 193, name = EXITTHREAD);
    OS2EXPORT BOOL OS2API SetThreadPriority(ULONG thread, LONG priority)
        OS2APIINFO(ord = 651, name = SETTHREADPRIORITY);
    OS2EXPORT ULONG OS2API GetSysColor(LONG index) OS2APIINFO(ord = 356, name = GETSYSCOLOR);
    OS2EXPORT ULONG OS2API CreateSolidBrush(ULONG color)
        OS2APIINFO(ord = 97, name = CREATESOLIDBRUSH);
    OS2EXPORT LONG OS2API FillRect(ULONG dc, PVOID rectangle, ULONG brush)
        OS2APIINFO(ord = 207, name = FILLRECT);
    OS2EXPORT VOID OS2API GetSystemTime(PVOID output) OS2APIINFO(ord = 361, name = GETSYSTEMTIME);
    OS2EXPORT BOOL OS2API SystemTimeToFileTime(PVOID input, PVOID output)
        OS2APIINFO(ord = 684, name = SYSTEMTIMETOFILETIME);
    OS2EXPORT LONG OS2API GetLocaleInfo(ULONG locale, ULONG type, PSZ output, LONG capacity)
        OS2APIINFO(ord = 2055, name = GETLOCALEINFO);
    OS2EXPORT BOOL OS2API IsDBCSLeadByte(ULONG character)
        OS2APIINFO(ord = 442, name = ISDBCSLEADBYTE);
    OS2EXPORT ULONG OS2API GetDriveType(PSZ root) OS2APIINFO(ord = 277, name = GETDRIVETYPE);
    OS2EXPORT ULONG OS2API GetLogicalDrives(void) OS2APIINFO(ord = 303, name = GETLOGICALDRIVES);
    OS2EXPORT BOOL OS2API GetVolumeInformation(PSZ root, PSZ label, ULONG label_capacity,
                                               PULONG serial, PULONG component_length, PULONG flags,
                                               PSZ filesystem, ULONG filesystem_capacity)
        OS2APIINFO(ord = 380, name = GETVOLUMEINFORMATION);
    OS2EXPORT ULONG OS2API GetLogicalDriveStrings(ULONG capacity, PSZ output)
        OS2APIINFO(ord = 302, name = GETLOGICALDRIVESTRINGS);
    OS2EXPORT ULONG OS2API GetFullPathName(PSZ name, ULONG capacity, PSZ output, PSZ *filepart)
        OS2APIINFO(ord = 292, name = GETFULLPATHNAME);
    OS2EXPORT ULONG OS2API GetWindowsDirectory(PSZ output, ULONG capacity)
        OS2APIINFO(ord = 393, name = GETWINDOWSDIRECTORY);
    OS2EXPORT ULONG OS2API GetCurrentDirectory(ULONG capacity, PSZ output)
        OS2APIINFO(ord = 253, name = GETCURRENTDIRECTORY);
    OS2EXPORT BOOL OS2API SetCurrentDirectory(PSZ name)
        OS2APIINFO(ord = 604, name = SETCURRENTDIRECTORY);
    OS2EXPORT USHORT OS2API GetUserDefaultLangID(void)
        OS2APIINFO(ord = 2065, name = GETUSERDEFAULTLANGID);
    OS2EXPORT VOID OS2API GetLocalTime(PVOID output) OS2APIINFO(ord = 301, name = GETLOCALTIME);
    OS2EXPORT ULONG OS2API CreatePalette(PVOID input) OS2APIINFO(ord = 85, name = CREATEPALETTE);
    OS2EXPORT ULONG OS2API SelectPalette(ULONG dc, ULONG palette, BOOL background)
        OS2APIINFO(ord = 586, name = SELECTPALETTE);
    OS2EXPORT ULONG OS2API RealizePalette(ULONG dc) OS2APIINFO(ord = 536, name = REALIZEPALETTE);
    OS2EXPORT ULONG OS2API GetPaletteEntries(ULONG palette, ULONG start, ULONG count, PVOID output)
        OS2APIINFO(ord = 332, name = GETPALETTEENTRIES);
    OS2EXPORT ULONG OS2API GetNearestPaletteIndex(ULONG palette, ULONG color)
        OS2APIINFO(ord = 321, name = GETNEARESTPALETTEINDEX);
    OS2EXPORT ULONG OS2API RegisterClipboardFormat(PSZ name)
        OS2APIINFO(ord = 557, name = REGISTERCLIPBOARDFORMAT);
    OS2EXPORT ULONG OS2API CreateMenu(void) OS2APIINFO(ord = 82, name = CREATEMENU);
    OS2EXPORT ULONG OS2API CreatePopupMenu(void) OS2APIINFO(ord = 91, name = CREATEPOPUPMENU);
    OS2EXPORT BOOL OS2API DestroyMenu(ULONG menu) OS2APIINFO(ord = 145, name = DESTROYMENU);
    OS2EXPORT BOOL OS2API InsertMenu(ULONG menu, ULONG position, ULONG flags, ULONG id, PSZ text)
        OS2APIINFO(ord = 420, name = INSERTMENU);
    OS2EXPORT BOOL OS2API AppendMenu(ULONG menu, ULONG flags, ULONG id, PSZ text)
        OS2APIINFO(ord = 9, name = APPENDMENU);
    OS2EXPORT LONG OS2API GetMenuItemCount(ULONG menu)
        OS2APIINFO(ord = 307, name = GETMENUITEMCOUNT);
    OS2EXPORT ULONG OS2API GetMenuItemID(ULONG menu, LONG position)
        OS2APIINFO(ord = 308, name = GETMENUITEMID);
    OS2EXPORT ULONG OS2API GetSubMenu(ULONG menu, LONG position)
        OS2APIINFO(ord = 355, name = GETSUBMENU);
    OS2EXPORT ULONG OS2API GetMenuState(ULONG menu, ULONG id, ULONG flags)
        OS2APIINFO(ord = 309, name = GETMENUSTATE);
    OS2EXPORT ULONG OS2API CheckMenuItem(ULONG menu, ULONG id, ULONG flags)
        OS2APIINFO(ord = 33, name = CHECKMENUITEM);
    OS2EXPORT ULONG OS2API EnableMenuItem(ULONG menu, ULONG id, ULONG flags)
        OS2APIINFO(ord = 166, name = ENABLEMENUITEM);
    OS2EXPORT BOOL OS2API IsMenu(ULONG menu) OS2APIINFO(ord = 446, name = ISMENU);
    OS2EXPORT BOOL OS2API RemoveMenu(ULONG menu, ULONG id, ULONG flags)
        OS2APIINFO(ord = 565, name = REMOVEMENU);
    OS2EXPORT BOOL OS2API DeleteMenu(ULONG menu, ULONG id, ULONG flags)
        OS2APIINFO(ord = 138, name = DELETEMENU);
    OS2EXPORT BOOL OS2API MessageBeep(ULONG type) OS2APIINFO(ord = 486, name = MESSAGEBEEP);
    OS2EXPORT ULONG OS2API GetCapture(void) OS2APIINFO(ord = 235, name = GETCAPTURE);
    OS2EXPORT ULONG OS2API SetCapture(ULONG window) OS2APIINFO(ord = 597, name = SETCAPTURE);
    OS2EXPORT BOOL OS2API ReleaseCapture(void) OS2APIINFO(ord = 559, name = RELEASECAPTURE);
    OS2EXPORT BOOL OS2API GetClientRect(ULONG window, PVOID output)
        OS2APIINFO(ord = 244, name = GETCLIENTRECT);
    OS2EXPORT BOOL OS2API GetWindowRect(ULONG window, PVOID output)
        OS2APIINFO(ord = 388, name = GETWINDOWRECT);
    OS2EXPORT BOOL OS2API ClientToScreen(ULONG window, PVOID output)
        OS2APIINFO(ord = 39, name = CLIENTTOSCREEN);
    OS2EXPORT BOOL OS2API ScreenToClient(ULONG window, PVOID output)
        OS2APIINFO(ord = 579, name = SCREENTOCLIENT);
    OS2EXPORT ULONG OS2API GetDesktopWindow(void) OS2APIINFO(ord = 268, name = GETDESKTOPWINDOW);
    OS2EXPORT LONG OS2API SendMessage(ULONG window, ULONG message, ULONG first, ULONG second)
        OS2APIINFO(ord = 588, name = SENDMESSAGE);
    OS2EXPORT LONG OS2API CallWindowProc(ULONG procedure, ULONG window, ULONG message, ULONG first,
                                         ULONG second) OS2APIINFO(ord = 22, name = CALLWINDOWPROC);
    OS2EXPORT BOOL OS2API SetRect(PVOID output, LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 639, name = SETRECT);
    OS2EXPORT BOOL OS2API SetProp(ULONG window, PSZ name, ULONG data)
        OS2APIINFO(ord = 637, name = SETPROP);
    OS2EXPORT ULONG OS2API GetProp(ULONG window, PSZ name) OS2APIINFO(ord = 344, name = GETPROP);
    OS2EXPORT ULONG OS2API RemoveProp(ULONG window, PSZ name)
        OS2APIINFO(ord = 566, name = REMOVEPROP);
    OS2EXPORT ULONG OS2API CreateIC(PSZ driver, PSZ device, PSZ output, PVOID mode)
        OS2APIINFO(ord = 78, name = CREATEIC);
    OS2EXPORT ULONG OS2API CreateDC(PSZ driver, PSZ device, PSZ output, PVOID mode)
        OS2APIINFO(ord = 64, name = CREATEDC);
    OS2EXPORT BOOL OS2API PrintDlg(PVOID data) OS2APIINFO(ord = 530, name = PRINTDLG);
    OS2EXPORT LONG OS2API StartDoc(ULONG dc, PVOID info) OS2APIINFO(ord = 674, name = STARTDOC);
    OS2EXPORT LONG OS2API StartPage(ULONG dc) OS2APIINFO(ord = 675, name = STARTPAGE);
    OS2EXPORT LONG OS2API EndPage(ULONG dc) OS2APIINFO(ord = 172, name = ENDPAGE);
    OS2EXPORT LONG OS2API EndDoc(ULONG dc) OS2APIINFO(ord = 171, name = ENDDOC);
    OS2EXPORT LONG OS2API AbortDoc(ULONG dc) OS2APIINFO(ord = 1, name = ABORTDOC);
    OS2EXPORT ULONG OS2API CreateRectRgn(LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 93, name = CREATERECTRGN);
    OS2EXPORT BOOL OS2API SetRectRgn(ULONG region, LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 641, name = SETRECTRGN);
    OS2EXPORT LONG OS2API CombineRgn(ULONG destination, ULONG first, ULONG second, LONG mode)
        OS2APIINFO(ord = 47, name = COMBINERGN);
    OS2EXPORT LONG OS2API SelectClipRgn(ULONG dc, ULONG region)
        OS2APIINFO(ord = 584, name = SELECTCLIPRGN);
    OS2EXPORT LONG OS2API GetRgnBox(ULONG region, PVOID output)
        OS2APIINFO(ord = 349, name = GETRGNBOX);
    OS2EXPORT ULONG OS2API CreateRectRgnIndirect(PVOID input)
        OS2APIINFO(ord = 94, name = CREATERECTRGNINDIRECT);
    OS2EXPORT ULONG OS2API CountClipboardFormats(void)
        OS2APIINFO(ord = 56, name = COUNTCLIPBOARDFORMATS);
    OS2EXPORT BOOL OS2API OpenClipboard(ULONG window) OS2APIINFO(ord = 502, name = OPENCLIPBOARD);
    OS2EXPORT BOOL OS2API CloseClipboard(void) OS2APIINFO(ord = 41, name = CLOSECLIPBOARD);
    OS2EXPORT BOOL OS2API EmptyClipboard(void) OS2APIINFO(ord = 165, name = EMPTYCLIPBOARD);
    OS2EXPORT ULONG OS2API GetClipboardData(ULONG format)
        OS2APIINFO(ord = 248, name = GETCLIPBOARDDATA);
    OS2EXPORT ULONG OS2API SetClipboardData(ULONG format, ULONG handle)
        OS2APIINFO(ord = 602, name = SETCLIPBOARDDATA);
    OS2EXPORT ULONG OS2API EnumClipboardFormats(ULONG format)
        OS2APIINFO(ord = 177, name = ENUMCLIPBOARDFORMATS);
    OS2EXPORT ULONG OS2API GetClipboardFormatName(ULONG format, PSZ output, LONG capacity)
        OS2APIINFO(ord = 249, name = GETCLIPBOARDFORMATNAME);
    OS2EXPORT ULONG OS2API GetClipboardOwner(void) OS2APIINFO(ord = 250, name = GETCLIPBOARDOWNER);
    OS2EXPORT BOOL OS2API IsClipboardFormatAvailable(ULONG format)
        OS2APIINFO(ord = 441, name = ISCLIPBOARDFORMATAVAILABLE);
    OS2EXPORT ULONG OS2API SetClipboardViewer(ULONG window)
        OS2APIINFO(ord = 603, name = SETCLIPBOARDVIEWER);
    OS2EXPORT ULONG OS2API GetClipboardViewer(void)
        OS2APIINFO(ord = 251, name = GETCLIPBOARDVIEWER);
    OS2EXPORT BOOL OS2API ChangeClipboardChain(ULONG window, ULONG next)
        OS2APIINFO(ord = 23, name = CHANGECLIPBOARDCHAIN);
    OS2EXPORT ULONG OS2API RegisterWindowMessage(PSZ name)
        OS2APIINFO(ord = 558, name = REGISTERWINDOWMESSAGE);
    OS2EXPORT LONG OS2API DefFrameProc(ULONG frame, ULONG client, ULONG message, ULONG first,
                                       ULONG second) OS2APIINFO(ord = 129, name = DEFFRAMEPROC);
    OS2EXPORT LONG OS2API DefMDIChildProc(ULONG child, ULONG message, ULONG first, ULONG second)
        OS2APIINFO(ord = 130, name = DEFMDICHILDPROC);
    OS2EXPORT ULONG OS2API SetWindowsHookEx(LONG type, ULONG procedure, ULONG module, ULONG thread)
        OS2APIINFO(ord = 665, name = SETWINDOWSHOOKEX);
    OS2EXPORT BOOL OS2API UnhookWindowsHookEx(ULONG hook)
        OS2APIINFO(ord = 700, name = UNHOOKWINDOWSHOOKEX);
    OS2EXPORT LONG OS2API CallNextHookEx(ULONG hook, LONG code, ULONG key, ULONG parameter)
        OS2APIINFO(ord = 21, name = CALLNEXTHOOKEX);
    OS2EXPORT SHORT OS2API GetKeyState(LONG key) OS2APIINFO(ord = 297, name = GETKEYSTATE);
    OS2EXPORT BOOL OS2API GetCursorPos(PVOID output) OS2APIINFO(ord = 262, name = GETCURSORPOS);
    OS2EXPORT BOOL OS2API SetCursorPos(LONG x, LONG y) OS2APIINFO(ord = 606, name = SETCURSORPOS);
    OS2EXPORT LONG OS2API GetMenuCheckMarkDimensions(void)
        OS2APIINFO(ord = 306, name = GETMENUCHECKMARKDIMENSIONS);
    OS2EXPORT ULONG OS2API WindowFromPoint(LONG x, LONG y)
        OS2APIINFO(ord = 716, name = WINDOWFROMPOINT);
    OS2EXPORT BOOL OS2API IsZoomed(ULONG window) OS2APIINFO(ord = 451, name = ISZOOMED);
    OS2EXPORT BOOL OS2API IsIconic(ULONG window) OS2APIINFO(ord = 445, name = ISICONIC);
    OS2EXPORT BOOL OS2API GetWindowPlacement(ULONG window, PVOID output)
        OS2APIINFO(ord = 387, name = GETWINDOWPLACEMENT);
    OS2EXPORT BOOL OS2API SetWindowPlacement(ULONG window, PVOID input)
        OS2APIINFO(ord = 661, name = SETWINDOWPLACEMENT);
    OS2EXPORT ULONG OS2API GetParent(ULONG window) OS2APIINFO(ord = 333, name = GETPARENT);
    OS2EXPORT ULONG OS2API SetParent(ULONG window, ULONG parent)
        OS2APIINFO(ord = 633, name = SETPARENT);
    OS2EXPORT BOOL OS2API RedrawWindow(ULONG window, PVOID rectangle, ULONG region, ULONG flags)
        OS2APIINFO(ord = 540, name = REDRAWWINDOW);
    OS2EXPORT LONG OS2API GetMenuString(ULONG menu, ULONG item, PCHAR output, LONG capacity,
                                        ULONG flags) OS2APIINFO(ord = 310, name = GETMENUSTRING);
    OS2EXPORT BOOL OS2API WinHelp(ULONG window, PSZ filename, ULONG command, ULONG data)
        OS2APIINFO(ord = 715, name = WINHELP);
    OS2EXPORT ULONG OS2API GetMenu(ULONG window) OS2APIINFO(ord = 305, name = GETMENU);
    OS2EXPORT BOOL OS2API SetMenu(ULONG window, ULONG menu) OS2APIINFO(ord = 628, name = SETMENU);
    OS2EXPORT BOOL OS2API DrawMenuBar(ULONG window) OS2APIINFO(ord = 161, name = DRAWMENUBAR);
    OS2EXPORT ULONG OS2API FindFirstFile(PSZ name, PVOID output)
        OS2APIINFO(ord = 211, name = FINDFIRSTFILE);
    OS2EXPORT BOOL OS2API FindNextFile(ULONG handle, PVOID output)
        OS2APIINFO(ord = 212, name = FINDNEXTFILE);
    OS2EXPORT BOOL OS2API FindClose(ULONG handle) OS2APIINFO(ord = 210, name = FINDCLOSE);
    OS2EXPORT ULONG OS2API CreateFont(LONG height, LONG width, LONG escapement, LONG orientation,
                                      LONG weight, ULONG italic, ULONG underline, ULONG strikeout,
                                      ULONG charset, ULONG out_precision, ULONG clip_precision,
                                      ULONG quality, ULONG pitch, PSZ face)
        OS2APIINFO(ord = 75, name = CREATEFONT);
    OS2EXPORT BOOL OS2API SetWindowText(ULONG window, PSZ text)
        OS2APIINFO(ord = 663, name = SETWINDOWTEXT);
    OS2EXPORT LONG OS2API GetWindowText(ULONG window, PCHAR text, LONG capacity)
        OS2APIINFO(ord = 389, name = GETWINDOWTEXT);
    OS2EXPORT LONG OS2API GetWindowTextLength(ULONG window)
        OS2APIINFO(ord = 390, name = GETWINDOWTEXTLENGTH);
    OS2EXPORT ULONG OS2API LocalAlloc(ULONG flags, ULONG size)
        OS2APIINFO(ord = 465, name = LOCALALLOC);
    OS2EXPORT ULONG OS2API LocalFree(ULONG handle) OS2APIINFO(ord = 469, name = LOCALFREE);
    OS2EXPORT ULONG OS2API LocalLock(ULONG handle) OS2APIINFO(ord = 471, name = LOCALLOCK);
    OS2EXPORT BOOL OS2API LocalUnlock(ULONG handle) OS2APIINFO(ord = 474, name = LOCALUNLOCK);
    OS2EXPORT ULONG OS2API LocalFlags(ULONG handle) OS2APIINFO(ord = 468, name = LOCALFLAGS);
    OS2EXPORT ULONG OS2API LocalSize(ULONG handle) OS2APIINFO(ord = 473, name = LOCALSIZE);
    OS2EXPORT ULONG OS2API LocalHandle(ULONG pointer) OS2APIINFO(ord = 470, name = LOCALHANDLE);
    OS2EXPORT ULONG OS2API LocalReAlloc(ULONG handle, ULONG size, ULONG flags)
        OS2APIINFO(ord = 472, name = LOCALREALLOC);
    OS2EXPORT ULONG OS2API GlobalAlloc(ULONG flags, ULONG size)
        OS2APIINFO(ord = 396, name = GLOBALALLOC);
    OS2EXPORT ULONG OS2API GlobalFree(ULONG handle) OS2APIINFO(ord = 401, name = GLOBALFREE);
    OS2EXPORT ULONG OS2API GlobalLock(ULONG handle) OS2APIINFO(ord = 404, name = GLOBALLOCK);
    OS2EXPORT BOOL OS2API GlobalUnlock(ULONG handle) OS2APIINFO(ord = 408, name = GLOBALUNLOCK);
    OS2EXPORT ULONG OS2API GlobalFlags(ULONG handle) OS2APIINFO(ord = 400, name = GLOBALFLAGS);
    OS2EXPORT ULONG OS2API GlobalSize(ULONG handle) OS2APIINFO(ord = 407, name = GLOBALSIZE);
    OS2EXPORT ULONG OS2API GlobalHandle(ULONG pointer) OS2APIINFO(ord = 403, name = GLOBALHANDLE);
    OS2EXPORT ULONG OS2API GlobalReAlloc(ULONG handle, ULONG size, ULONG flags)
        OS2APIINFO(ord = 406, name = GLOBALREALLOC);
    OS2EXPORT LONG OS2API GetDIBits(ULONG dc, ULONG bitmap, ULONG start, ULONG count, PVOID bits,
                                    PVOID information, ULONG usage)
        OS2APIINFO(ord = 267, name = GETDIBITS);
    OS2EXPORT LONG OS2API LoadString(HMODULE module, ULONG identity, PCHAR output, LONG capacity)
        OS2APIINFO(ord = 464, name = LOADSTRING);
    OS2EXPORT ULONG OS2API lstrcpy(ULONG destination, ULONG source)
        OS2APIINFO(ord = 481, name = LSTRCPY);
    OS2EXPORT ULONG OS2API lstrcat(ULONG destination, ULONG source)
        OS2APIINFO(ord = 478, name = LSTRCAT);
    OS2EXPORT LONG OS2API lstrlen(ULONG source) OS2APIINFO(ord = 482, name = LSTRLEN);
    OS2EXPORT LONG OS2API SetMapMode(ULONG dc, LONG mode) OS2APIINFO(ord = 626, name = SETMAPMODE);
    OS2EXPORT BOOL OS2API SetViewportOrgEx(ULONG dc, LONG x, LONG y, PVOID previous)
        OS2APIINFO(ord = 655, name = SETVIEWPORTORGEX);
    OS2EXPORT BOOL OS2API SetWindowOrgEx(ULONG dc, LONG x, LONG y, PVOID previous)
        OS2APIINFO(ord = 660, name = SETWINDOWORGEX);
    OS2EXPORT BOOL OS2API SetViewportExtEx(ULONG dc, LONG x, LONG y, PVOID previous)
        OS2APIINFO(ord = 654, name = SETVIEWPORTEXTEX);
    OS2EXPORT BOOL OS2API SetWindowExtEx(ULONG dc, LONG x, LONG y, PVOID previous)
        OS2APIINFO(ord = 658, name = SETWINDOWEXTEX);
    OS2EXPORT BOOL OS2API PlayMetaFile(ULONG dc, ULONG handle)
        OS2APIINFO(ord = 517, name = PLAYMETAFILE);
    OS2EXPORT BOOL OS2API EnumMetaFile(ULONG dc, ULONG handle, ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 181, name = ENUMMETAFILE);
    OS2EXPORT BOOL OS2API PlayMetaFileRecord(ULONG dc, PVOID table, PVOID record, ULONG count)
        OS2APIINFO(ord = 518, name = PLAYMETAFILERECORD);
    OS2EXPORT LONG OS2API StretchDIBits(ULONG dc, LONG x, LONG y, LONG width, LONG height,
                                        LONG source_x, LONG source_y, LONG source_width,
                                        LONG source_height, PVOID bits, PVOID information,
                                        ULONG usage, ULONG operation)
        OS2APIINFO(ord = 677, name = STRETCHDIBITS);
    OS2EXPORT LONG OS2API GetMapMode(ULONG dc) OS2APIINFO(ord = 304, name = GETMAPMODE);
    OS2EXPORT LONG OS2API IntersectClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 424, name = INTERSECTCLIPRECT);
    OS2EXPORT LONG OS2API ExcludeClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 190, name = EXCLUDECLIPRECT);
    OS2EXPORT BOOL OS2API GetTextExtentPoint32(ULONG dc, PCHAR text, LONG count, PVOID size)
        OS2APIINFO(ord = 369, name = GETTEXTEXTENTPOINT32);
    OS2EXPORT BOOL OS2API GetTextExtentExPoint(ULONG dc, PCHAR text, LONG count, LONG maximum,
                                               PVOID fit, PVOID advances, PVOID size)
        OS2APIINFO(ord = 1021, name = GETTEXTEXTENTEXPOINT);
    OS2EXPORT ULONG OS2API GetTabbedTextExtent(ULONG dc, PCHAR text, LONG count, LONG tab_count,
                                               PVOID tabs)
        OS2APIINFO(ord = 362, name = GETTABBEDTEXTEXTENT);
    OS2EXPORT BOOL OS2API EnableWindow(ULONG window, BOOL enable)
        OS2APIINFO(ord = 168, name = ENABLEWINDOW);
    OS2EXPORT BOOL OS2API IsWindowEnabled(ULONG window)
        OS2APIINFO(ord = 449, name = ISWINDOWENABLED);
    OS2EXPORT BOOL OS2API IsWindowVisible(ULONG window)
        OS2APIINFO(ord = 450, name = ISWINDOWVISIBLE);
    OS2EXPORT BOOL OS2API IsWindow(ULONG window) OS2APIINFO(ord = 448, name = ISWINDOW);
    OS2EXPORT BOOL OS2API InvalidateRect(ULONG window, PVOID rectangle, BOOL erase)
        OS2APIINFO(ord = 426, name = INVALIDATERECT);
    OS2EXPORT BOOL OS2API ValidateRect(ULONG window, PVOID rectangle)
        OS2APIINFO(ord = 707, name = VALIDATERECT);
    OS2EXPORT BOOL OS2API InvalidateRgn(ULONG window, ULONG region, BOOL erase)
        OS2APIINFO(ord = 427, name = INVALIDATERGN);
    OS2EXPORT BOOL OS2API ValidateRgn(ULONG window, ULONG region)
        OS2APIINFO(ord = 708, name = VALIDATERGN);
    OS2EXPORT BOOL OS2API GetUpdateRect(ULONG window, PVOID rectangle, BOOL erase)
        OS2APIINFO(ord = 376, name = GETUPDATERECT);
    OS2EXPORT BOOL OS2API SetScrollRange(ULONG window, LONG bar, LONG minimum, LONG maximum,
                                         BOOL redraw) OS2APIINFO(ord = 643, name = SETSCROLLRANGE);
    OS2EXPORT LONG OS2API SetScrollPos(ULONG window, LONG bar, LONG position, BOOL redraw)
        OS2APIINFO(ord = 642, name = SETSCROLLPOS);
    OS2EXPORT LONG OS2API GetScrollPos(ULONG window, LONG bar)
        OS2APIINFO(ord = 351, name = GETSCROLLPOS);
    OS2EXPORT BOOL OS2API ShowScrollBar(ULONG window, LONG bar, BOOL show)
        OS2APIINFO(ord = 670, name = SHOWSCROLLBAR);
    OS2EXPORT BOOL OS2API GetScrollRange(ULONG window, LONG bar, PVOID minimum, PVOID maximum)
        OS2APIINFO(ord = 352, name = GETSCROLLRANGE);
    OS2EXPORT ULONG OS2API GetClassLong(ULONG window, LONG index)
        OS2APIINFO(ord = 241, name = GETCLASSLONG);
    OS2EXPORT LONG OS2API GetClassName(ULONG window, PCHAR name, LONG capacity)
        OS2APIINFO(ord = 242, name = GETCLASSNAME);
    OS2EXPORT ULONG OS2API GetWindow(ULONG window, ULONG command)
        OS2APIINFO(ord = 382, name = GETWINDOW);
    OS2EXPORT BOOL OS2API CreateCaret(ULONG window, ULONG bitmap, LONG width, LONG height)
        OS2APIINFO(ord = 60, name = CREATECARET);
    OS2EXPORT BOOL OS2API DestroyCaret(void) OS2APIINFO(ord = 142, name = DESTROYCARET);
    OS2EXPORT BOOL OS2API HideCaret(ULONG window) OS2APIINFO(ord = 414, name = HIDECARET);
    OS2EXPORT BOOL OS2API ShowCaret(ULONG window) OS2APIINFO(ord = 667, name = SHOWCARET);
    OS2EXPORT BOOL OS2API SetCaretPos(LONG x, LONG y) OS2APIINFO(ord = 599, name = SETCARETPOS);
    OS2EXPORT ULONG OS2API GetCaretBlinkTime(void) OS2APIINFO(ord = 236, name = GETCARETBLINKTIME);
    OS2EXPORT BOOL OS2API SetCaretBlinkTime(ULONG interval)
        OS2APIINFO(ord = 598, name = SETCARETBLINKTIME);
    OS2EXPORT BOOL OS2API GetCaretPos(PVOID point) OS2APIINFO(ord = 237, name = GETCARETPOS);
    OS2EXPORT BOOL OS2API BringWindowToTop(ULONG window)
        OS2APIINFO(ord = 19, name = BRINGWINDOWTOTOP);
    OS2EXPORT ULONG OS2API CreatePen(LONG style, LONG width, ULONG color)
        OS2APIINFO(ord = 87, name = CREATEPEN);
    OS2EXPORT BOOL OS2API LineTo(ULONG dc, LONG x, LONG y) OS2APIINFO(ord = 456, name = LINETO);
    OS2EXPORT LONG OS2API SetROP2(ULONG dc, LONG mode) OS2APIINFO(ord = 638, name = SETROP2);
    OS2EXPORT ULONG OS2API CreatePenIndirect(PVOID record)
        OS2APIINFO(ord = 88, name = CREATEPENINDIRECT);
    OS2EXPORT BOOL OS2API MoveToEx(ULONG dc, LONG x, LONG y, PVOID previous)
        OS2APIINFO(ord = 491, name = MOVETOEX);
    OS2EXPORT LONG OS2API GetROP2(ULONG dc) OS2APIINFO(ord = 346, name = GETROP2);
    OS2EXPORT BOOL OS2API Rectangle(ULONG dc, LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 539, name = RECTANGLE);
    OS2EXPORT BOOL OS2API PeekMessage(PVOID message, ULONG window, ULONG first, ULONG last,
                                      ULONG flags) OS2APIINFO(ord = 513, name = PEEKMESSAGE);
    OS2EXPORT LONG OS2API GetMessage(PVOID message, ULONG window, ULONG first, ULONG last)
        OS2APIINFO(ord = 311, name = GETMESSAGE);
    OS2EXPORT BOOL OS2API TranslateMessage(PVOID message)
        OS2APIINFO(ord = 699, name = TRANSLATEMESSAGE);
    OS2EXPORT LONG OS2API DispatchMessage(PVOID message)
        OS2APIINFO(ord = 149, name = DISPATCHMESSAGE);
    OS2EXPORT BOOL OS2API PostMessage(ULONG window, ULONG message, ULONG first, ULONG second)
        OS2APIINFO(ord = 527, name = POSTMESSAGE);
    OS2EXPORT VOID OS2API PostQuitMessage(LONG status)
        OS2APIINFO(ord = 528, name = POSTQUITMESSAGE);
    OS2EXPORT LONG OS2API GetMessageTime(void) OS2APIINFO(ord = 314, name = GETMESSAGETIME);
    OS2EXPORT ULONG OS2API GetMessagePos(void) OS2APIINFO(ord = 313, name = GETMESSAGEPOS);
    OS2EXPORT ULONG OS2API GetSystemMenu(ULONG window, BOOL revert)
        OS2APIINFO(ord = 358, name = GETSYSTEMMENU);
    OS2EXPORT ULONG OS2API GetWindowThreadProcessId(ULONG window, PVOID process)
        OS2APIINFO(ord = 391, name = GETWINDOWTHREADPROCESSID);
    OS2EXPORT ULONG OS2API GetCurrentProcessId(void)
        OS2APIINFO(ord = 257, name = GETCURRENTPROCESSID);
    OS2EXPORT SHORT OS2API GetAsyncKeyState(LONG key)
        OS2APIINFO(ord = 1012, name = GETASYNCKEYSTATE);
    OS2EXPORT BOOL OS2API SetRectEmpty(PVOID output) OS2APIINFO(ord = 640, name = SETRECTEMPTY);
    OS2EXPORT BOOL OS2API CopyRect(PVOID output, PVOID input) OS2APIINFO(ord = 55, name = COPYRECT);
    OS2EXPORT BOOL OS2API IsRectEmpty(PVOID input) OS2APIINFO(ord = 447, name = ISRECTEMPTY);
    OS2EXPORT BOOL OS2API EqualRect(PVOID first, PVOID second)
        OS2APIINFO(ord = 187, name = EQUALRECT);
    OS2EXPORT BOOL OS2API UnionRect(PVOID output, PVOID first, PVOID second)
        OS2APIINFO(ord = 701, name = UNIONRECT);
    OS2EXPORT BOOL OS2API IntersectRect(PVOID output, PVOID first, PVOID second)
        OS2APIINFO(ord = 425, name = INTERSECTRECT);
    OS2EXPORT BOOL OS2API SubtractRect(PVOID output, PVOID first, PVOID second)
        OS2APIINFO(ord = 680, name = SUBTRACTRECT);
    OS2EXPORT BOOL OS2API OffsetRect(PVOID rectangle, LONG x, LONG y)
        OS2APIINFO(ord = 498, name = OFFSETRECT);
    OS2EXPORT BOOL OS2API InflateRect(PVOID rectangle, LONG x, LONG y)
        OS2APIINFO(ord = 417, name = INFLATERECT);
    OS2EXPORT BOOL OS2API PtInRect(PVOID rectangle, LONG x, LONG y)
        OS2APIINFO(ord = 531, name = PTINRECT);
    OS2EXPORT LONG OS2API GetClipBox(ULONG dc, PVOID rectangle)
        OS2APIINFO(ord = 245, name = GETCLIPBOX);
    OS2EXPORT LONG OS2API FrameRect(ULONG dc, PVOID rectangle, ULONG brush)
        OS2APIINFO(ord = 220, name = FRAMERECT);
    OS2EXPORT BOOL OS2API InvertRect(ULONG dc, PVOID rectangle)
        OS2APIINFO(ord = 428, name = INVERTRECT);
    OS2EXPORT ULONG OS2API CreateBitmap(LONG width, LONG height, ULONG planes, ULONG depth,
                                        PVOID bits) OS2APIINFO(ord = 58, name = CREATEBITMAP);
    OS2EXPORT LONG OS2API SetDIBits(ULONG dc, ULONG bitmap, ULONG start, ULONG count, PVOID bits,
                                    PVOID information, ULONG usage)
        OS2APIINFO(ord = 608, name = SETDIBITS);
    OS2EXPORT BOOL OS2API GetTextExtentPoint(ULONG dc, PCHAR text, LONG count, PVOID size)
        OS2APIINFO(ord = 368, name = GETTEXTEXTENTPOINT);
    OS2EXPORT BOOL OS2API ExtTextOut(ULONG dc, LONG x, LONG y, ULONG flags, PVOID rectangle,
                                     PCHAR text, ULONG count, PLONG advances)
        OS2APIINFO(ord = 200, name = EXTTEXTOUT);
    OS2EXPORT BOOL OS2API WaitMessage(void) OS2APIINFO(ord = 712, name = WAITMESSAGE);
    OS2EXPORT ULONG OS2API GetCurrentProcess(void) OS2APIINFO(ord = 256, name = GETCURRENTPROCESS);
    OS2EXPORT BOOL OS2API SetPriorityClass(ULONG process, ULONG priority)
        OS2APIINFO(ord = 636, name = SETPRIORITYCLASS);
    OS2EXPORT ULONG OS2API GetPriorityClass(ULONG process)
        OS2APIINFO(ord = 337, name = GETPRIORITYCLASS);
    OS2EXPORT LONG OS2API InterlockedIncrement(PLONG value)
        OS2APIINFO(ord = 423, name = INTERLOCKEDINCREMENT);
    OS2EXPORT LONG OS2API InterlockedDecrement(PLONG value)
        OS2APIINFO(ord = 421, name = INTERLOCKEDDECREMENT);
    OS2EXPORT LONG OS2API InterlockedExchange(PLONG value, LONG replacement)
        OS2APIINFO(ord = 422, name = INTERLOCKEDEXCHANGE);
    OS2EXPORT ULONG OS2API LoadMenu(ULONG module, PSZ resource)
        OS2APIINFO(ord = 462, name = LOADMENU);
    OS2EXPORT BOOL OS2API AdjustWindowRectEx(PVOID rectangle, ULONG style, BOOL menu,
                                             ULONG extended_style)
        OS2APIINFO(ord = 6, name = ADJUSTWINDOWRECTEX);
    OS2EXPORT LONG OS2API GetTextFace(ULONG dc, LONG capacity, PCHAR output)
        OS2APIINFO(ord = 370, name = GETTEXTFACE);
    OS2EXPORT BOOL OS2API EnumChildWindows(ULONG parent, ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 176, name = ENUMCHILDWINDOWS);
    OS2EXPORT BOOL OS2API WinTranslateDevicePoints(ULONG dc, ULONG window, PVOID points, LONG count,
                                                   LONG direction)
        OS2APIINFO(ord = 6003, name = WINTRANSLATEDEVICEPOINTS);
    OS2EXPORT LONG OS2API DialogBox(ULONG module, PSZ resource, ULONG owner, ULONG procedure)
        OS2APIINFO(ord = 147, name = DIALOGBOX);
    OS2EXPORT LONG OS2API DialogBoxParam(ULONG module, PSZ resource, ULONG owner, ULONG procedure,
                                         ULONG parameter)
        OS2APIINFO(ord = 148, name = DIALOGBOXPARAM);
    OS2EXPORT BOOL OS2API EndDialog(ULONG dialog, LONG result)
        OS2APIINFO(ord = 170, name = ENDDIALOG);
    OS2EXPORT ULONG OS2API GetDlgItem(ULONG dialog, LONG identity)
        OS2APIINFO(ord = 273, name = GETDLGITEM);
    OS2EXPORT BOOL OS2API SetDlgItemText(ULONG dialog, LONG identity, PSZ text)
        OS2APIINFO(ord = 611, name = SETDLGITEMTEXT);
    OS2EXPORT ULONG OS2API GetDlgItemText(ULONG dialog, LONG identity, PCHAR output, LONG capacity)
        OS2APIINFO(ord = 275, name = GETDLGITEMTEXT);
    OS2EXPORT LONG OS2API SendDlgItemMessage(ULONG dialog, LONG identity, ULONG message,
                                             ULONG first, ULONG second)
        OS2APIINFO(ord = 587, name = SENDDLGITEMMESSAGE);
    OS2EXPORT BOOL OS2API PatBlt(ULONG dc, LONG x, LONG y, LONG width, LONG height, ULONG operation)
        OS2APIINFO(ord = 511, name = PATBLT);
    OS2EXPORT ULONG OS2API CreatePatternBrush(ULONG bitmap)
        OS2APIINFO(ord = 86, name = CREATEPATTERNBRUSH);
    OS2EXPORT BOOL OS2API DPtoLP(ULONG dc, PVOID points, LONG count)
        OS2APIINFO(ord = 100, name = DPTOLP);
    OS2EXPORT BOOL OS2API LPtoDP(ULONG dc, PVOID points, LONG count)
        OS2APIINFO(ord = 453, name = LPTODP);
    OS2EXPORT BOOL OS2API GetKeyboardState(PVOID output)
        OS2APIINFO(ord = 1013, name = GETKEYBOARDSTATE);
    OS2EXPORT BOOL OS2API Polygon(ULONG dc, PVOID points, LONG count)
        OS2APIINFO(ord = 524, name = POLYGON);
    OS2EXPORT BOOL OS2API Polyline(ULONG dc, PVOID points, LONG count)
        OS2APIINFO(ord = 525, name = POLYLINE);
    OS2EXPORT ULONG OS2API SetTextAlign(ULONG dc, ULONG alignment)
        OS2APIINFO(ord = 647, name = SETTEXTALIGN);
    OS2EXPORT ULONG OS2API GetTextAlign(ULONG dc) OS2APIINFO(ord = 365, name = GETTEXTALIGN);
    OS2EXPORT BOOL OS2API SetBrushOrgEx(ULONG dc, LONG x, LONG y, PVOID previous)
        OS2APIINFO(ord = 596, name = SETBRUSHORGEX);
    OS2EXPORT BOOL OS2API GetBrushOrgEx(ULONG dc, PVOID output)
        OS2APIINFO(ord = 234, name = GETBRUSHORGEX);
    OS2EXPORT BOOL OS2API SetForegroundWindow(ULONG window)
        OS2APIINFO(ord = 621, name = SETFOREGROUNDWINDOW);
    OS2EXPORT BOOL OS2API IsBadReadPtr(PVOID pointer, ULONG bytes)
        OS2APIINFO(ord = 433, name = ISBADREADPTR);
    OS2EXPORT BOOL OS2API IsBadWritePtr(PVOID pointer, ULONG bytes)
        OS2APIINFO(ord = 435, name = ISBADWRITEPTR);
    OS2EXPORT BOOL OS2API IsBadCodePtr(ULONG procedure) OS2APIINFO(ord = 430, name = ISBADCODEPTR);
    OS2EXPORT ULONG OS2API CharUpper(ULONG pointer) OS2APIINFO(ord = 30, name = CHARUPPER);
    OS2EXPORT ULONG OS2API CharLower(ULONG pointer) OS2APIINFO(ord = 24, name = CHARLOWER);
    OS2EXPORT ULONG OS2API CharUpperBuff(ULONG pointer, ULONG count)
        OS2APIINFO(ord = 31, name = CHARUPPERBUFF);
    OS2EXPORT ULONG OS2API CharLowerBuff(ULONG pointer, ULONG count)
        OS2APIINFO(ord = 25, name = CHARLOWERBUFF);
    OS2EXPORT ULONG OS2API CharNext(ULONG pointer) OS2APIINFO(ord = 26, name = CHARNEXT);
    OS2EXPORT ULONG OS2API CharPrev(ULONG start, ULONG current)
        OS2APIINFO(ord = 27, name = CHARPREV);
    OS2EXPORT LONG OS2API lstrcmp(ULONG first, ULONG second) OS2APIINFO(ord = 479, name = LSTRCMP);
    OS2EXPORT LONG OS2API lstrcmpi(ULONG first, ULONG second)
        OS2APIINFO(ord = 480, name = LSTRCMPI);
    OS2EXPORT ULONG OS2API GetFileAttributes(PSZ name)
        OS2APIINFO(ord = 284, name = GETFILEATTRIBUTES);
    OS2EXPORT ULONG OS2API GetTickCount(void) OS2APIINFO(ord = 373, name = GETTICKCOUNT);
    OS2EXPORT ULONG OS2API GetCurrentTime(void) OS2APIINFO(ord = 260, name = GETCURRENTTIME);
    OS2EXPORT LONG OS2API MulDiv(LONG number, LONG numerator, LONG denominator)
        OS2APIINFO(ord = 494, name = MULDIV);
    OS2EXPORT LONG OS2API RegCloseKey(ULONG key) OS2APIINFO(ord = 541, name = REGCLOSEKEY);
    OS2EXPORT LONG OS2API RegDeleteKey(ULONG key, PSZ name)
        OS2APIINFO(ord = 544, name = REGDELETEKEY);
    OS2EXPORT LONG OS2API RegOpenKey(ULONG key, PSZ name, PULONG output)
        OS2APIINFO(ord = 549, name = REGOPENKEY);
    OS2EXPORT LONG OS2API RegOpenKeyEx(ULONG key, PSZ name, ULONG options, ULONG access,
                                       PULONG output) OS2APIINFO(ord = 550, name = REGOPENKEYEX);
    OS2EXPORT LONG OS2API RegCreateKey(ULONG key, PSZ name, PULONG output)
        OS2APIINFO(ord = 542, name = REGCREATEKEY);
    OS2EXPORT LONG OS2API RegCreateKeyEx(ULONG key, PSZ name, ULONG reserved, PSZ class_name,
                                         ULONG options, ULONG access, PVOID security, PULONG output,
                                         PULONG disposition)
        OS2APIINFO(ord = 543, name = REGCREATEKEYEX);
    OS2EXPORT LONG OS2API RegQueryValueEx(ULONG key, PSZ name, PULONG reserved, PULONG type,
                                          PVOID data, PULONG size)
        OS2APIINFO(ord = 553, name = REGQUERYVALUEEX);
    OS2EXPORT LONG OS2API RegEnumKey(ULONG key, ULONG index, PSZ name, ULONG capacity)
        OS2APIINFO(ord = 546, name = REGENUMKEY);
    OS2EXPORT LONG OS2API RegEnumKeyEx(ULONG key, ULONG index, PSZ name, PULONG name_size,
                                       PULONG reserved, PSZ class_name, PULONG class_size,
                                       PVOID last_write) OS2APIINFO(ord = 547, name = REGENUMKEYEX);
    OS2EXPORT LONG OS2API RegEnumValue(ULONG key, ULONG index, PSZ name, PULONG name_size,
                                       PULONG reserved, PULONG type, PVOID data, PULONG data_size)
        OS2APIINFO(ord = 548, name = REGENUMVALUE);
    OS2EXPORT LONG OS2API RegDeleteValue(ULONG key, PSZ name)
        OS2APIINFO(ord = 545, name = REGDELETEVALUE);
    OS2EXPORT LONG OS2API RegSetValueEx(ULONG key, PSZ name, ULONG reserved, ULONG type, PVOID data,
                                        ULONG size) OS2APIINFO(ord = 555, name = REGSETVALUEEX);
    OS2EXPORT ULONG OS2API TlsAlloc(void) OS2APIINFO(ord = 692, name = TLSALLOC);
    OS2EXPORT BOOL OS2API TlsFree(ULONG index) OS2APIINFO(ord = 693, name = TLSFREE);
    OS2EXPORT ULONG OS2API TlsGetValue(ULONG index) OS2APIINFO(ord = 694, name = TLSGETVALUE);
    OS2EXPORT BOOL OS2API TlsSetValue(ULONG index, ULONG value)
        OS2APIINFO(ord = 695, name = TLSSETVALUE);
    OS2EXPORT ULONG OS2API LoadAccelerators(HMODULE module, PSZ name)
        OS2APIINFO(ord = 457, name = LOADACCELERATORS);
    OS2EXPORT ULONG OS2API CreateAcceleratorTable(PVOID data, LONG count)
        OS2APIINFO(ord = 57, name = CREATEACCELERATORTABLE);
    OS2EXPORT LONG OS2API CopyAcceleratorTable(ULONG handle, PVOID data, LONG count)
        OS2APIINFO(ord = 1026, name = COPYACCELERATORTABLE);
    OS2EXPORT BOOL OS2API DestroyAcceleratorTable(ULONG handle)
        OS2APIINFO(ord = 141, name = DESTROYACCELERATORTABLE);
    OS2EXPORT LONG OS2API TranslateAccelerator(ULONG window, ULONG table, PVOID message)
        OS2APIINFO(ord = 697, name = TRANSLATEACCELERATOR);
    OS2EXPORT BOOL OS2API AnimatePalette(ULONG a0, ULONG a1, ULONG a2, PVOID a3)
        OS2APIINFO(ord = 8, name = ANIMATEPALETTE);
    OS2EXPORT BOOL OS2API Arc(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5, LONG a6, LONG a7, LONG a8)
        OS2APIINFO(ord = 10, name = ARC);
    OS2EXPORT BOOL OS2API BeginPath(ULONG a0) OS2APIINFO(ord = 17, name = BEGINPATH);
    OS2EXPORT ULONG OS2API CloseMetaFile(ULONG a0) OS2APIINFO(ord = 45, name = CLOSEMETAFILE);
    OS2EXPORT ULONG OS2API CopyMetaFile(ULONG a0, PSZ a1) OS2APIINFO(ord = 54, name = COPYMETAFILE);
    OS2EXPORT ULONG OS2API CreateBrushIndirect(PVOID a0)
        OS2APIINFO(ord = 59, name = CREATEBRUSHINDIRECT);
    OS2EXPORT ULONG OS2API CreateDIBitmap(ULONG a0, PVOID a1, ULONG a2, PVOID a3, PVOID a4, ULONG a5)
        OS2APIINFO(ord = 66, name = CREATEDIBITMAP);
    OS2EXPORT ULONG OS2API CreateEllipticRgn(LONG a0, LONG a1, LONG a2, LONG a3)
        OS2APIINFO(ord = 70, name = CREATEELLIPTICRGN);
    OS2EXPORT ULONG OS2API CreateEllipticRgnIndirect(PVOID a0)
        OS2APIINFO(ord = 71, name = CREATEELLIPTICRGNINDIRECT);
    OS2EXPORT ULONG OS2API CreateHatchBrush(LONG a0, ULONG a1)
        OS2APIINFO(ord = 77, name = CREATEHATCHBRUSH);
    OS2EXPORT ULONG OS2API CreateIcon(ULONG a0, LONG a1, LONG a2, ULONG a3, ULONG a4, PVOID a5, PVOID a6)
        OS2APIINFO(ord = 79, name = CREATEICON);
    OS2EXPORT ULONG OS2API CreateMetaFile(PSZ a0) OS2APIINFO(ord = 83, name = CREATEMETAFILE);
    OS2EXPORT ULONG OS2API CreatePolyPolygonRgn(PVOID a0, PVOID a1, LONG a2, LONG a3)
        OS2APIINFO(ord = 89, name = CREATEPOLYPOLYGONRGN);
    OS2EXPORT ULONG OS2API CreatePolygonRgn(PVOID a0, LONG a1, LONG a2)
        OS2APIINFO(ord = 90, name = CREATEPOLYGONRGN);
    OS2EXPORT ULONG OS2API CreateRoundRectRgn(LONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5)
        OS2APIINFO(ord = 95, name = CREATEROUNDRECTRGN);
    OS2EXPORT BOOL OS2API DeleteEnhMetaFile(ULONG a0)
        OS2APIINFO(ord = 136, name = DELETEENHMETAFILE);
    OS2EXPORT BOOL OS2API DrawFocusRect(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 159, name = DRAWFOCUSRECT);
    OS2EXPORT BOOL OS2API DrawIcon(ULONG a0, LONG a1, LONG a2, ULONG a3)
        OS2APIINFO(ord = 160, name = DRAWICON);
    OS2EXPORT BOOL OS2API Ellipse(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4)
        OS2APIINFO(ord = 164, name = ELLIPSE);
    OS2EXPORT BOOL OS2API EndPath(ULONG a0) OS2APIINFO(ord = 174, name = ENDPATH);
    OS2EXPORT BOOL OS2API EnumEnhMetaFile(ULONG a0, ULONG a1, ULONG a2, ULONG a3, PVOID a4)
        OS2APIINFO(ord = 178, name = ENUMENHMETAFILE);
    OS2EXPORT LONG OS2API EnumFonts(ULONG a0, PSZ a1, ULONG a2, ULONG a3)
        OS2APIINFO(ord = 180, name = ENUMFONTS);
    OS2EXPORT BOOL OS2API EqualRgn(ULONG a0, ULONG a1) OS2APIINFO(ord = 188, name = EQUALRGN);
    OS2EXPORT LONG OS2API Escape(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4)
        OS2APIINFO(ord = 189, name = ESCAPE);
    OS2EXPORT ULONG OS2API ExtCreatePen(ULONG a0, ULONG a1, PVOID a2, ULONG a3, PVOID a4)
        OS2APIINFO(ord = 196, name = EXTCREATEPEN);
    OS2EXPORT LONG OS2API ExtSelectClipRgn(ULONG a0, ULONG a1, LONG a2)
        OS2APIINFO(ord = 199, name = EXTSELECTCLIPRGN);
    OS2EXPORT BOOL OS2API FillRgn(ULONG a0, ULONG a1, ULONG a2)
        OS2APIINFO(ord = 208, name = FILLRGN);
    OS2EXPORT BOOL OS2API FrameRgn(ULONG a0, ULONG a1, ULONG a2, LONG a3, LONG a4)
        OS2APIINFO(ord = 221, name = FRAMERGN);
    OS2EXPORT LONG OS2API GetBitmapBits(ULONG a0, LONG a1, PVOID a2)
        OS2APIINFO(ord = 229, name = GETBITMAPBITS);
    OS2EXPORT ULONG OS2API GetBkColor(ULONG a0) OS2APIINFO(ord = 231, name = GETBKCOLOR);
    OS2EXPORT LONG OS2API GetBkMode(ULONG a0) OS2APIINFO(ord = 232, name = GETBKMODE);
    OS2EXPORT BOOL OS2API GetCharABCWidths(ULONG a0, ULONG a1, ULONG a2, PVOID a3)
        OS2APIINFO(ord = 238, name = GETCHARABCWIDTHS);
    OS2EXPORT BOOL OS2API GetCharWidth(ULONG a0, ULONG a1, ULONG a2, PVOID a3)
        OS2APIINFO(ord = 239, name = GETCHARWIDTH);
    OS2EXPORT LONG OS2API GetClipRgn(ULONG a0, ULONG a1) OS2APIINFO(ord = 247, name = GETCLIPRGN);
    OS2EXPORT ULONG OS2API GetCurrentObject(ULONG a0, ULONG a1)
        OS2APIINFO(ord = 254, name = GETCURRENTOBJECT);
    OS2EXPORT BOOL OS2API GetCurrentPositionEx(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 255, name = GETCURRENTPOSITIONEX);
    OS2EXPORT ULONG OS2API GetEnhMetaFile(PSZ a0) OS2APIINFO(ord = 278, name = GETENHMETAFILE);
    OS2EXPORT ULONG OS2API GetEnhMetaFileBits(ULONG a0, ULONG a1, PVOID a2)
        OS2APIINFO(ord = 279, name = GETENHMETAFILEBITS);
    OS2EXPORT ULONG OS2API GetEnhMetaFileHeader(ULONG a0, ULONG a1, PVOID a2)
        OS2APIINFO(ord = 280, name = GETENHMETAFILEHEADER);
    OS2EXPORT ULONG OS2API GetKerningPairs(ULONG a0, ULONG a1, PVOID a2)
        OS2APIINFO(ord = 295, name = GETKERNINGPAIRS);
    OS2EXPORT ULONG OS2API GetMetaFile(PSZ a0) OS2APIINFO(ord = 315, name = GETMETAFILE);
    OS2EXPORT ULONG OS2API GetOutlineTextMetrics(ULONG a0, ULONG a1, PVOID a2)
        OS2APIINFO(ord = 330, name = GETOUTLINETEXTMETRICS);
    OS2EXPORT LONG OS2API GetPolyFillMode(ULONG a0) OS2APIINFO(ord = 336, name = GETPOLYFILLMODE);
    OS2EXPORT ULONG OS2API GetSystemPaletteEntries(ULONG a0, ULONG a1, ULONG a2, PVOID a3)
        OS2APIINFO(ord = 360, name = GETSYSTEMPALETTEENTRIES);
    OS2EXPORT ULONG OS2API GetTextColor(ULONG a0) OS2APIINFO(ord = 367, name = GETTEXTCOLOR);
    OS2EXPORT BOOL OS2API GetViewportExtEx(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 378, name = GETVIEWPORTEXTEX);
    OS2EXPORT BOOL OS2API GetViewportOrgEx(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 379, name = GETVIEWPORTORGEX);
    OS2EXPORT ULONG OS2API GetWinMetaFileBits(ULONG a0, ULONG a1, PVOID a2, LONG a3, ULONG a4)
        OS2APIINFO(ord = 381, name = GETWINMETAFILEBITS);
    OS2EXPORT ULONG OS2API GetWindowDC(ULONG a0) OS2APIINFO(ord = 383, name = GETWINDOWDC);
    OS2EXPORT BOOL OS2API GetWindowExtEx(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 384, name = GETWINDOWEXTEX);
    OS2EXPORT BOOL OS2API GetWindowOrgEx(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 386, name = GETWINDOWORGEX);
    OS2EXPORT BOOL OS2API LineDDA(LONG a0, LONG a1, LONG a2, LONG a3, ULONG a4, ULONG a5)
        OS2APIINFO(ord = 455, name = LINEDDA);
    OS2EXPORT LONG OS2API OffsetRgn(ULONG a0, LONG a1, LONG a2)
        OS2APIINFO(ord = 499, name = OFFSETRGN);
    OS2EXPORT BOOL OS2API OffsetViewportOrgEx(ULONG a0, LONG a1, LONG a2, PVOID a3)
        OS2APIINFO(ord = 500, name = OFFSETVIEWPORTORGEX);
    OS2EXPORT BOOL OS2API OffsetWindowOrgEx(ULONG a0, LONG a1, LONG a2, PVOID a3)
        OS2APIINFO(ord = 501, name = OFFSETWINDOWORGEX);
    OS2EXPORT BOOL OS2API PaintRgn(ULONG a0, ULONG a1) OS2APIINFO(ord = 510, name = PAINTRGN);
    OS2EXPORT BOOL OS2API Pie(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5, LONG a6, LONG a7, LONG a8)
        OS2APIINFO(ord = 514, name = PIE);
    OS2EXPORT BOOL OS2API PlayEnhMetaFile(ULONG a0, ULONG a1, PVOID a2)
        OS2APIINFO(ord = 515, name = PLAYENHMETAFILE);
    OS2EXPORT BOOL OS2API PlayEnhMetaFileRecord(ULONG a0, PVOID a1, PVOID a2, ULONG a3)
        OS2APIINFO(ord = 516, name = PLAYENHMETAFILERECORD);
    OS2EXPORT BOOL OS2API PolyPolygon(ULONG a0, PVOID a1, PVOID a2, LONG a3)
        OS2APIINFO(ord = 522, name = POLYPOLYGON);
    OS2EXPORT BOOL OS2API PolyPolyline(ULONG a0, PVOID a1, PVOID a2, ULONG a3)
        OS2APIINFO(ord = 523, name = POLYPOLYLINE);
    OS2EXPORT BOOL OS2API PtInRegion(ULONG a0, LONG a1, LONG a2)
        OS2APIINFO(ord = 532, name = PTINREGION);
    OS2EXPORT BOOL OS2API RectInRegion(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 537, name = RECTINREGION);
    OS2EXPORT BOOL OS2API RectVisible(ULONG a0, PVOID a1) OS2APIINFO(ord = 538, name = RECTVISIBLE);
    OS2EXPORT BOOL OS2API ResetDC(ULONG a0, PVOID a1) OS2APIINFO(ord = 569, name = RESETDC);
    OS2EXPORT BOOL OS2API ResizePalette(ULONG a0, ULONG a1)
        OS2APIINFO(ord = 571, name = RESIZEPALETTE);
    OS2EXPORT BOOL OS2API RoundRect(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5, LONG a6)
        OS2APIINFO(ord = 575, name = ROUNDRECT);
    OS2EXPORT BOOL OS2API ScaleViewportExtEx(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, PVOID a5)
        OS2APIINFO(ord = 577, name = SCALEVIEWPORTEXTEX);
    OS2EXPORT BOOL OS2API ScaleWindowExtEx(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, PVOID a5)
        OS2APIINFO(ord = 578, name = SCALEWINDOWEXTEX);
    OS2EXPORT BOOL OS2API ScrollDC(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4, ULONG a5, PVOID a6)
        OS2APIINFO(ord = 580, name = SCROLLDC);
    OS2EXPORT BOOL OS2API ScrollWindow(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4)
        OS2APIINFO(ord = 581, name = SCROLLWINDOW);
    OS2EXPORT LONG OS2API ScrollWindowEx(ULONG a0, LONG a1, LONG a2, PVOID a3, PVOID a4, ULONG a5, PVOID a6, ULONG a7)
        OS2APIINFO(ord = 582, name = SCROLLWINDOWEX);
    OS2EXPORT LONG OS2API SetBitmapBits(ULONG a0, ULONG a1, PVOID a2)
        OS2APIINFO(ord = 591, name = SETBITMAPBITS);
    OS2EXPORT LONG OS2API SetDIBitsToDevice(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, LONG a5, LONG a6, ULONG a7, ULONG a8, PVOID a9, PVOID a10, ULONG a11)
        OS2APIINFO(ord = 609, name = SETDIBITSTODEVICE);
    OS2EXPORT ULONG OS2API SetEnhMetaFileBits(ULONG a0, PVOID a1)
        OS2APIINFO(ord = 614, name = SETENHMETAFILEBITS);
    OS2EXPORT ULONG OS2API SetPaletteEntries(ULONG a0, ULONG a1, ULONG a2, PVOID a3)
        OS2APIINFO(ord = 632, name = SETPALETTEENTRIES);
    OS2EXPORT ULONG OS2API SetPixel(ULONG a0, LONG a1, LONG a2, ULONG a3)
        OS2APIINFO(ord = 634, name = SETPIXEL);
    OS2EXPORT LONG OS2API SetPolyFillMode(ULONG a0, LONG a1)
        OS2APIINFO(ord = 635, name = SETPOLYFILLMODE);
    OS2EXPORT LONG OS2API SetStretchBltMode(ULONG a0, LONG a1)
        OS2APIINFO(ord = 644, name = SETSTRETCHBLTMODE);
    OS2EXPORT LONG OS2API SetTextCharacterExtra(ULONG a0, LONG a1)
        OS2APIINFO(ord = 648, name = SETTEXTCHARACTEREXTRA);
    OS2EXPORT BOOL OS2API SetTextJustification(ULONG a0, LONG a1, LONG a2)
        OS2APIINFO(ord = 650, name = SETTEXTJUSTIFICATION);
    OS2EXPORT ULONG OS2API SetWinMetaFileBits(ULONG a0, PVOID a1, ULONG a2, PVOID a3)
        OS2APIINFO(ord = 657, name = SETWINMETAFILEBITS);
    OS2EXPORT BOOL OS2API StretchBlt(ULONG a0, LONG a1, LONG a2, LONG a3, LONG a4, ULONG a5, LONG a6, LONG a7, LONG a8, LONG a9, ULONG a10)
        OS2APIINFO(ord = 676, name = STRETCHBLT);
    OS2EXPORT BOOL OS2API StrokePath(ULONG a0) OS2APIINFO(ord = 679, name = STROKEPATH);
    OS2EXPORT LONG OS2API TabbedTextOut(ULONG a0, LONG a1, LONG a2, PCHAR a3, LONG a4, LONG a5, PVOID a6, LONG a7)
        OS2APIINFO(ord = 686, name = TABBEDTEXTOUT);
    OS2EXPORT BOOL OS2API UnrealizeObject(ULONG a0) OS2APIINFO(ord = 704, name = UNREALIZEOBJECT);
    OS2EXPORT ULONG OS2API WindowFromDC(ULONG a0) OS2APIINFO(ord = 1000, name = WINDOWFROMDC);
    OS2EXPORT ULONG OS2API DeviceCapabilities(PSZ a0, PSZ a1, ULONG a2, PVOID a3, PVOID a4)
        OS2APIINFO(ord = 1004, name = DEVICECAPABILITIES);
    OS2EXPORT ULONG OS2API HPSToHDC(ULONG a0, ULONG a1, PSZ a2, PVOID a3)
        OS2APIINFO(ord = 1022, name = HPSTOHDC);
    OS2EXPORT VOID OS2API DeleteHDCObjects(ULONG a0)
        OS2APIINFO(ord = 1023, name = DELETEHDCOBJECTS);
    OS2EXPORT LONG OS2API SetAbortProc(ULONG a0, ULONG a1)
        OS2APIINFO(ord = 1031, name = SETABORTPROC);
    OS2EXPORT ULONG OS2API _CreateBitmapIndirect(PVOID a0)
        OS2APIINFO(ord = 5000, name = _CREATEBITMAPINDIRECT);
    OS2EXPORT BOOL OS2API WinTranslateDeviceRects(ULONG a0, ULONG a1, PVOID a2, LONG a3, ULONG a4)
        OS2APIINFO(ord = 6004, name = WINTRANSLATEDEVICERECTS);
    OS2EXPORT ULONG OS2API WinTranslateGraphicsObjectHandle(ULONG a0, ULONG a1, ULONG a2)
        OS2APIINFO(ord = 6005, name = WINTRANSLATEGRAPHICSOBJECTHANDLE);
    OS2EXPORT ULONG OS2API AddAtom(PSZ name) OS2APIINFO(ord = 3, name = ADDATOM);
    OS2EXPORT BOOL OS2API AdjustWindowRect(PVOID rectangle, ULONG style, BOOL menu)
        OS2APIINFO(ord = 5, name = ADJUSTWINDOWRECT);
    OS2EXPORT BOOL OS2API Beep(ULONG frequency, ULONG duration) OS2APIINFO(ord = 14, name = BEEP);
    OS2EXPORT ULONG OS2API BeginDeferWindowPos(LONG count)
        OS2APIINFO(ord = 15, name = BEGINDEFERWINDOWPOS);
    OS2EXPORT BOOL OS2API CheckRadioButton(ULONG dialog, LONG first, LONG last, LONG checked)
        OS2APIINFO(ord = 34, name = CHECKRADIOBUTTON);
    OS2EXPORT ULONG OS2API ChildWindowFromPoint(ULONG parent, LONG x, LONG y)
        OS2APIINFO(ord = 35, name = CHILDWINDOWFROMPOINT);
    OS2EXPORT BOOL OS2API ChooseColor(PVOID record) OS2APIINFO(ord = 36, name = CHOOSECOLOR);
    OS2EXPORT BOOL OS2API ChooseFont(PVOID record) OS2APIINFO(ord = 37, name = CHOOSEFONT);
    OS2EXPORT BOOL OS2API ClipCursor(PVOID rectangle) OS2APIINFO(ord = 40, name = CLIPCURSOR);
    OS2EXPORT LONG OS2API CompareFileTime(PVOID first, PVOID second)
        OS2APIINFO(ord = 49, name = COMPAREFILETIME);
    OS2EXPORT BOOL OS2API CopyFile(PSZ source, PSZ destination, BOOL exclusive)
        OS2APIINFO(ord = 52, name = COPYFILE);
    OS2EXPORT ULONG OS2API CreateDialog(ULONG module, PSZ resource, ULONG owner, ULONG procedure)
        OS2APIINFO(ord = 67, name = CREATEDIALOG);
    OS2EXPORT ULONG OS2API CreateDialogParam(ULONG module, PSZ resource, ULONG owner, ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 68, name = CREATEDIALOGPARAM);
    OS2EXPORT BOOL OS2API CreateDirectory(PSZ path, PVOID security)
        OS2APIINFO(ord = 69, name = CREATEDIRECTORY);
    OS2EXPORT ULONG OS2API CreateEvent(PVOID security, BOOL manual, BOOL initial, PSZ name)
        OS2APIINFO(ord = 73, name = CREATEEVENT);
    OS2EXPORT ULONG OS2API CreateMutex(PVOID security, BOOL owner, PSZ name)
        OS2APIINFO(ord = 84, name = CREATEMUTEX);
    OS2EXPORT BOOL OS2API DdeAbandonTransaction(ULONG id, ULONG handle, ULONG transaction)
        OS2APIINFO(ord = 101, name = DDEABANDONTRANSACTION);
    OS2EXPORT ULONG OS2API DdeAccessData(ULONG handle, PVOID size)
        OS2APIINFO(ord = 102, name = DDEACCESSDATA);
    OS2EXPORT ULONG OS2API DdeAddData(ULONG handle, PVOID input, ULONG count, ULONG offset)
        OS2APIINFO(ord = 103, name = DDEADDDATA);
    OS2EXPORT ULONG OS2API DdeClientTransaction(ULONG address, ULONG size, ULONG handle, ULONG item, ULONG format, ULONG type, ULONG timeout, PVOID status)
        OS2APIINFO(ord = 104, name = DDECLIENTTRANSACTION);
    OS2EXPORT ULONG OS2API DdeConnect(ULONG id, ULONG service, ULONG topic, PVOID context)
        OS2APIINFO(ord = 106, name = DDECONNECT);
    OS2EXPORT ULONG OS2API DdeCreateDataHandle(ULONG id, PVOID input, ULONG count, ULONG offset, ULONG item, ULONG format, ULONG flags)
        OS2APIINFO(ord = 108, name = DDECREATEDATAHANDLE);
    OS2EXPORT BOOL OS2API DdeDisconnect(ULONG handle) OS2APIINFO(ord = 110, name = DDEDISCONNECT);
    OS2EXPORT BOOL OS2API DdeFreeDataHandle(ULONG handle)
        OS2APIINFO(ord = 113, name = DDEFREEDATAHANDLE);
    OS2EXPORT ULONG OS2API DdeGetData(ULONG handle, PVOID output, ULONG capacity, ULONG offset)
        OS2APIINFO(ord = 115, name = DDEGETDATA);
    OS2EXPORT BOOL OS2API DdePostAdvise(ULONG id, ULONG topic, ULONG item)
        OS2APIINFO(ord = 120, name = DDEPOSTADVISE);
    OS2EXPORT ULONG OS2API DdeQueryConvInfo(ULONG handle, ULONG transaction, PVOID output)
        OS2APIINFO(ord = 121, name = DDEQUERYCONVINFO);
    OS2EXPORT BOOL OS2API DdeSetUserHandle(ULONG handle, ULONG transaction, ULONG user)
        OS2APIINFO(ord = 125, name = DDESETUSERHANDLE);
    OS2EXPORT BOOL OS2API DdeUnaccessData(ULONG handle)
        OS2APIINFO(ord = 126, name = DDEUNACCESSDATA);
    OS2EXPORT LONG OS2API DefDlgProc(ULONG window, ULONG message, ULONG first, ULONG second)
        OS2APIINFO(ord = 128, name = DEFDLGPROC);
    OS2EXPORT ULONG OS2API DeferWindowPos(ULONG handle, ULONG window, ULONG after, LONG x, LONG y, LONG width, LONG height, ULONG flags)
        OS2APIINFO(ord = 132, name = DEFERWINDOWPOS);
    OS2EXPORT ULONG OS2API DeleteAtom(ULONG atom) OS2APIINFO(ord = 133, name = DELETEATOM);
    OS2EXPORT BOOL OS2API DeleteFile(PSZ path) OS2APIINFO(ord = 137, name = DELETEFILE);
    OS2EXPORT BOOL OS2API DlgDirSelectEx(ULONG dialog, PSZ output, LONG capacity, LONG id)
        OS2APIINFO(ord = 153, name = DLGDIRSELECTEX);
    OS2EXPORT BOOL OS2API DosDateTimeToFileTime(USHORT date, USHORT time, PVOID output)
        OS2APIINFO(ord = 154, name = DOSDATETIMETOFILETIME);
    OS2EXPORT VOID OS2API DragAcceptFiles(ULONG window, BOOL accept)
        OS2APIINFO(ord = 155, name = DRAGACCEPTFILES);
    OS2EXPORT VOID OS2API DragFinish(ULONG drop) OS2APIINFO(ord = 156, name = DRAGFINISH);
    OS2EXPORT ULONG OS2API DragQueryFile(ULONG drop, ULONG index, PSZ output, ULONG capacity)
        OS2APIINFO(ord = 157, name = DRAGQUERYFILE);
    OS2EXPORT BOOL OS2API DragQueryPoint(ULONG drop, PVOID output)
        OS2APIINFO(ord = 158, name = DRAGQUERYPOINT);
    OS2EXPORT BOOL OS2API EndDeferWindowPos(ULONG handle)
        OS2APIINFO(ord = 169, name = ENDDEFERWINDOWPOS);
    OS2EXPORT BOOL OS2API EnumThreadWindows(ULONG thread, ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 185, name = ENUMTHREADWINDOWS);
    OS2EXPORT BOOL OS2API EnumWindows(ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 186, name = ENUMWINDOWS);
    OS2EXPORT BOOL OS2API FileTimeToDosDateTime(PVOID input, PVOID date, PVOID time)
        OS2APIINFO(ord = 203, name = FILETIMETODOSDATETIME);
    OS2EXPORT ULONG OS2API FindAtom(PSZ name) OS2APIINFO(ord = 209, name = FINDATOM);
    OS2EXPORT ULONG OS2API FindWindow(PSZ class_name, PSZ title)
        OS2APIINFO(ord = 215, name = FINDWINDOW);
    OS2EXPORT ULONG OS2API GetCursor(void) OS2APIINFO(ord = 261, name = GETCURSOR);
    OS2EXPORT BOOL OS2API GetDiskFreeSpace(PSZ root, PVOID sectors, PVOID bytes, PVOID free_clusters, PVOID total_clusters)
        OS2APIINFO(ord = 271, name = GETDISKFREESPACE);
    OS2EXPORT LONG OS2API GetDlgCtrlID(ULONG window) OS2APIINFO(ord = 272, name = GETDLGCTRLID);
    OS2EXPORT ULONG OS2API GetDlgItemInt(ULONG dialog, LONG id, PVOID translated, BOOL sign)
        OS2APIINFO(ord = 274, name = GETDLGITEMINT);
    OS2EXPORT ULONG OS2API GetDoubleClickTime(void)
        OS2APIINFO(ord = 276, name = GETDOUBLECLICKTIME);
    OS2EXPORT ULONG OS2API GetEnvironmentStrings(void)
        OS2APIINFO(ord = 282, name = GETENVIRONMENTSTRINGS);
    OS2EXPORT ULONG OS2API GetEnvironmentVariable(PSZ name, PSZ output, ULONG capacity)
        OS2APIINFO(ord = 283, name = GETENVIRONMENTVARIABLE);
    OS2EXPORT BOOL OS2API GetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write)
        OS2APIINFO(ord = 287, name = GETFILETIME);
    OS2EXPORT SHORT OS2API GetFileTitle(PSZ path, PSZ output, USHORT capacity)
        OS2APIINFO(ord = 288, name = GETFILETITLE);
    OS2EXPORT ULONG OS2API GetForegroundWindow(void)
        OS2APIINFO(ord = 291, name = GETFOREGROUNDWINDOW);
    OS2EXPORT LONG OS2API GetKeyNameText(ULONG parameter, PSZ output, LONG capacity)
        OS2APIINFO(ord = 296, name = GETKEYNAMETEXT);
    OS2EXPORT ULONG OS2API GetLastActivePopup(ULONG window)
        OS2APIINFO(ord = 299, name = GETLASTACTIVEPOPUP);
    OS2EXPORT ULONG OS2API GetNextDlgTabItem(ULONG dialog, ULONG control, BOOL previous)
        OS2APIINFO(ord = 323, name = GETNEXTDLGTABITEM);
    OS2EXPORT ULONG OS2API GetNextWindow(ULONG window, ULONG direction)
        OS2APIINFO(ord = 324, name = GETNEXTWINDOW);
    OS2EXPORT LONG OS2API GetPriorityClipboardFormat(PVOID formats, LONG count)
        OS2APIINFO(ord = 338, name = GETPRIORITYCLIPBOARDFORMAT);
    OS2EXPORT ULONG OS2API GetPrivateProfileInt(PSZ section, PSZ key, LONG fallback, PSZ filename)
        OS2APIINFO(ord = 339, name = GETPRIVATEPROFILEINT);
    OS2EXPORT ULONG OS2API GetSystemDirectory(PSZ output, ULONG capacity)
        OS2APIINFO(ord = 357, name = GETSYSTEMDIRECTORY);
    OS2EXPORT ULONG OS2API GetTempFileName(PSZ path, PSZ prefix, ULONG unique, PSZ output)
        OS2APIINFO(ord = 363, name = GETTEMPFILENAME);
    OS2EXPORT ULONG OS2API GetTempPath(ULONG capacity, PSZ output)
        OS2APIINFO(ord = 364, name = GETTEMPPATH);
    OS2EXPORT ULONG OS2API GetTimeZoneInformation(PVOID output)
        OS2APIINFO(ord = 374, name = GETTIMEZONEINFORMATION);
    OS2EXPORT ULONG OS2API GetTopWindow(ULONG parent) OS2APIINFO(ord = 375, name = GETTOPWINDOW);
    OS2EXPORT LONG OS2API GetUpdateRgn(ULONG window, ULONG output, BOOL erase)
        OS2APIINFO(ord = 377, name = GETUPDATERGN);
    OS2EXPORT USHORT OS2API GetWindowWord(ULONG handle, LONG index)
        OS2APIINFO(ord = 392, name = GETWINDOWWORD);
    OS2EXPORT VOID OS2API GlobalMemoryStatus(PVOID output)
        OS2APIINFO(ord = 405, name = GLOBALMEMORYSTATUS);
    OS2EXPORT BOOL OS2API IsBadHugeReadPtr(PVOID pointer, ULONG size)
        OS2APIINFO(ord = 431, name = ISBADHUGEREADPTR);
    OS2EXPORT BOOL OS2API IsBadHugeWritePtr(PVOID pointer, ULONG size)
        OS2APIINFO(ord = 432, name = ISBADHUGEWRITEPTR);
    OS2EXPORT BOOL OS2API IsBadStringPtr(ULONG address, ULONG maximum)
        OS2APIINFO(ord = 434, name = ISBADSTRINGPTR);
    OS2EXPORT BOOL OS2API IsCharAlpha(ULONG character) OS2APIINFO(ord = 436, name = ISCHARALPHA);
    OS2EXPORT BOOL OS2API IsCharAlphaNumeric(ULONG character)
        OS2APIINFO(ord = 437, name = ISCHARALPHANUMERIC);
    OS2EXPORT BOOL OS2API IsCharUpper(ULONG character) OS2APIINFO(ord = 439, name = ISCHARUPPER);
    OS2EXPORT BOOL OS2API IsChild(ULONG parent, ULONG child) OS2APIINFO(ord = 440, name = ISCHILD);
    OS2EXPORT BOOL OS2API IsDialogMessage(ULONG dialog, PVOID message)
        OS2APIINFO(ord = 443, name = ISDIALOGMESSAGE);
    OS2EXPORT BOOL OS2API LockWindowUpdate(ULONG window)
        OS2APIINFO(ord = 477, name = LOCKWINDOWUPDATE);
    OS2EXPORT BOOL OS2API MapDialogRect(ULONG dialog, PVOID rectangle)
        OS2APIINFO(ord = 483, name = MAPDIALOGRECT);
    OS2EXPORT LONG OS2API MapWindowPoints(ULONG from, ULONG to, PVOID points, ULONG count)
        OS2APIINFO(ord = 484, name = MAPWINDOWPOINTS);
    OS2EXPORT LONG OS2API MessageBox(ULONG owner, PSZ text, PSZ caption, ULONG flags)
        OS2APIINFO(ord = 487, name = MESSAGEBOX);
    OS2EXPORT BOOL OS2API ModifyMenu(ULONG handle, ULONG identity, ULONG flags, ULONG identifier, PSZ text)
        OS2APIINFO(ord = 488, name = MODIFYMENU);
    OS2EXPORT BOOL OS2API MoveFile(PSZ source, PSZ destination)
        OS2APIINFO(ord = 490, name = MOVEFILE);
    OS2EXPORT VOID OS2API OutputDebugString(PSZ text)
        OS2APIINFO(ord = 508, name = OUTPUTDEBUGSTRING);
    OS2EXPORT BOOL OS2API PostThreadMessage(ULONG thread, ULONG message, ULONG first, ULONG second)
        OS2APIINFO(ord = 529, name = POSTTHREADMESSAGE);
    OS2EXPORT LONG OS2API RegQueryInfoKey(ULONG key, PSZ class_name, PULONG class_size, PULONG reserved, PULONG subkeys, PULONG max_subkey, PULONG max_class, PULONG values, PULONG max_value_name, PULONG max_value_data, PULONG security, PVOID last_write)
        OS2APIINFO(ord = 551, name = REGQUERYINFOKEY);
    OS2EXPORT LONG OS2API RegQueryValue(ULONG key, PSZ subkey, PSZ output, PULONG size)
        OS2APIINFO(ord = 552, name = REGQUERYVALUE);
    OS2EXPORT LONG OS2API RegSetValue(ULONG key, PSZ subkey, ULONG type, PVOID data, ULONG size)
        OS2APIINFO(ord = 554, name = REGSETVALUE);
    OS2EXPORT BOOL OS2API ReleaseMutex(ULONG handle) OS2APIINFO(ord = 561, name = RELEASEMUTEX);
    OS2EXPORT BOOL OS2API RemoveDirectory(PSZ path) OS2APIINFO(ord = 563, name = REMOVEDIRECTORY);
    OS2EXPORT BOOL OS2API AddFontResource(PSZ path) OS2APIINFO(ord = 4, name = ADDFONTRESOURCE);
    OS2EXPORT BOOL OS2API RemoveFontResource(PSZ path)
        OS2APIINFO(ord = 564, name = REMOVEFONTRESOURCE);
    OS2EXPORT BOOL OS2API ReplyMessage(LONG result) OS2APIINFO(ord = 568, name = REPLYMESSAGE);
    OS2EXPORT BOOL OS2API ResetEvent(ULONG handle) OS2APIINFO(ord = 570, name = RESETEVENT);
    OS2EXPORT ULONG OS2API SearchPath(PSZ path, PSZ file, PSZ extension, ULONG capacity, PSZ output, PVOID part)
        OS2APIINFO(ord = 583, name = SEARCHPATH);
    OS2EXPORT BOOL OS2API SetDlgItemInt(ULONG dialog, LONG id, ULONG value, BOOL sign)
        OS2APIINFO(ord = 610, name = SETDLGITEMINT);
    OS2EXPORT BOOL OS2API SetEnvironmentVariable(PSZ name, PSZ value)
        OS2APIINFO(ord = 615, name = SETENVIRONMENTVARIABLE);
    OS2EXPORT BOOL OS2API SetEvent(ULONG handle) OS2APIINFO(ord = 616, name = SETEVENT);
    OS2EXPORT BOOL OS2API SetFileAttributes(PSZ path, ULONG attributes)
        OS2APIINFO(ord = 617, name = SETFILEATTRIBUTES);
    OS2EXPORT BOOL OS2API SetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write)
        OS2APIINFO(ord = 619, name = SETFILETIME);
    OS2EXPORT ULONG OS2API SetHandleCount(ULONG count) OS2APIINFO(ord = 623, name = SETHANDLECOUNT);
    OS2EXPORT USHORT OS2API SetWindowWord(ULONG handle, LONG index, USHORT value)
        OS2APIINFO(ord = 664, name = SETWINDOWWORD);
    OS2EXPORT LONG OS2API ShowCursor(BOOL show) OS2APIINFO(ord = 668, name = SHOWCURSOR);
    OS2EXPORT BOOL OS2API TrackPopupMenu(ULONG handle, ULONG flags, LONG x, LONG y, LONG reserved, ULONG owner, PVOID exclude)
        OS2APIINFO(ord = 696, name = TRACKPOPUPMENU);
    OS2EXPORT BOOL OS2API TranslateMDISysAccel(ULONG client, PVOID message)
        OS2APIINFO(ord = 698, name = TRANSLATEMDISYSACCEL);
    OS2EXPORT SHORT OS2API VkKeyScan(ULONG character) OS2APIINFO(ord = 709, name = VKKEYSCAN);
    OS2EXPORT BOOL OS2API WriteProfileString(PSZ section, PSZ key, PSZ value)
        OS2APIINFO(ord = 719, name = WRITEPROFILESTRING);
    OS2EXPORT VOID OS2API ZeroMemory(PVOID output, ULONG count)
        OS2APIINFO(ord = 722, name = ZEROMEMORY);
    OS2EXPORT LONG OS2API _lcreat(PSZ path, LONG attributes) OS2APIINFO(ord = 724, name = _LCREAT);
    OS2EXPORT LONG OS2API _lopen(PSZ path, LONG flags) OS2APIINFO(ord = 726, name = _LOPEN);
    OS2EXPORT ULONG OS2API MapVirtualKey(ULONG code, ULONG kind)
        OS2APIINFO(ord = 1007, name = MAPVIRTUALKEY);
    OS2EXPORT ULONG OS2API GetMenuContextHelpId(ULONG handle)
        OS2APIINFO(ord = 1016, name = GETMENUCONTEXTHELPID);
    OS2EXPORT BOOL OS2API SetMenuContextHelpId(ULONG handle, ULONG help)
        OS2APIINFO(ord = 1017, name = SETMENUCONTEXTHELPID);
    OS2EXPORT ULONG OS2API ExtractIcon(ULONG instance, PSZ path, ULONG index)
        OS2APIINFO(ord = 1025, name = EXTRACTICON);
    OS2EXPORT LONG OS2API lstrcmp2(PSZ first, PSZ second) OS2APIINFO(ord = 1033, name = LSTRCMP2);
    OS2EXPORT ULONG OS2API GetKeyboardLayout(ULONG thread)
        OS2APIINFO(ord = 2053, name = GETKEYBOARDLAYOUT);
    OS2EXPORT BOOL OS2API GetStringTypeEx(ULONG locale, ULONG kind, PSZ input, LONG count, PVOID output)
        OS2APIINFO(ord = 2057, name = GETSTRINGTYPEEX);
    OS2EXPORT ULONG OS2API _CreateDialogIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 5002, name = _CREATEDIALOGINDIRECTPARAM);
    OS2EXPORT LONG OS2API _DialogBoxIndirect(ULONG module, ULONG address, ULONG owner, ULONG procedure)
        OS2APIINFO(ord = 5004, name = _DIALOGBOXINDIRECT);
    OS2EXPORT LONG OS2API _DialogBoxIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure, ULONG parameter)
        OS2APIINFO(ord = 5005, name = _DIALOGBOXINDIRECTPARAM);
    OS2EXPORT BOOL OS2API WinXTranslateMsg(ULONG anchor, PVOID input, PVOID output)
        OS2APIINFO(ord = 6011, name = WINXTRANSLATEMSG);
    OS2EXPORT BOOL OS2API CreateProcess(PSZ application, PSZ command, PVOID process_security,
                                        PVOID thread_security, BOOL inherit, ULONG flags,
                                        PVOID environment, PSZ directory, PVOID startup,
                                        PVOID information) OS2APIINFO(ord = 92, name = CREATEPROCESS);
    OS2EXPORT BOOL OS2API DuplicateHandle(ULONG source_process, ULONG source, ULONG target_process,
                                          PULONG output, ULONG access, BOOL inherit, ULONG options)
        OS2APIINFO(ord = 163, name = DUPLICATEHANDLE);
    OS2EXPORT VOID OS2API ExitProcess(ULONG code) OS2APIINFO(ord = 192, name = EXITPROCESS);
    OS2EXPORT BOOL OS2API ExitWindows(ULONG reserved, ULONG code)
        OS2APIINFO(ord = 194, name = EXITWINDOWS);
    OS2EXPORT VOID OS2API FatalAppExit(ULONG action, PSZ message)
        OS2APIINFO(ord = 201, name = FATALAPPEXIT);
    OS2EXPORT VOID OS2API FatalExit(ULONG code) OS2APIINFO(ord = 202, name = FATALEXIT);
    OS2EXPORT ULONG OS2API OpenProcess(ULONG access, BOOL inherit, ULONG id)
        OS2APIINFO(ord = 506, name = OPENPROCESS);
    OS2EXPORT ULONG OS2API SuspendThread(ULONG handle) OS2APIINFO(ord = 681, name = SUSPENDTHREAD);
    OS2EXPORT BOOL OS2API TerminateThread(ULONG handle, ULONG code)
        OS2APIINFO(ord = 688, name = TERMINATETHREAD);
    OS2EXPORT ULONG OS2API WinExec(PSZ command, ULONG show) OS2APIINFO(ord = 714, name = WINEXEC);
    OS2EXPORT BOOL OS2API GetExitCodeProcess(ULONG handle, PULONG code)
        OS2APIINFO(ord = 1005, name = GETEXITCODEPROCESS);
    OS2EXPORT ULONG OS2API ShellExecute(ULONG window, PSZ operation, PSZ file, PSZ parameters,
                                        PSZ directory, LONG show)
        OS2APIINFO(ord = 1024, name = SHELLEXECUTE);
    OS2EXPORT ULONG OS2API WaitForInputIdle(ULONG handle, ULONG timeout)
        OS2APIINFO(ord = 1028, name = WAITFORINPUTIDLE);
#ifdef __cplusplus
}
#endif
#endif
