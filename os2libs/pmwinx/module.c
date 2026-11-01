/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmgpi.h"
#include "guest/handles.h"
#include "os2libs/generated/pmwinx-lx.h"

static uint32 references;
ULONG LoadLibraryOpen32(PSZ name)
{
    TRACE_NATIVE("LoadLibraryOpen32('%s')", name);
    LxModule *module =
        name && *name && GLoaderState.loadModule ? GLoaderState.loadModule(name) : NULL;
    return module ? module->handle : 0;
}
BOOL FreeLibraryOpen32(ULONG module)
{
    TRACE_NATIVE("FreeLibraryOpen32(%u)", module);
    LxHandleBorrow borrow = {0};
    if (!GLoaderState.freeModule || !lx_handle_borrow(module, LX_HANDLE_MODULE, &borrow))
        return FALSE;
    GLoaderState.freeModule(borrow.resource);
    lx_handle_release(&borrow);
    return TRUE;
}
typedef struct Open32DC
{
    HPS space;
    HWND window;
    HAB anchor;
} Open32DC;
static void destroyOpen32DC(void *value)
{
    Open32DC *dc = value;
    if (dc->anchor == WinQueryAnchorBlock(HWND_DESKTOP))
        WinReleasePS(dc->space);
    free(dc);
}
static Open32DC *borrowOpen32DC(ULONG handle, LxHandleBorrow *borrow)
{
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DC, borrow))
        return NULL;
    Open32DC *dc = borrow->resource;
    if (dc->anchor == WinQueryAnchorBlock(HWND_DESKTOP))
        return dc;
    lx_handle_release(borrow);
    return NULL;
}
ULONG GetDCOpen32(HWND window)
{
    TRACE_NATIVE("GetDCOpen32(%u)", window);
    window = window ? window : HWND_DESKTOP;
    if (!WinIsWindow(0, window))
        return 0;
    Open32DC *dc = calloc(1, sizeof *dc);
    if (!dc)
        return 0;
    dc->window = window;
    dc->anchor = WinQueryAnchorBlock(HWND_DESKTOP);
    /* Foreign display windows share these screen capabilities; drawing stays local. */
    dc->space = WinGetPS(WinQueryWindowDC(window) ? window : HWND_DESKTOP);
    ULONG result = dc->space ? lx_handle_create(LX_HANDLE_OPEN32_DC, dc, destroyOpen32DC) : 0;
    if (!result)
        destroyOpen32DC(dc);
    return result;
}
LONG ReleaseDCOpen32(HWND window, ULONG handle)
{
    TRACE_NATIVE("ReleaseDCOpen32(%u, %u)", window, handle);
    LxHandleBorrow borrow = {0};
    Open32DC *dc = borrowOpen32DC(handle, &borrow);
    if (!dc)
        return 0;
    LONG result = dc->window == (window ? window : HWND_DESKTOP) &&
                  lx_handle_close(handle, LX_HANDLE_OPEN32_DC);
    lx_handle_release(&borrow);
    return result;
}
LONG GetDeviceCapsOpen32(ULONG handle, LONG index)
{
    TRACE_NATIVE("GetDeviceCapsOpen32(%u, %d)", handle, index);
    LxHandleBorrow borrow = {0};
    Open32DC *dc = borrowOpen32DC(handle, &borrow);
    if (!dc)
        return 0;
    HDC device = GpiQueryDevice(dc->space);
    LONG caps[42];
    BOOL valid = device != (HDC)-1 && DevQueryCaps(device, 0, 42, caps);
    lx_handle_release(&borrow);
    if (!valid)
        return 0;
    switch (index)
    {
        case 2:
            return caps[2] == 2 ? 1 : 0; /* DT_RASDISPLAY */
        case 4:
            return caps[8] ? (LONG)((int64_t)caps[4] * 1000 / caps[8]) : 0;
        case 6:
            return caps[9] ? (LONG)((int64_t)caps[5] * 1000 / caps[9]) : 0;
        case 8:
            return caps[4];
        case 10:
            return caps[5];
        case 12:
            return caps[16];
        case 14:
            return caps[15];
        case 22:
            return caps[27];
        case 24:
            return caps[16] > 8 ? -1 : caps[14];
        case 88:
            return caps[37];
        case 90:
            return caps[38];
        case 108:
            return caps[16];
        default:
            return 0;
    }
}
LONG LoadStringOpen32(ULONG module, ULONG id, PCHAR output, LONG capacity)
{
    TRACE_NATIVE("LoadStringOpen32(%u, %u, %p, %d)", module, id, output, capacity);
    if (!module)
        return 0;
    if (module == UINT32_MAX)
        module = 0;
    if (id & 0xff000000u)
        id &= 0xffffu;
    return WinLoadString(0, module, id, capacity, output);
}

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
