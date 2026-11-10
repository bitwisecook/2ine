/* Public domain. No rights reserved. Wholly AI-authored. */
/* Printer device contexts: page size, options, page breaks and DevEscape
   requests, written out through the print job. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmdev.h"
#include "os2/ps/pmmerge/pmwin/print_job.h"

enum
{
    PAGE_DPI = 300,
    A4_WIDTH = 2480,            /* 210 mm at PAGE_DPI */
    A4_HEIGHT = 3508,           /* 297 mm at PAGE_DPI */
    MAX_PAGE_PIXELS = 16777216, /* 4096 x 4096 */
    MAX_ESCAPE_INPUT = 1048576, /* bytes */
    MAX_DOCUMENT_NAME = 4096,   /* bytes */
    PAPER_WHITE = 0xffffff
};

HDC lx_pm_open_printer_dc(HAB hab, LONG type, unsigned kind)
{
    if ((type != OD_QUEUED && type != OD_DIRECT && type != OD_INFO) ||
        (kind != LX_PRINT_PDF && kind != LX_PRINT_PCL))
        return 0;
    HDC handle = lx_pm_open_dc(hab, OD_INFO, 0);
    LxHandleBorrow borrow = {0};
    if (!handle || !lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return 0;
    PmDeviceContext *dc = borrow.resource;
    dc->type = type;
    dc->printer_kind = kind;
    dc->printer_bitmap = lx_pm_bitmap_create(A4_WIDTH, A4_HEIGHT, 1, 24);
    LxHandleBorrow bitmap = {0};
    PmBitmap *page = lx_pm_bitmap(dc->printer_bitmap, &bitmap);
    if (page)
        for (size_t i = 0; i < (size_t)page->width * page->height; i++)
            page->pixels[i] = PAPER_WHITE;
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
    if (!width || !height || (uint64_t)width * height > MAX_PAGE_PIXELS ||
        !lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return FALSE;
    PmDeviceContext *dc = borrow.resource;
    PmBitmap *page = dc->printer_kind ? lx_pm_bitmap(dc->printer_bitmap, &bitmap) : NULL;
    uint32_t *pixels = page ? malloc((size_t)width * height * sizeof *pixels) : NULL;
    if (pixels)
    {
        for (size_t i = 0; i < (size_t)width * height; i++)
            pixels[i] = PAPER_WHITE;
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

LONG lx_pm_print_escape(HDC handle, LONG code, LONG input_count, const void *input,
                        PLONG output_count, void *output)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    LxHandleBorrow borrow = {0}, bitmap = {0};
    LONG result = DEVESC_ERROR;
    if (!anchor || input_count < 0 || input_count > MAX_ESCAPE_INPUT || (input_count && !input) ||
        !lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        return DEVESC_ERROR;
    PmDeviceContext *dc = borrow.resource;
    PmBitmap *page =
        dc->anchor == anchor && dc->printer_kind ? lx_pm_bitmap(dc->printer_bitmap, &bitmap) : NULL;
    if (!page)
        goto done;
    if (code == DEVESC_QUERYESCSUPPORT)
    {
        if (input_count == sizeof(LONG))
        {
            LONG escape = (LONG)lx_guest_load_u32(input);
            result = escape == DEVESC_QUERYESCSUPPORT || escape == DEVESC_GETSCALINGFACTOR ||
                             escape == DEVESC_STARTDOC || escape == DEVESC_ENDDOC ||
                             escape == DEVESC_ABORTDOC || escape == DEVESC_NEWFRAME ||
                             escape == DEVESC_FLUSHOUTPUT || escape == DEVESC_RAWDATA
                         ? DEV_OK
                         : DEVESC_NOTIMPLEMENTED;
        }
    }
    else if (code == DEVESC_GETSCALINGFACTOR)
    {
        /* Two zero shift counts: the page is not scaled. */
        if (output_count && *output_count >= 8 && output)
        {
            lx_guest_store_u32(output, 0);
            lx_guest_store_u32((uint8 *)output + 4, 0);
            *output_count = 8;
            result = DEV_OK;
        }
    }
    else if (code == DEVESC_STARTDOC)
    {
        if (!dc->print_job && dc->type != OD_INFO && input_count <= MAX_DOCUMENT_NAME)
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
                    page->pixels[i] = PAPER_WHITE;
                result = DEV_OK;
            }
            else if (cancelled)
                result = DEV_ERROR;
        }
    }
    else if (code == DEVESC_ABORTDOC)
    {
        if (dc->print_job)
        {
            lx_print_abort(dc->print_job);
            dc->print_job = NULL;
            result = DEV_OK;
        }
    }
    else if (code == DEVESC_NEWFRAME || code == DEVESC_ENDDOC)
    {
        if (dc->print_job)
        {
            BOOL marked = !dc->print_pages;
            for (size_t i = 0; !marked && i < (size_t)page->width * page->height; i++)
                marked = page->pixels[i] != PAPER_WHITE;
            /* ENDDOC emits its last page only when something was drawn on it. */
            BOOL ok =
                (code == DEVESC_ENDDOC && !marked) ||
                lx_print_page(dc->print_job, page->pixels, page->width, page->height, PAGE_DPI);
            if (ok)
                dc->print_pages++;
            if (code == DEVESC_ENDDOC)
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
                    page->pixels[i] = PAPER_WHITE;
            result = ok ? DEV_OK : DEVESC_ERROR;
        }
    }
    else if (code == DEVESC_FLUSHOUTPUT)
        result = dc->print_job ? DEV_OK : DEVESC_ERROR;
    else if (code == DEVESC_RAWDATA)
        result = lx_print_raw(dc->print_job, input, (size_t)input_count) ? DEV_OK : DEVESC_ERROR;
    else
        result = DEVESC_NOTIMPLEMENTED;
done:
    lx_handle_release(&bitmap);
    lx_handle_release(&borrow);
    return result;
}
