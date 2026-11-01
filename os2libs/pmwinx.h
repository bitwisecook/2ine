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
    OS2EXPORT VOID OS2API WinInitializeOpen32(ULONG reserved)
        OS2APIINFO(ord = 6009, name = WININITIALIZEOPEN32);
    OS2EXPORT VOID OS2API WinTerminateOpen32(ULONG reserved)
        OS2APIINFO(ord = 6010, name = WINTERMINATEOPEN32);
    OS2EXPORT BOOL OS2API WinXProcessMsg(PVOID message)
        OS2APIINFO(ord = 6007, name = WINXPROCESSMSG);
    OS2EXPORT ULONG OS2API GetLastError(void) OS2APIINFO(ord = 300, name = GETLASTERROR);
    OS2EXPORT VOID OS2API SetLastError(ULONG error) OS2APIINFO(ord = 624, name = SETLASTERROR);
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
