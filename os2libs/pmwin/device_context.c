/* Public domain. No rights reserved. Wholly AI-authored. */
/* Display-compatible contexts have typed identities and cannot close while associated. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/print_job.h"

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
    if (!anchor || (type != 0 && type != 6 && type != 8) ||
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

HDC lx_pm_open_printer_dc(HAB hab, LONG type, unsigned kind)
{
    if ((type != 2 && type != 5 && type != 6) || (kind != LX_PRINT_PDF && kind != LX_PRINT_PCL))
        return 0;
    HDC handle = lx_pm_open_dc(hab, 6, 0);
    LxHandleBorrow borrow = {0};
    if (!handle || !lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return 0;
    PmDeviceContext *dc = borrow.resource;
    dc->type = type;
    dc->printer_kind = kind;
    dc->printer_bitmap = lx_pm_bitmap_create(2480, 3508, 1, 24);
    LxHandleBorrow bitmap = {0};
    PmBitmap *page = lx_pm_bitmap(dc->printer_bitmap, &bitmap);
    if (page)
        for (size_t i = 0; i < (size_t)page->width * page->height; i++)
            page->pixels[i] = 0xffffff;
    lx_handle_release(&bitmap);
    lx_handle_release(&borrow);
    if (!page)
    {
        lx_pm_close_dc(handle);
        return 0;
    }
    return handle;
}

BOOL lx_pm_printer_size(HDC handle, unsigned *width, unsigned *height)
{
    LxHandleBorrow borrow = {0}, bitmap = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return FALSE;
    PmDeviceContext *dc = borrow.resource;
    PmBitmap *page = dc->printer_kind ? lx_pm_bitmap(dc->printer_bitmap, &bitmap) : NULL;
    if (page)
    {
        *width = page->width;
        *height = page->height;
    }
    lx_handle_release(&bitmap);
    lx_handle_release(&borrow);
    return page != NULL;
}

BOOL lx_pm_printer_options(HDC handle, unsigned copies, unsigned duplex, BOOL draft)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return FALSE;
    PmDeviceContext *dc = borrow.resource;
    BOOL printer = dc->printer_kind != 0;
    if (printer)
    {
        dc->print_copies = copies;
        dc->print_duplex = duplex;
        dc->print_draft = draft;
        lx_print_options(dc->print_job, copies, duplex, draft);
    }
    lx_handle_release(&borrow);
    return printer;
}

/* A page size change replaces the blank page in place, so bound presentation spaces keep their
   lease. It is refused during a job's page output. */
BOOL lx_pm_printer_page(HDC handle, unsigned width, unsigned height)
{
    LxHandleBorrow borrow = {0}, bitmap = {0};
    if (!width || !height || (uint64_t)width * height > 16777216 ||
        !lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return FALSE;
    PmDeviceContext *dc = borrow.resource;
    PmBitmap *page = dc->printer_kind ? lx_pm_bitmap(dc->printer_bitmap, &bitmap) : NULL;
    uint32_t *pixels = page ? malloc((size_t)width * height * sizeof *pixels) : NULL;
    if (pixels)
    {
        for (size_t i = 0; i < (size_t)width * height; i++)
            pixels[i] = 0xffffff;
        free(page->pixels);
        page->pixels = pixels;
        page->width = width;
        page->height = height;
    }
    lx_handle_release(&bitmap);
    lx_handle_release(&borrow);
    return pixels != NULL;
}

unsigned lx_pm_printer_kind(HDC handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    LxHandleBorrow borrow = {0};
    unsigned kind = 0;
    if (anchor && lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
    {
        PmDeviceContext *dc = borrow.resource;
        if (dc->anchor == anchor)
            kind = dc->printer_kind;
    }
    lx_handle_release(&borrow);
    return kind;
}

unsigned lx_pm_dc_dpi(HDC handle)
{
    unsigned dpi = GLoaderState.display_dpi;
    return lx_pm_printer_kind(handle) ? 300 : dpi >= 72 && dpi <= 384 ? dpi : 96;
}

LONG lx_pm_display_scale(LONG pixels)
{
    return (LONG)(((int64_t)pixels * lx_pm_dc_dpi(0) + 48) / 96);
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
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
    return ps ? ps->dc : (HDC)-1;
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

LONG lx_pm_print_escape(HDC handle, LONG code, LONG input_count, const void *input,
                        PLONG output_count, void *output)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    LxHandleBorrow borrow = {0}, bitmap = {0};
    LONG result = -1;
    if (!anchor || input_count < 0 || input_count > 1048576 || (input_count && !input) ||
        !lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return -1;
    PmDeviceContext *dc = borrow.resource;
    PmBitmap *page =
        dc->anchor == anchor && dc->printer_kind ? lx_pm_bitmap(dc->printer_bitmap, &bitmap) : NULL;
    if (!page)
        goto done;
    if (code == 0)
    {
        if (input_count == 4)
        {
            LONG escape = (LONG)lx_guest_load_u32(input);
            result = escape == 0 || escape == 1 || escape == 8150 || escape == 8151 ||
                             escape == 8153 || escape == 16300 || escape == 16302 || escape == 16303
                         ? 1
                         : 0;
        }
    }
    else if (code == 1)
    {
        if (output_count && *output_count >= 8 && output)
        {
            lx_guest_store_u32(output, 0);
            lx_guest_store_u32((uint8 *)output + 4, 0);
            *output_count = 8;
            result = 1;
        }
    }
    else if (code == 8150)
    {
        if (!dc->print_job && dc->type != 6 && input_count <= 4096)
        {
            char *title = calloc((size_t)input_count + 1, 1);
            if (!title)
                goto done;
            if (input_count)
                memcpy(title, input, (size_t)input_count);
            int cancelled = 0;
            dc->print_job = lx_print_begin(dc->printer_kind, title, &cancelled);
            free(title);
            if (dc->print_job)
            {
                lx_print_options(dc->print_job, dc->print_copies, dc->print_duplex,
                                 dc->print_draft);
                dc->print_pages = 0;
                for (size_t i = 0; i < (size_t)page->width * page->height; i++)
                    page->pixels[i] = 0xffffff;
                result = 1;
            }
            else if (cancelled)
                result = 0;
        }
    }
    else if (code == 8153)
    {
        if (dc->print_job)
        {
            lx_print_abort(dc->print_job);
            dc->print_job = NULL;
            result = 1;
        }
    }
    else if (code == 16300 || code == 8151)
    {
        if (dc->print_job)
        {
            BOOL marked = !dc->print_pages;
            for (size_t i = 0; !marked && i < (size_t)page->width * page->height; i++)
                marked = page->pixels[i] != 0xffffff;
            BOOL ok = (code == 8151 && !marked) ||
                      lx_print_page(dc->print_job, page->pixels, page->width, page->height, 300);
            if (ok)
                dc->print_pages++;
            if (code == 8151)
            {
                LxPrintJob *job = dc->print_job;
                dc->print_job = NULL;
                if (ok)
                    ok = lx_print_finish(job);
                else
                    lx_print_abort(job);
            }
            else if (ok)
                for (size_t i = 0; i < (size_t)page->width * page->height; i++)
                    page->pixels[i] = 0xffffff;
            result = ok ? 1 : -1;
        }
    }
    else if (code == 16302)
        result = dc->print_job ? 1 : -1;
    else if (code == 16303)
        result = lx_print_raw(dc->print_job, input, (size_t)input_count) ? 1 : -1;
    else
        result = 0;
done:
    lx_handle_release(&bitmap);
    lx_handle_release(&borrow);
    return result;
}
