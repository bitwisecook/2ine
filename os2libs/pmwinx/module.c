/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx.h"
#include "os2libs/pmwin.h"
#include "os2libs/generated/pmwinx-lx.h"

static uint32 references;
static LX_HOST_THREAD_LOCAL ULONG current_cursor;
ULONG LoadCursor(ULONG instance, ULONG name)
{
    TRACE_NATIVE("LoadCursor(%u, %u)", instance, name);
    if (instance)
        return name <= 0xffff ? WinLoadPointer(HWND_DESKTOP, instance, name) : 0;
    LONG id;
    switch (name)
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
            break; /* IDC_WAIT / IDC_APPSTARTING */
        case 32642:
            id = 6;
            break;
        case 32643:
            id = 7;
            break;
        case 32644:
            id = 8;
            break;
        case 32645:
            id = 9;
            break;
        case 32646:
            id = 5;
            break; /* IDC_SIZEALL */
        case 32648:
            id = 18;
            break; /* IDC_NO */
        default:
            return 0;
    }
    return WinQuerySysPointer(HWND_DESKTOP, id, FALSE);
}
ULONG SetCursor(ULONG cursor)
{
    ULONG previous = current_cursor;
    if (!WinSetPointer(HWND_DESKTOP, cursor))
        return 0;
    current_cursor = cursor;
    return previous;
}
BOOL DestroyCursor(ULONG cursor)
{
    return WinDestroyPointer(cursor);
}
BOOL WinXProcessMsg(PVOID message)
{
    (void)message;
    /* This worker creates OS/2 PM windows; no Open32 window owns their messages. */
    return FALSE;
}

VOID WinInitializeOpen32(ULONG reserved)
{
    TRACE_NATIVE("WinInitializeOpen32(%u)", (uint)reserved);
    __atomic_add_fetch(&references, 1, __ATOMIC_RELAXED);
}

VOID WinTerminateOpen32(ULONG reserved)
{
    TRACE_NATIVE("WinTerminateOpen32(%u)", (uint)reserved);
    uint32 count = __atomic_load_n(&references, __ATOMIC_RELAXED);
    while (count && !__atomic_compare_exchange_n(&references, &count, count - 1, 0,
                                                 __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
    }
}

LONG MulDiv(LONG number, LONG numerator, LONG denominator)
{
    if (!denominator)
        return -1;
    int64_t product = (int64_t)number * numerator;
    const int negative = (product < 0) != (denominator < 0);
    if (product < 0)
        product = -product;
    int64_t divisor = denominator;
    if (divisor < 0)
        divisor = -divisor;
    int64_t result = (product + divisor / 2) / divisor;
    if (negative)
        result = -result;
    return result < INT32_MIN || result > INT32_MAX ? -1 : (LONG)result;
}
