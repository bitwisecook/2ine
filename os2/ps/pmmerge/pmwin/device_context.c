/* Public domain. No rights reserved. Wholly AI-authored. */
/* Device contexts: typed identities, display contexts, association with a PS.
   A context cannot close while associated. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmdev.h"
#include "os2/ps/pmmerge/pmwin/print_job.h"

/* Display resolution: the configured DPI within a sane range, else 96. Printers use 300. */
enum
{
    DEFAULT_DISPLAY_DPI = 96,
    MIN_DISPLAY_DPI = 72,
    MAX_DISPLAY_DPI = 384,
    PRINTER_DPI = 300
};

static void destroy_dc(void *resource)
{
    PmDeviceContext *dc = resource;
    AnchorBlock *anchor = dc->anchor;
    PmDeviceContext **slot = &anchor->devices;
    while (*slot && *slot != dc)
        slot = &(*slot)->next;
    if (*slot)
        *slot = dc->next;
    lx_print_abort(dc->print_job);
    if (dc->printer_bitmap)
        lx_pm_bitmap_delete(dc->printer_bitmap);
    free(dc);
}

HDC lx_pm_open_dc(HAB hab, LONG type, HDC compatible)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || (type != OD_SCREEN && type != OD_INFO && type != OD_MEMORY) ||
        (compatible && !lx_pm_dc_valid(compatible)))
        return 0;
    PmDeviceContext *dc = calloc(1, sizeof *dc);
    if (!dc)
        return 0;
    dc->anchor = anchor;
    dc->type = type;
    dc->handle = lx_handle_create(LX_HANDLE_PM_DC, dc, destroy_dc);
    if (!dc->handle)
    {
        free(dc);
        return 0;
    }
    dc->next = anchor->devices;
    anchor->devices = dc;
    return dc->handle;
}

unsigned lx_pm_dc_dpi(HDC handle)
{
    unsigned dpi = GProcessState.display_dpi;
    return lx_pm_printer_kind(handle)                         ? PRINTER_DPI
           : dpi >= MIN_DISPLAY_DPI && dpi <= MAX_DISPLAY_DPI ? dpi
                                                              : DEFAULT_DISPLAY_DPI;
}

/* Scales a 96-DPI measurement to the display, rounding to the nearest pel. */
LONG lx_pm_display_scale(LONG pixels)
{
    return (LONG)(((int64_t)pixels * lx_pm_dc_dpi(0) + DEFAULT_DISPLAY_DPI / 2) /
                  DEFAULT_DISPLAY_DPI);
}

BOOL lx_pm_dc_valid(HDC handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return FALSE;
    if (getWindowFromHWND(anchor, handle))
        return TRUE;
    LxHandleBorrow borrow = {0};
    BOOL valid = FALSE;
    if (lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        valid = ((PmDeviceContext *)borrow.resource)->anchor == anchor;
    lx_handle_release(&borrow);
    return valid;
}

static int unused_dc(void *resource)
{
    return ((PmDeviceContext *)resource)->spaces == 0;
}

LONG lx_pm_close_dc(HDC handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    LxHandleBorrow borrow = {0};
    LONG result = -1;
    if (anchor && lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
    {
        PmDeviceContext *dc = borrow.resource;
        if (dc->anchor == anchor &&
            lx_handle_close_guarded(handle, LX_HANDLE_PM_DC, 1, unused_dc) == 1)
            result = 0;
    }
    lx_handle_release(&borrow);
    return result;
}

void destroyDeviceContexts(AnchorBlock *anchor)
{
    while (anchor->devices)
        lx_handle_close(anchor->devices->handle, LX_HANDLE_PM_DC);
}

HDC lx_pm_query_dc(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->dc : HDC_ERROR;
}

BOOL lx_pm_associate_dc(HPS handle, HDC dc)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
    if (!ps || (dc && !lx_pm_dc_valid(dc)))
        return FALSE;
    LxHandleBorrow next = {0};
    Window *window = dc ? getWindowFromHWND(anchor, dc) : NULL;
    if (dc && !window)
    {
        if (!lx_handle_borrow(dc, LX_HANDLE_PM_DC, &next))
            return FALSE;
        ((PmDeviceContext *)next.resource)->spaces++;
    }
    PmDeviceContext *previous = ps->dc_lease.resource;
    if (previous && previous->printer_kind && ps->bitmap_lease.resource)
    {
        ((PmBitmap *)ps->bitmap_lease.resource)->selected--;
        lx_handle_release(&ps->bitmap_lease);
    }
    if (ps->dc_lease.resource)
        ((PmDeviceContext *)ps->dc_lease.resource)->spaces--;
    lx_handle_release(&ps->dc_lease);
    ps->dc_lease = next;
    ps->dc = dc;
    ps->window = window;
    PmDeviceContext *printer = next.resource;
    if (printer && printer->printer_kind)
    {
        if (ps->bitmap_lease.resource)
        {
            ((PmBitmap *)ps->bitmap_lease.resource)->selected--;
            lx_handle_release(&ps->bitmap_lease);
        }
        PmBitmap *page = lx_pm_bitmap(printer->printer_bitmap, &ps->bitmap_lease);
        if (!page)
            return FALSE;
        page->selected++;
    }
    return TRUE;
}

HDC WinOpenWindowDC(HWND hwnd)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && getWindowFromHWND(anchor, hwnd) ? hwnd : 0;
}

HDC WinQueryWindowDC(HWND hwnd)
{
    return WinOpenWindowDC(hwnd);
}
