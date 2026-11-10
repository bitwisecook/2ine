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
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        return (uintptr_t)name <= UINT16_MAX
                   ? WinLoadPointer(HWND_DESKTOP, module, (ULONG)(uintptr_t)name)
                   : 0;
    }
    LONG id = 0;
    if (!module)
        switch ((uintptr_t)name)
        {
            case WIN32_IDC_ARROW:
                id = SPTR_ARROW;
                break;
            case WIN32_IDC_IBEAM:
                id = SPTR_TEXT;
                break;
            case WIN32_IDC_WAIT:
            case WIN32_IDC_APPSTARTING:
                id = SPTR_WAIT;
                break;
            case WIN32_IDC_SIZENWSE:
                id = SPTR_SIZENWSE;
                break;
            case WIN32_IDC_SIZENESW:
                id = SPTR_SIZENESW;
                break;
            case WIN32_IDC_SIZEWE:
                id = SPTR_SIZEWE;
                break;
            case WIN32_IDC_SIZENS:
                id = SPTR_SIZENS;
                break;
            case WIN32_IDC_SIZEALL:
                id = SPTR_MOVE;
                break;
            case WIN32_IDC_NO:
                id = SPTR_ILLEGAL;
                break;
        }
    ULONG result = id ? lx_pm_system_pointer(id) : 0;
    if (!result)
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return result;
}

ULONG SetCursor(ULONG cursor)
{
    LxHandleBorrow borrow = {0};
    if (cursor && !lx_handle_borrow(cursor, LX_HANDLE_PM_POINTER, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!open32Presentation())
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    ULONG previous = current_cursor;
    current_cursor = cursor;
    /* A valid guest cursor remains current even if a headless host cannot display it. */
    if (!WinSetPointer(HWND_DESKTOP, cursor))
        open32SetError(ERROR_NOT_SUPPORTED);
    lx_handle_release(&borrow);
    return previous;
}

BOOL DestroyCursor(ULONG cursor)
{
    return WinDestroyPointer(cursor);
}

static BOOL cursor_y(LONG input, LONG *output)
{
    LONG height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
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
    lx_guest_store_u32((uint8 *)output + WIN32_POINT_Y, (ULONG)y);
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
