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
    OS2EXPORT ULONG OS2API CreateThread(PVOID security, ULONG stack, ULONG procedure,
                                        ULONG parameter, ULONG flags, PULONG id)
        OS2APIINFO(ord = 98, name = CREATETHREAD);
    OS2EXPORT BOOL OS2API GetExitCodeThread(ULONG handle, PULONG result)
        OS2APIINFO(ord = 1006, name = GETEXITCODETHREAD);
    OS2EXPORT ULONG OS2API ResumeThread(ULONG handle) OS2APIINFO(ord = 573, name = RESUMETHREAD);
    OS2EXPORT ULONG OS2API WaitForSingleObject(ULONG handle, ULONG timeout)
        OS2APIINFO(ord = 711, name = WAITFORSINGLEOBJECT);
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
    OS2EXPORT BOOL OS2API IsIconic(ULONG window) OS2APIINFO(ord = 445, name = ISICONIC);
    OS2EXPORT BOOL OS2API GetWindowPlacement(ULONG window, PVOID output)
        OS2APIINFO(ord = 387, name = GETWINDOWPLACEMENT);
    OS2EXPORT ULONG OS2API GetParent(ULONG window) OS2APIINFO(ord = 333, name = GETPARENT);
    OS2EXPORT BOOL OS2API RedrawWindow(ULONG window, PVOID rectangle, ULONG region, ULONG flags)
        OS2APIINFO(ord = 540, name = REDRAWWINDOW);
    OS2EXPORT LONG OS2API GetMenuString(ULONG menu, ULONG item, PCHAR output, LONG capacity,
                                        ULONG flags) OS2APIINFO(ord = 310, name = GETMENUSTRING);
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
    OS2EXPORT LONG OS2API GetMapMode(ULONG dc) OS2APIINFO(ord = 304, name = GETMAPMODE);
    OS2EXPORT LONG OS2API IntersectClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 424, name = INTERSECTCLIPRECT);
    OS2EXPORT LONG OS2API ExcludeClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom)
        OS2APIINFO(ord = 190, name = EXCLUDECLIPRECT);
    OS2EXPORT BOOL OS2API GetTextExtentPoint32(ULONG dc, PCHAR text, LONG count, PVOID size)
        OS2APIINFO(ord = 369, name = GETTEXTEXTENTPOINT32);
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
    OS2EXPORT LONG OS2API MulDiv(LONG number, LONG numerator, LONG denominator)
        OS2APIINFO(ord = 494, name = MULDIV);
    OS2EXPORT LONG OS2API RegCloseKey(ULONG key) OS2APIINFO(ord = 541, name = REGCLOSEKEY);
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
#ifdef __cplusplus
}
#endif
#endif
