/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared Windows system cursors use the corresponding PM host cursor. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmwin.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include "emulator/handles.h"
static _Thread_local ULONG current_cursor;

ULONG LoadCursor(HMODULE module, PSZ name)
{
    TRACE_NATIVE("LoadCursor(%u, %p)", module, name);
    if (module)
    {
        if (!open32Presentation())
        {
            open32SetError(8);
            return 0;
        }
        return (uintptr_t)name <= 0xffff
                   ? WinLoadPointer(HWND_DESKTOP, module, (ULONG)(uintptr_t)name)
                   : 0;
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
            case 32650:
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
    ULONG result = id ? lx_pm_system_pointer(id) : 0;
    if (!result)
        open32SetError(1814);
    return result;
}

ULONG SetCursor(ULONG cursor)
{
    LxHandleBorrow borrow = {0};
    if (cursor && !lx_handle_borrow(cursor, LX_HANDLE_PM_POINTER, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    if (!open32Presentation())
    {
        lx_handle_release(&borrow);
        open32SetError(8);
        return 0;
    }
    ULONG previous = current_cursor;
    current_cursor = cursor;
    /* A valid guest cursor remains current even if a headless host cannot display it. */
    if (!WinSetPointer(HWND_DESKTOP, cursor))
        open32SetError(50); /* ERROR_NOT_SUPPORTED: resource validity and presentation differ. */
    lx_handle_release(&borrow);
    return previous;
}

BOOL DestroyCursor(ULONG cursor)
{
    return WinDestroyPointer(cursor);
}

static BOOL cursor_y(LONG input, LONG *output)
{
    enum
    {
        screen_height = 21
    }; /* SV_CYSCREEN */

    LONG height = WinQuerySysValue(HWND_DESKTOP, screen_height);
    if (height <= 0)
    {
        open32SetError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    /* A pointer denotes a pixel, rather than a rectangle's exclusive edge. */
    int64_t y = (int64_t)height - 1 - input;
    if (y < INT32_MIN || y > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *output = (LONG)y;
    return TRUE;
}

BOOL GetCursorPos(PVOID output)
{
    TRACE_NATIVE("GetCursorPos(%p)", output);
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    POINTL point;
    if (!WinQueryPointerPos(HWND_DESKTOP, &point))
    {
        open32SetError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    LONG y;
    if (!cursor_y(point.y, &y))
        return FALSE;
    lx_guest_store_u32(output, (ULONG)point.x);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)y);
    return TRUE;
}

BOOL SetCursorPos(LONG x, LONG y)
{
    TRACE_NATIVE("SetCursorPos(%d, %d)", x, y);
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    open32ClipCursorPoint(&x, &y);
    LONG native_y;
    if (!cursor_y(y, &native_y))
        return FALSE;
    if (!WinSetPointerPos(HWND_DESKTOP, x, native_y))
    {
        open32SetError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    return TRUE;
}
