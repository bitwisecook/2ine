/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmwinx/windows.h"
#include "hal/host_library.h"
static _Thread_local LONG cursor_count;
static ULONG locked_window; /* WinLockWindowUpdate state is shared by the desktop. */
LONG ShowCursor(BOOL show)
{
    LONG previous = cursor_count;
    if (show)
    {
        if (cursor_count < INT32_MAX)
            cursor_count++;
    }
    else if (cursor_count > INT32_MIN)
        cursor_count--;
    if ((previous < 0) != (cursor_count < 0))
        WinShowPointer(HWND_DESKTOP, cursor_count >= 0);
    return cursor_count;
}
void open32ClipCursorPoint(LONG *x, LONG *y)
{
    LONG height = WinQuerySysValue(HWND_DESKTOP, 21), native = height - 1 - *y;
    lx_pm_clip_pointer(x, &native);
    *y = height - 1 - native;
}
BOOL ClipCursor(PVOID rectangle)
{
    if (!open32Presentation())
    {
        open32SetError(8);
        return FALSE;
    }
    if (!rectangle)
    {
        lx_pm_set_pointer_clip(NULL);
        return TRUE;
    }
    /* Windows bounds become inclusive PM desktop bounds limited to the screen. */
    const uint8 *wire = rectangle;
    LONG width = WinQuerySysValue(HWND_DESKTOP, 20), height = WinQuerySysValue(HWND_DESKTOP, 21);
    LONG left = (LONG)lx_guest_load_u32(wire), top = height - (LONG)lx_guest_load_u32(wire + 4);
    LONG right = (LONG)lx_guest_load_u32(wire + 8);
    LONG bottom = height - (LONG)lx_guest_load_u32(wire + 12);
    RECTL bounds = {left > 0 ? left : 0, bottom > 0 ? bottom : 0,
                    right < width - 1 ? right : width - 1, top < height - 1 ? top : height - 1};
    lx_pm_set_pointer_clip(&bounds);
    POINTL point;
    if (WinQueryPointerPos(HWND_DESKTOP, &point))
        WinSetPointerPos(HWND_DESKTOP, point.x, point.y);
    return TRUE;
}
BOOL open32WindowUpdateLocked(ULONG window)
{
    ULONG locked = __atomic_load_n(&locked_window, __ATOMIC_ACQUIRE);
    return locked &&
           (locked == window || open32ClientWindow(locked) == window || IsChild(locked, window));
}
BOOL LockWindowUpdate(ULONG window)
{
    if (window && !IsWindow(window))
    {
        open32SetError(1400);
        return FALSE;
    }
    if (window)
    {
        ULONG expected = 0;
        return __atomic_compare_exchange_n(&locked_window, &expected, window, 0, __ATOMIC_ACQ_REL,
                                           __ATOMIC_ACQUIRE);
    }
    ULONG previous = __atomic_exchange_n(&locked_window, 0, __ATOMIC_ACQ_REL);
    if (previous && IsWindow(previous))
        InvalidateRect(previous, NULL, TRUE);
    return TRUE;
}
BOOL RemoveFontResource(PSZ path)
{
    if (!path || !*path)
    {
        open32SetError(87);
        return FALSE;
    }
    /* Font registration belongs to the native GPI provider; resource unloads
       must affect its catalogue rather than fabricate a successful removal. */
    typedef BOOL(OS2API * Unload)(ULONG, PSZ);
    Unload unload = NULL;
    for (LxModule *module = GLoaderState.loaded_modules; module; module = module->next)
        if (module->nativelib && !strcasecmp(module->name, "PMGPI"))
        {
            unload = (Unload)lx_host_library_symbol(module->nativelib, "GpiUnloadFonts");
            break;
        }
    BOOL result = unload && unload(open32Presentation(), path);
    if (!result)
        open32SetError(unload ? 2 : 50);
    return result;
}
