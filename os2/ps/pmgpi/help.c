/* Public domain. No rights reserved. Wholly AI-authored. */
/* Help-manager bitmap uploads and explicit errors for unsupported metafile rendering. */
#include "os2/include/pmgpi.h"
#include "os2/ps/pmwin/internal.h"
#include "os2/ps/pmwin/graphics.h"
#include "emulator/abi.h"

static BOOL unsupported(void)
{
    WinSetErrorInfo(PMERR_FUNCTION_NOT_SUPPORTED, 0, NULL);
    return FALSE;
}

LONG GpiPlayMetaFile(HPS ps, ULONG handle, LONG count, PLONG options, PLONG segments,
                     LONG description_length, PSZ description)
{
    (void)ps;
    (void)handle;
    (void)count;
    (void)options;
    (void)segments;
    (void)description_length;
    (void)description;
    unsupported();
    return -1;
}

BOOL GpiQueryBoundaryData(HPS handle, PRECTL bounds)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !bounds)
        return FALSE;
    *bounds = ps->gpi.boundary_set ? ps->gpi.boundary : (RECTL){0, 0, 0, 0};
    return TRUE;
}

BOOL GpiSetDrawControl(HPS handle, LONG control, LONG value)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || control < 1 || control > 5 || (value != 0 && value != 1))
        return FALSE;
    LONG matrix[9];
    if (!GpiQueryDefaultViewMatrix(handle, 9, matrix))
        return FALSE;
    /* DCTL_ERASE and DCTL_DYNAMIC concern retained drawing, which does not exist here. */
    if (control == 2)
        ps->gpi.display_enabled = value;
    else if (control == 3)
        ps->gpi.boundary_enabled = value;
    else if (control == 5)
    {
        ps->gpi.correlate = value;
        ps->gpi.pick_hit = FALSE;
    }
    else if (value && control == 4)
        return unsupported();
    return TRUE;
}

extern BOOL lx_gpi_default_matrix(HPS, LONG, PVOID, LONG);

BOOL GpiSetDefaultViewMatrix(HPS ps, LONG count, PVOID matrix, LONG options)
{
    return lx_gpi_default_matrix(ps, count, matrix, options);
}

LONG GpiSetBitmapBits(HPS handle, LONG start, LONG scans, PVOID bits, PVOID information)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    PmBitmap *destination = ps ? ps->bitmap_lease.resource : NULL;
    if (!destination || !information || start < 0 || scans < 0 ||
        (ULONG)start > destination->height || (scans && !bits))
        return -1;
    if (!scans || (ULONG)start == destination->height)
        return 0;
    unsigned rows = (ULONG)scans > destination->height - (ULONG)start
                        ? destination->height - (ULONG)start
                        : (ULONG)scans;
    uint8 *source = information;
    ULONG fixed = lx_guest_load_u32(source);
    if (fixed != 12 && fixed != 64)
        return -1;
    ULONG width = fixed == 12 ? lx_guest_load_u16(source + 4) : lx_guest_load_u32(source + 4);
    ULONG depth = lx_guest_load_u16(source + (fixed == 12 ? 10 : 14));
    if (width != destination->width ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
        return -1;
    ULONG colors = depth <= 8 ? 1u << depth : 0;
    if (fixed == 64 && lx_guest_load_u32(source + 32))
        colors = lx_guest_load_u32(source + 32);
    if (colors > 256)
        return -1;
    size_t size = fixed + colors * (fixed == 12 ? 3 : 4);
    uint8 *info = malloc(size);
    if (!info)
        return -1;
    memcpy(info, source, size);
    if (fixed == 12)
        lx_guest_store_u16(info + 6, rows);
    else
        lx_guest_store_u32(info + 8, rows);
    ULONG temporary = lx_pm_bitmap_create_data(handle, info, 4, bits, info);
    free(info);
    LxHandleBorrow borrow = {0};
    PmBitmap *decoded = lx_pm_bitmap(temporary, &borrow);
    if (decoded)
    {
        memcpy(destination->pixels + (size_t)start * destination->width, decoded->pixels,
               (size_t)rows * destination->width * sizeof *decoded->pixels);
        destination->palette_count = decoded->palette_count;
        memcpy(destination->palette, decoded->palette, sizeof destination->palette);
    }
    lx_handle_release(&borrow);
    if (temporary)
        lx_pm_bitmap_delete(temporary);
    return decoded ? (LONG)rows : -1;
}
