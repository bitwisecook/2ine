/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared Windows system cursors use the corresponding PM host cursor. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin.h"
static _Thread_local ULONG current_cursor;
ULONG LoadCursor(HMODULE module, PSZ name)
{
    TRACE_NATIVE("LoadCursor(%u, %p)", module, name);
    if (!open32Presentation())
    {
        open32SetError(8);
        return 0;
    }
    LONG id = 0;
    if (!module)
        switch ((uintptr_t)name)
        {
            case 32512:
                id = 1;
                break; /* IDC_ARROW */
            case 32513:
                id = 2;
                break; /* IDC_IBEAM */
            case 32514:
                id = 3;
                break; /* IDC_WAIT */
            case 32642:
                id = 6;
                break; /* IDC_SIZENWSE */
            case 32643:
                id = 7;
                break; /* IDC_SIZENESW */
            case 32644:
                id = 8;
                break; /* IDC_SIZEWE */
            case 32645:
                id = 9;
                break; /* IDC_SIZENS */
            case 32646:
                id = 5;
                break; /* IDC_SIZEALL */
            case 32648:
                id = 18;
                break; /* IDC_NO */
        }
    ULONG result = id ? WinQuerySysPointer(HWND_DESKTOP, id, FALSE) : 0;
    if (!result)
        open32SetError(1814);
    return result;
}
ULONG SetCursor(ULONG cursor)
{
    if (!WinSetPointer(HWND_DESKTOP, cursor))
    {
        open32SetError(6);
        return 0;
    }
    ULONG previous = current_cursor;
    current_cursor = cursor;
    return previous;
}
