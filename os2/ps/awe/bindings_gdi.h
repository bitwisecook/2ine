/* Public domain. No rights reserved. Wholly AI-authored. */
/* Checked graphics ABI: stack32 words, byte snapshots, no host SDK structures. */
#ifndef TWOINE_PMWINX_BINDINGS_GDI_H
#define TWOINE_PMWINX_BINDINGS_GDI_H

enum
{
    /* Bound on metafile, device, port and face names copied from the guest. */
    GRAPHICS_NAME_MAX = 4096,
    /* GETTECHNOLOGY writes "Raster Printer" or "Raster Display" and its NUL. */
    ESCAPE_TECHNOLOGY_SIZE = 15,
    ESCAPE_PAPER_BINS_MAX = 256,
    /* DeviceCapabilities reports Letter, Legal and A4 from one bin; names are fixed-width. */
    DEVICE_PAPER_COUNT = 3,
    DEVICE_BIN_COUNT = 1,
    DEVICE_BIN_NAME_SIZE = 24,
    DEVICE_PAPER_NAME_SIZE = 64
};

typedef struct O32GraphicsBuffer
{
    LxGuestBorrow lease;
    void *copy, *host;
} O32GraphicsBuffer;

static BOOL o32_graphics_buffer(ULONG address, uint64 size, unsigned mode, BOOL optional,
                                O32GraphicsBuffer *out)
{
    if (!address)
    {
        if (optional)
            return TRUE;
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (size > OPEN32_GUEST_BUFFER_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!size)
    {
        out->copy = calloc(1, 1);
        out->host = out->copy;
        return out->host != NULL;
    }
    if (!lx_execution_borrow(GProcessState.execution, address, (size_t)size, mode, &out->lease))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (mode == LX_GUEST_READ)
    {
        out->copy = malloc((size_t)size);
        if (!out->copy)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        memcpy(out->copy, out->lease.host, (size_t)size);
        out->host = out->copy;
    }
    else
        out->host = out->lease.host;
    return TRUE;
}

static void o32_graphics_release(O32GraphicsBuffer *buffer)
{
    free(buffer->copy);
    lx_execution_release(&buffer->lease);
}

static uint32 bridge32_AnimatePalette(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, (uint64)(a2 * (uint64)WIN32_PALETTEENTRY_SIZE),
                                    LX_GUEST_READ, FALSE, &p3);
    if (valid)
        result = (uint32)AnimatePalette((ULONG)a0, (ULONG)a1, (ULONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_Arc(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    ULONG a6 = lx_guest_load_u32(a + 24);
    ULONG a7 = lx_guest_load_u32(a + 28);
    ULONG a8 = lx_guest_load_u32(a + 32);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)Arc((ULONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4, (LONG)a5, (LONG)a6,
                             (LONG)a7, (LONG)a8);
    return result;
}

static uint32 bridge32_BeginPath(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)BeginPath((ULONG)a0);
    return result;
}

static uint32 bridge32_CloseMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)CloseMetaFile((ULONG)a0);
    return result;
}

static uint32 bridge32_CopyMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    char *s1 = a1 ? lx_execution_copy_string(GProcessState.execution, a1, GRAPHICS_NAME_MAX) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a1 && !s1)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        valid = FALSE;
    }
    if (valid)
        result = (uint32)CopyMetaFile((ULONG)a0, s1);
    free(s1);
    return result;
}

static uint32 bridge32_CreateBrushIndirect(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    O32GraphicsBuffer p0 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a0, WIN32_LOGBRUSH_SIZE, LX_GUEST_READ, FALSE, &p0);
    if (valid)
        result = (uint32)CreateBrushIndirect((PVOID)p0.host);
    o32_graphics_release(&p0);
    return result;
}

static uint32 bridge32_CreateEllipticRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)CreateEllipticRgn((LONG)a0, (LONG)a1, (LONG)a2, (LONG)a3);
    return result;
}

static uint32 bridge32_CreateEllipticRgnIndirect(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    O32GraphicsBuffer p0 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a0, WIN32_RECT_SIZE, LX_GUEST_READ, FALSE, &p0);
    if (valid)
        result = (uint32)CreateEllipticRgnIndirect((PVOID)p0.host);
    o32_graphics_release(&p0);
    return result;
}

static uint32 bridge32_CreateHatchBrush(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)CreateHatchBrush((LONG)a0, (ULONG)a1);
    return result;
}

static uint32 bridge32_CreateMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    char *s0 = a0 ? lx_execution_copy_string(GProcessState.execution, a0, GRAPHICS_NAME_MAX) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a0 && !s0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        valid = FALSE;
    }
    if (valid)
        result = (uint32)CreateMetaFile(s0);
    free(s0);
    return result;
}

static uint32 bridge32_CreatePolygonRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p0 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(
            a0,
            (uint64)(a1 <= OPEN32_ELEMENT_COUNT_MAX ? a1 * (uint64)WIN32_POINT_SIZE : UINT64_MAX),
            LX_GUEST_READ, FALSE, &p0);
    if (valid)
        result = (uint32)CreatePolygonRgn((PVOID)p0.host, (LONG)a1, (LONG)a2);
    o32_graphics_release(&p0);
    return result;
}

static uint32 bridge32_CreateRoundRectRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result =
            (uint32)CreateRoundRectRgn((LONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4, (LONG)a5);
    return result;
}

static uint32 bridge32_DeleteEnhMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)DeleteEnhMetaFile((ULONG)a0);
    return result;
}

static uint32 bridge32_DrawFocusRect(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_RECT_SIZE, LX_GUEST_READ, FALSE, &p1);
    if (valid)
        result = (uint32)DrawFocusRect((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_DrawIcon(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)DrawIcon((ULONG)a0, (LONG)a1, (LONG)a2, (ULONG)a3);
    return result;
}

static uint32 bridge32_Ellipse(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)Ellipse((ULONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4);
    return result;
}

static uint32 bridge32_EndPath(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)EndPath((ULONG)a0);
    return result;
}

static uint32 bridge32_EnumEnhMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    O32GraphicsBuffer p4 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a4, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p4);
    if (valid)
        result =
            (uint32)EnumEnhMetaFile((ULONG)a0, (ULONG)a1, (ULONG)a2, (ULONG)a3, (PVOID)p4.host);
    o32_graphics_release(&p4);
    return result;
}

static uint32 bridge32_EnumFonts(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    char *s1 = a1 ? lx_execution_copy_string(GProcessState.execution, a1, GRAPHICS_NAME_MAX) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a1 && !s1)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        valid = FALSE;
    }
    if (valid)
        result = (uint32)EnumFonts((ULONG)a0, s1, (ULONG)a2, (ULONG)a3);
    free(s1);
    return result;
}

static uint32 bridge32_EqualRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)EqualRgn((ULONG)a0, (ULONG)a1);
    return result;
}

static uint32 bridge32_ExtCreatePen(uint8 *a)
{
    O32GraphicsBuffer brush = {0};
    uint32 result = 0;
    /* PS_USERSTYLE is refused, so the style-entry array is never read. */
    if (o32_graphics_buffer(lx_guest_load_u32(a + 8), 12, LX_GUEST_READ, FALSE, &brush))
        result = (uint32)ExtCreatePen(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), brush.host,
                                      lx_guest_load_u32(a + 12), NULL);
    o32_graphics_release(&brush);
    return result;
}

static uint32 bridge32_ExtSelectClipRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)ExtSelectClipRgn((ULONG)a0, (ULONG)a1, (LONG)a2);
    return result;
}

static uint32 bridge32_FillRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)FillRgn((ULONG)a0, (ULONG)a1, (ULONG)a2);
    return result;
}

static uint32 bridge32_FrameRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)FrameRgn((ULONG)a0, (ULONG)a1, (ULONG)a2, (LONG)a3, (LONG)a4);
    return result;
}

static uint32 bridge32_GetBitmapBits(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, (uint64)(a1), LX_GUEST_WRITE, TRUE, &p2);
    if (valid)
        result = (uint32)GetBitmapBits((ULONG)a0, (LONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_GetBkColor(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)GetBkColor((ULONG)a0);
    return result;
}

static uint32 bridge32_GetBkMode(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)GetBkMode((ULONG)a0);
    return result;
}

static uint32 bridge32_GetCharABCWidths(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3,
                                    (uint64)((a2 >= a1 && a2 <= UINT8_MAX)
                                                 ? ((a2 - a1 + 1) * (uint64)WIN32_ABC_SIZE)
                                                 : UINT64_MAX),
                                    LX_GUEST_WRITE, FALSE, &p3);
    if (valid)
        result = (uint32)GetCharABCWidths((ULONG)a0, (ULONG)a1, (ULONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_GetCharWidth(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3,
                                    (uint64)((a2 >= a1 && a2 <= UINT8_MAX)
                                                 ? ((a2 - a1 + 1) * (uint64)WIN32_DWORD_SIZE)
                                                 : UINT64_MAX),
                                    LX_GUEST_WRITE, FALSE, &p3);
    if (valid)
        result = (uint32)GetCharWidth((ULONG)a0, (ULONG)a1, (ULONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_GetClipRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)GetClipRgn((ULONG)a0, (ULONG)a1);
    return result;
}

static uint32 bridge32_GetCurrentObject(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)GetCurrentObject((ULONG)a0, (ULONG)a1);
    return result;
}

static uint32 bridge32_GetCurrentPositionEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_POINT_SIZE, LX_GUEST_WRITE, FALSE, &p1);
    if (valid)
        result = (uint32)GetCurrentPositionEx((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_GetEnhMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    char *s0 = a0 ? lx_execution_copy_string(GProcessState.execution, a0, GRAPHICS_NAME_MAX) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a0 && !s0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        valid = FALSE;
    }
    if (valid)
        result = (uint32)GetEnhMetaFile(s0);
    free(s0);
    return result;
}

static uint32 bridge32_GetEnhMetaFileBits(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, (uint64)(a1), LX_GUEST_WRITE, TRUE, &p2);
    if (valid)
        result = (uint32)GetEnhMetaFileBits((ULONG)a0, (ULONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_GetEnhMetaFileHeader(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, (uint64)(a1), LX_GUEST_WRITE, TRUE, &p2);
    if (valid)
        result = (uint32)GetEnhMetaFileHeader((ULONG)a0, (ULONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_GetKerningPairs(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, (uint64)(a1 * (uint64)WIN32_KERNINGPAIR_SIZE),
                                    LX_GUEST_WRITE, TRUE, &p2);
    if (valid)
        result = (uint32)GetKerningPairs((ULONG)a0, (ULONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_GetMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    char *s0 = a0 ? lx_execution_copy_string(GProcessState.execution, a0, GRAPHICS_NAME_MAX) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a0 && !s0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        valid = FALSE;
    }
    if (valid)
        result = (uint32)GetMetaFile(s0);
    free(s0);
    return result;
}

static uint32 bridge32_GetOutlineTextMetrics(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, (uint64)(a1), LX_GUEST_WRITE, TRUE, &p2);
    if (valid)
        result = (uint32)GetOutlineTextMetrics((ULONG)a0, (ULONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_GetPolyFillMode(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)GetPolyFillMode((ULONG)a0);
    return result;
}

static uint32 bridge32_GetSystemPaletteEntries(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, (uint64)(a2 * (uint64)WIN32_PALETTEENTRY_SIZE),
                                    LX_GUEST_WRITE, TRUE, &p3);
    if (valid)
        result = (uint32)GetSystemPaletteEntries((ULONG)a0, (ULONG)a1, (ULONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_GetTextColor(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)GetTextColor((ULONG)a0);
    return result;
}

static uint32 bridge32_GetViewportExtEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_POINT_SIZE, LX_GUEST_WRITE, FALSE, &p1);
    if (valid)
        result = (uint32)GetViewportExtEx((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_GetViewportOrgEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_POINT_SIZE, LX_GUEST_WRITE, FALSE, &p1);
    if (valid)
        result = (uint32)GetViewportOrgEx((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_GetWinMetaFileBits(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, (uint64)(a1), LX_GUEST_WRITE, TRUE, &p2);
    if (valid)
        result =
            (uint32)GetWinMetaFileBits((ULONG)a0, (ULONG)a1, (PVOID)p2.host, (LONG)a3, (ULONG)a4);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_GetWindowDC(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)GetWindowDC((ULONG)a0);
    return result;
}

static uint32 bridge32_GetWindowExtEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_POINT_SIZE, LX_GUEST_WRITE, FALSE, &p1);
    if (valid)
        result = (uint32)GetWindowExtEx((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_GetWindowOrgEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_POINT_SIZE, LX_GUEST_WRITE, FALSE, &p1);
    if (valid)
        result = (uint32)GetWindowOrgEx((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_LineDDA(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)LineDDA((LONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (ULONG)a4, (ULONG)a5);
    return result;
}

static uint32 bridge32_OffsetRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)OffsetRgn((ULONG)a0, (LONG)a1, (LONG)a2);
    return result;
}

static uint32 bridge32_OffsetViewportOrgEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, WIN32_POINT_SIZE, LX_GUEST_WRITE, TRUE, &p3);
    if (valid)
        result = (uint32)OffsetViewportOrgEx((ULONG)a0, (LONG)a1, (LONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_OffsetWindowOrgEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, WIN32_POINT_SIZE, LX_GUEST_WRITE, TRUE, &p3);
    if (valid)
        result = (uint32)OffsetWindowOrgEx((ULONG)a0, (LONG)a1, (LONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_PaintRgn(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)PaintRgn((ULONG)a0, (ULONG)a1);
    return result;
}

static uint32 bridge32_Pie(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    ULONG a6 = lx_guest_load_u32(a + 24);
    ULONG a7 = lx_guest_load_u32(a + 28);
    ULONG a8 = lx_guest_load_u32(a + 32);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)Pie((ULONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4, (LONG)a5, (LONG)a6,
                             (LONG)a7, (LONG)a8);
    return result;
}

static uint32 bridge32_PlayEnhMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, WIN32_RECT_SIZE, LX_GUEST_READ, FALSE, &p2);
    if (valid)
        result = (uint32)PlayEnhMetaFile((ULONG)a0, (ULONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_PtInRegion(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)PtInRegion((ULONG)a0, (LONG)a1, (LONG)a2);
    return result;
}

static uint32 bridge32_RectInRegion(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_RECT_SIZE, LX_GUEST_READ, FALSE, &p1);
    if (valid)
        result = (uint32)RectInRegion((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_RectVisible(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, WIN32_RECT_SIZE, LX_GUEST_READ, FALSE, &p1);
    if (valid)
        result = (uint32)RectVisible((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_ResetDC(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)ResetDC((ULONG)a0, (PVOID)(uintptr_t)a1);
    return result;
}

static uint32 bridge32_ResizePalette(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)ResizePalette((ULONG)a0, (ULONG)a1);
    return result;
}

static uint32 bridge32_RoundRect(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    ULONG a6 = lx_guest_load_u32(a + 24);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)RoundRect((ULONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4, (LONG)a5,
                                   (LONG)a6);
    return result;
}

static uint32 bridge32_ScaleViewportExtEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    O32GraphicsBuffer p5 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a5, WIN32_POINT_SIZE, LX_GUEST_WRITE, TRUE, &p5);
    if (valid)
        result = (uint32)ScaleViewportExtEx((ULONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4,
                                            (PVOID)p5.host);
    o32_graphics_release(&p5);
    return result;
}

static uint32 bridge32_ScaleWindowExtEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    O32GraphicsBuffer p5 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a5, WIN32_POINT_SIZE, LX_GUEST_WRITE, TRUE, &p5);
    if (valid)
        result = (uint32)ScaleWindowExtEx((ULONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4,
                                          (PVOID)p5.host);
    o32_graphics_release(&p5);
    return result;
}

static uint32 bridge32_ScrollDC(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    ULONG a6 = lx_guest_load_u32(a + 24);
    O32GraphicsBuffer p3 = {0};
    O32GraphicsBuffer p4 = {0};
    O32GraphicsBuffer p6 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p3);
    if (valid)
        valid = o32_graphics_buffer(a4, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p4);
    if (valid)
        valid = o32_graphics_buffer(a6, WIN32_RECT_SIZE, LX_GUEST_WRITE, TRUE, &p6);
    if (valid)
        result = (uint32)ScrollDC((ULONG)a0, (LONG)a1, (LONG)a2, (PVOID)p3.host, (PVOID)p4.host,
                                  (ULONG)a5, (PVOID)p6.host);
    o32_graphics_release(&p6);
    o32_graphics_release(&p4);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_ScrollWindow(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    O32GraphicsBuffer p3 = {0};
    O32GraphicsBuffer p4 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p3);
    if (valid)
        valid = o32_graphics_buffer(a4, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p4);
    if (valid)
        result =
            (uint32)ScrollWindow((ULONG)a0, (LONG)a1, (LONG)a2, (PVOID)p3.host, (PVOID)p4.host);
    o32_graphics_release(&p4);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_ScrollWindowEx(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    ULONG a6 = lx_guest_load_u32(a + 24);
    ULONG a7 = lx_guest_load_u32(a + 28);
    O32GraphicsBuffer p3 = {0};
    O32GraphicsBuffer p4 = {0};
    O32GraphicsBuffer p6 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p3);
    if (valid)
        valid = o32_graphics_buffer(a4, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p4);
    if (valid)
        valid = o32_graphics_buffer(a6, WIN32_RECT_SIZE, LX_GUEST_WRITE, TRUE, &p6);
    if (valid)
        result = (uint32)ScrollWindowEx((ULONG)a0, (LONG)a1, (LONG)a2, (PVOID)p3.host,
                                        (PVOID)p4.host, (ULONG)a5, (PVOID)p6.host, (ULONG)a7);
    o32_graphics_release(&p6);
    o32_graphics_release(&p4);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_SetBitmapBits(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a2, (uint64)(a1), LX_GUEST_READ, FALSE, &p2);
    if (valid)
        result = (uint32)SetBitmapBits((ULONG)a0, (ULONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_SetEnhMetaFileBits(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    O32GraphicsBuffer p1 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, (uint64)(a0), LX_GUEST_READ, FALSE, &p1);
    if (valid)
        result = (uint32)SetEnhMetaFileBits((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_SetPaletteEntries(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a3, (uint64)(a2 * (uint64)WIN32_PALETTEENTRY_SIZE),
                                    LX_GUEST_READ, FALSE, &p3);
    if (valid)
        result = (uint32)SetPaletteEntries((ULONG)a0, (ULONG)a1, (ULONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    return result;
}

static uint32 bridge32_SetPixel(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)SetPixel((ULONG)a0, (LONG)a1, (LONG)a2, (ULONG)a3);
    return result;
}

static uint32 bridge32_SetPolyFillMode(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)SetPolyFillMode((ULONG)a0, (LONG)a1);
    return result;
}

static uint32 bridge32_SetStretchBltMode(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)SetStretchBltMode((ULONG)a0, (LONG)a1);
    return result;
}

static uint32 bridge32_SetTextCharacterExtra(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)SetTextCharacterExtra((ULONG)a0, (LONG)a1);
    return result;
}

static uint32 bridge32_SetTextJustification(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)SetTextJustification((ULONG)a0, (LONG)a1, (LONG)a2);
    return result;
}

static uint32 bridge32_SetWinMetaFileBits(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    O32GraphicsBuffer p1 = {0};
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a1, (uint64)(a0), LX_GUEST_READ, FALSE, &p1);
    if (valid)
        valid = o32_graphics_buffer(a3, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p3);
    if (valid)
        result = (uint32)SetWinMetaFileBits((ULONG)a0, (PVOID)p1.host, (ULONG)a2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    o32_graphics_release(&p1);
    return result;
}

static uint32 bridge32_StretchBlt(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    ULONG a5 = lx_guest_load_u32(a + 20);
    ULONG a6 = lx_guest_load_u32(a + 24);
    ULONG a7 = lx_guest_load_u32(a + 28);
    ULONG a8 = lx_guest_load_u32(a + 32);
    ULONG a9 = lx_guest_load_u32(a + 36);
    ULONG a10 = lx_guest_load_u32(a + 40);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)StretchBlt((ULONG)a0, (LONG)a1, (LONG)a2, (LONG)a3, (LONG)a4, (ULONG)a5,
                                    (LONG)a6, (LONG)a7, (LONG)a8, (LONG)a9, (ULONG)a10);
    return result;
}

static uint32 bridge32_StrokePath(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)StrokePath((ULONG)a0);
    return result;
}

static uint32 bridge32_TabbedTextOut(uint8 *a)
{
    ULONG address = lx_guest_load_u32(a + 12), count = lx_guest_load_u32(a + 16),
          tab_count = lx_guest_load_u32(a + 20);
    O32GraphicsBuffer text = {0}, tabs = {0};
    uint32 result = 0;
    /* A count of -1 measures the zero-terminated string. */
    if (count == UINT32_MAX)
    {
        char *copy = address ? lx_execution_copy_string(GProcessState.execution, address,
                                                        OPEN32_ELEMENT_COUNT_MAX + 1)
                             : NULL;
        count = copy ? (ULONG)strlen(copy) : UINT32_MAX;
        free(copy);
    }
    if (o32_graphics_buffer(address, count <= OPEN32_ELEMENT_COUNT_MAX ? count : UINT64_MAX,
                            LX_GUEST_READ, FALSE, &text) &&
        o32_graphics_buffer(lx_guest_load_u32(a + 24),
                            tab_count <= OPEN32_ELEMENT_COUNT_MAX
                                ? tab_count * (uint64)WIN32_DWORD_SIZE
                                : UINT64_MAX,
                            LX_GUEST_READ, TRUE, &tabs))
        result =
            (uint32)TabbedTextOut(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                  (LONG)lx_guest_load_u32(a + 8), (PCHAR)text.host, (LONG)count,
                                  (LONG)tab_count, tabs.host, (LONG)lx_guest_load_u32(a + 28));
    o32_graphics_release(&tabs);
    o32_graphics_release(&text);
    return result;
}

static uint32 bridge32_UnrealizeObject(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)UnrealizeObject((ULONG)a0);
    return result;
}

static uint32 bridge32_WindowFromDC(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)WindowFromDC((ULONG)a0);
    return result;
}

static uint32 bridge32_HPSToHDC(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    char *s2 = a2 ? lx_execution_copy_string(GProcessState.execution, a2, GRAPHICS_NAME_MAX) : NULL;
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a2 && !s2)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        valid = FALSE;
    }
    if (valid)
        valid = o32_graphics_buffer(a3, WIN32_RECT_SIZE, LX_GUEST_READ, TRUE, &p3);
    if (valid)
        result = (uint32)HPSToHDC((ULONG)a0, (ULONG)a1, s2, (PVOID)p3.host);
    o32_graphics_release(&p3);
    free(s2);
    return result;
}

static uint32 bridge32_DeleteHDCObjects(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
    {
        DeleteHDCObjects((ULONG)a0);
    }
    return result;
}

static uint32 bridge32_SetAbortProc(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)SetAbortProc((ULONG)a0, (ULONG)a1);
    return result;
}

static uint32 bridge32__CreateBitmapIndirect(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    O32GraphicsBuffer p0 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(a0, WIN32_BITMAP_SIZE, LX_GUEST_READ, FALSE, &p0);
    if (valid)
        result = (uint32)_CreateBitmapIndirect((PVOID)p0.host);
    o32_graphics_release(&p0);
    return result;
}

static uint32 bridge32_WinTranslateDeviceRects(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    ULONG a3 = lx_guest_load_u32(a + 12);
    ULONG a4 = lx_guest_load_u32(a + 16);
    O32GraphicsBuffer p2 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        valid = o32_graphics_buffer(
            a2,
            (uint64)(a3 <= OPEN32_ELEMENT_COUNT_MAX ? a3 * (uint64)WIN32_RECT_SIZE : UINT64_MAX),
            LX_GUEST_READ | LX_GUEST_WRITE, FALSE, &p2);
    if (valid)
        result = (uint32)WinTranslateDeviceRects((ULONG)a0, (ULONG)a1, (PVOID)p2.host, (LONG)a3,
                                                 (ULONG)a4);
    o32_graphics_release(&p2);
    return result;
}

static uint32 bridge32_WinTranslateGraphicsObjectHandle(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    ULONG a1 = lx_guest_load_u32(a + 4);
    ULONG a2 = lx_guest_load_u32(a + 8);
    uint32 result = 0;
    BOOL valid = TRUE;
    if (valid)
        result = (uint32)WinTranslateGraphicsObjectHandle((ULONG)a0, (ULONG)a1, (ULONG)a2);
    return result;
}

static BOOL o32_graphics_dib(ULONG address, ULONG start, ULONG count, ULONG usage,
                             O32GraphicsBuffer *info, size_t *pixel_bytes)
{
    uint8 header[WIN32_BITMAPINFOHEADER_SIZE];
    size_t info_bytes = 0, expected_pixels = 0;
    if (!lx_execution_read(GProcessState.execution, address, header, sizeof header))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!open32DibBuffers(header, start, count, usage, &info_bytes, &expected_pixels) ||
        !o32_graphics_buffer(address, info_bytes, LX_GUEST_READ, FALSE, info))
        return FALSE;
    size_t actual_info = 0, actual_pixels = 0;
    if (!open32DibBuffers(info->host, start, count, usage, &actual_info, &actual_pixels) ||
        actual_info != info_bytes || actual_pixels != expected_pixels)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *pixel_bytes = actual_pixels;
    return TRUE;
}

static uint32 bridge32_CreateDIBitmap(uint8 *a)
{
    ULONG dc = lx_guest_load_u32(a), options = lx_guest_load_u32(a + 8),
          usage = lx_guest_load_u32(a + 20);
    O32GraphicsBuffer header = {0}, info = {0}, bits = {0};
    size_t pixels = 0;
    ULONG result = 0;
    BOOL valid = o32_graphics_buffer(lx_guest_load_u32(a + 4), WIN32_BITMAPINFOHEADER_SIZE,
                                     LX_GUEST_READ, FALSE, &header);
    /* CBM_INIT without bits creates an uninitialized bitmap. */
    if (!lx_guest_load_u32(a + 12))
        options &= ~(ULONG)WIN32_CBM_INIT;
    if (valid && (options & WIN32_CBM_INIT))
        valid = o32_graphics_dib(lx_guest_load_u32(a + 16), 0, UINT32_MAX, usage, &info, &pixels) &&
                o32_graphics_buffer(lx_guest_load_u32(a + 12), pixels, LX_GUEST_READ, FALSE, &bits);
    if (valid)
        result = CreateDIBitmap(dc, header.host, options, bits.host, info.host, usage);
    o32_graphics_release(&bits);
    o32_graphics_release(&info);
    o32_graphics_release(&header);
    return result;
}

static uint32 bridge32_SetDIBitsToDevice(uint8 *a)
{
    O32GraphicsBuffer info = {0}, bits = {0};
    size_t pixels = 0;
    ULONG result = 0, start = lx_guest_load_u32(a + 28), count = lx_guest_load_u32(a + 32),
          usage = lx_guest_load_u32(a + 44);
    if (o32_graphics_dib(lx_guest_load_u32(a + 40), start, count, usage, &info, &pixels) &&
        o32_graphics_buffer(lx_guest_load_u32(a + 36), pixels, LX_GUEST_READ, FALSE, &bits))
        result = (ULONG)SetDIBitsToDevice(
            lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4), (LONG)lx_guest_load_u32(a + 8),
            (LONG)lx_guest_load_u32(a + 12), (LONG)lx_guest_load_u32(a + 16),
            (LONG)lx_guest_load_u32(a + 20), (LONG)lx_guest_load_u32(a + 24), start, count,
            bits.host, info.host, usage);
    o32_graphics_release(&bits);
    o32_graphics_release(&info);
    return result;
}

static uint32 bridge32_CreateIcon(uint8 *a)
{
    LONG width = (LONG)lx_guest_load_u32(a + 4), height = (LONG)lx_guest_load_u32(a + 8);
    ULONG planes = lx_guest_load_u32(a + 12) & UINT8_MAX,
          depth = lx_guest_load_u32(a + 16) & UINT8_MAX;
    ULONG color_address = lx_guest_load_u32(a + 24);
    size_t mask_size = 0;
    O32GraphicsBuffer mask = {0}, pixels = {0};
    ULONG result = 0;
    /* Both planes use DWORD-aligned rows; color data is read only below 16 bits per pel. */
    if (!color_address || planes * depth >= 16)
    {
        planes = depth = 1;
        color_address = 0;
    }
    if (open32BitmapBytes(width, height, 1, 1, &mask_size))
    {
        uint64 mask_bytes = ((uint64)width + 31) / 32 * 4 * (uint64)height;
        uint64 color_bytes = ((uint64)width * planes * depth + 31) / 32 * 4 * (uint64)height;
        if (o32_graphics_buffer(lx_guest_load_u32(a + 20), mask_bytes, LX_GUEST_READ, FALSE,
                                &mask) &&
            o32_graphics_buffer(color_address, color_bytes, LX_GUEST_READ, TRUE, &pixels))
            result = CreateIcon(lx_guest_load_u32(a), width, height, planes, depth, mask.host,
                                pixels.host);
    }
    o32_graphics_release(&pixels);
    o32_graphics_release(&mask);
    return result;
}

enum
{
    GRAPHICS_POLY_POLYGON_REGION,
    GRAPHICS_POLY_POLYGON,
    GRAPHICS_POLY_POLYLINE
};

static uint32 o32_graphics_polygons(uint8 *a, unsigned kind)
{
    BOOL region = kind == GRAPHICS_POLY_POLYGON_REGION;
    ULONG counts_address = lx_guest_load_u32(a + (region ? 4 : 8)),
          polygons = lx_guest_load_u32(a + (region ? 8 : 12)),
          points_address = lx_guest_load_u32(a + (region ? 0 : 4));
    O32GraphicsBuffer counts = {0}, points = {0};
    ULONG result = 0;
    uint64 total = 0;
    BOOL valid = polygons <= OPEN32_ELEMENT_COUNT_MAX &&
                 o32_graphics_buffer(counts_address, (uint64)polygons * WIN32_DWORD_SIZE,
                                     LX_GUEST_READ, FALSE, &counts);
    if (!valid)
        open32SetError(ERROR_INVALID_PARAMETER);
    if (valid)
    {
        for (ULONG i = 0; i < polygons; i++)
        {
            ULONG count = lx_guest_load_u32((uint8 *)counts.host + i * WIN32_DWORD_SIZE);
            if (count > OPEN32_ELEMENT_COUNT_MAX || total + count > OPEN32_ELEMENT_COUNT_MAX)
            {
                valid = FALSE;
                open32SetError(ERROR_INVALID_PARAMETER);
                break;
            }
            total += count;
        }
    }
    if (valid)
        valid = o32_graphics_buffer(points_address, total * WIN32_POINT_SIZE, LX_GUEST_READ, FALSE,
                                    &points);
    if (valid)
    {
        if (region)
            result = CreatePolyPolygonRgn(points.host, counts.host, (LONG)polygons,
                                          (LONG)lx_guest_load_u32(a + 12));
        else if (kind == GRAPHICS_POLY_POLYGON)
            result = PolyPolygon(lx_guest_load_u32(a), points.host, counts.host, (LONG)polygons);
        else
            result = PolyPolyline(lx_guest_load_u32(a), points.host, counts.host, polygons);
    }
    o32_graphics_release(&points);
    o32_graphics_release(&counts);
    return result;
}

static uint32 bridge32_CreatePolyPolygonRgn(uint8 *a)
{
    return o32_graphics_polygons(a, GRAPHICS_POLY_POLYGON_REGION);
}

static uint32 bridge32_PolyPolygon(uint8 *a)
{
    return o32_graphics_polygons(a, GRAPHICS_POLY_POLYGON);
}

static uint32 bridge32_PolyPolyline(uint8 *a)
{
    return o32_graphics_polygons(a, GRAPHICS_POLY_POLYLINE);
}

static uint32 bridge32_PlayEnhMetaFileRecord(uint8 *a)
{
    uint8 header[WIN32_EMR_SIZE];
    ULONG address = lx_guest_load_u32(a + 8), count = lx_guest_load_u32(a + 12), size = 0,
          result = 0;
    O32GraphicsBuffer record = {0}, table = {0};
    if (lx_execution_read(GProcessState.execution, address, header, sizeof header))
        size = lx_guest_load_u32(header + WIN32_EMR_BYTES);
    /* Records are DWORD aligned; the handle table size comes from ENHMETAHEADER.nHandles, a
       WORD. */
    if (size < WIN32_EMR_SIZE || (size & 3) || count > UINT16_MAX)
        open32SetError(ERROR_INVALID_PARAMETER);
    else if (o32_graphics_buffer(address, size, LX_GUEST_READ, FALSE, &record) &&
             lx_guest_load_u32((uint8 *)record.host + WIN32_EMR_BYTES) == size &&
             o32_graphics_buffer(lx_guest_load_u32(a + 4), (uint64)count * WIN32_DWORD_SIZE,
                                 LX_GUEST_READ | LX_GUEST_WRITE, count == 0, &table))
        result = PlayEnhMetaFileRecord(lx_guest_load_u32(a), table.host, record.host, count);
    o32_graphics_release(&table);
    o32_graphics_release(&record);
    return result;
}

static uint32 bridge32_Escape(uint8 *a)
{
    LONG code = (LONG)lx_guest_load_u32(a + 4), count = (LONG)lx_guest_load_u32(a + 8);
    O32GraphicsBuffer input = {0}, output = {0};
    ULONG result = 0;
    /* SETABORTPROC passes the procedure itself in the input pointer. */
    if (code == WIN32_SETABORTPROC)
        return (ULONG)SetAbortProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 12));
    if (count < 0 || count > OPEN32_ELEMENT_COUNT_MAX)
        open32SetError(ERROR_INVALID_PARAMETER);
    else if (o32_graphics_buffer(lx_guest_load_u32(a + 12), (ULONG)count, LX_GUEST_READ, count == 0,
                                 &input))
    {
        /* Word-sized and dword-sized inputs get outputs of the same width. */
        size_t output_bytes =
            code == WIN32_GETPHYSPAGESIZE || code == WIN32_GETPRINTINGOFFSET ||
                    code == WIN32_GETSCALINGFACTOR
                ? WIN32_POINT_SIZE
            : code == WIN32_SETCOPYCOUNT || code == WIN32_SETMITERLIMIT
                ? (size_t)(count == WIN32_WORD_SIZE ? WIN32_WORD_SIZE : WIN32_DWORD_SIZE)
            : code == WIN32_GETTECHNOLOGY ? ESCAPE_TECHNOLOGY_SIZE
            /* BINS: the current bin and the bin count, as WORDs */
            : code == WIN32_GETSETPAPERBINS ? 2 * WIN32_WORD_SIZE
                                            : 0;
        if (code == WIN32_ENUMPAPERBINS && (count == WIN32_WORD_SIZE || count == WIN32_DWORD_SIZE))
        {
            LONG bins = count == WIN32_DWORD_SIZE ? (LONG)lx_guest_load_u32(input.host)
                                                  : (SHORT)lx_guest_load_u16(input.host);
            /* A WORD bin ID and a fixed-width name per bin */
            output_bytes = bins > 0 && bins <= ESCAPE_PAPER_BINS_MAX
                               ? (size_t)bins * (WIN32_WORD_SIZE + DEVICE_BIN_NAME_SIZE)
                               : 0;
        }
        /* The input is the first and last character; the output is a WORD width for each. */
        if (code == WIN32_GETEXTENTTABLE && count == 2)
        {
            const uint8 *range = input.host;
            output_bytes =
                range[1] >= range[0] ? ((size_t)range[1] - range[0] + 1) * WIN32_WORD_SIZE : 0;
        }
        if (o32_graphics_buffer(lx_guest_load_u32(a + 16), output_bytes, LX_GUEST_WRITE, TRUE,
                                &output))
            result = (ULONG)Escape(lx_guest_load_u32(a), code, count, input.host, output.host);
    }
    o32_graphics_release(&output);
    o32_graphics_release(&input);
    return result;
}

static uint32 bridge32_DeviceCapabilities(uint8 *a)
{
    ULONG device = lx_guest_load_u32(a), port = lx_guest_load_u32(a + 4),
          capability = lx_guest_load_u32(a + 8) & UINT16_MAX;
    char *device_name =
             device ? lx_execution_copy_string(GProcessState.execution, device, GRAPHICS_NAME_MAX)
                    : NULL,
         *port_name =
             port ? lx_execution_copy_string(GProcessState.execution, port, GRAPHICS_NAME_MAX)
                  : NULL;
    O32GraphicsBuffer output = {0};
    ULONG result = UINT32_MAX;
    size_t size = capability == WIN32_DC_PAPERS      ? DEVICE_PAPER_COUNT * WIN32_WORD_SIZE
                  : capability == WIN32_DC_BINS      ? DEVICE_BIN_COUNT * WIN32_WORD_SIZE
                  : capability == WIN32_DC_PAPERSIZE ? DEVICE_PAPER_COUNT * WIN32_POINT_SIZE
                  /* one horizontal and vertical resolution pair */
                  : capability == WIN32_DC_ENUMRESOLUTIONS ? 2 * WIN32_DWORD_SIZE
                  : capability == WIN32_DC_BINNAMES        ? DEVICE_BIN_COUNT * DEVICE_BIN_NAME_SIZE
                  : capability == WIN32_DC_PAPERNAMES ? DEVICE_PAPER_COUNT * DEVICE_PAPER_NAME_SIZE
                                                      : 0;
    if ((!device || device_name) && (!port || port_name) &&
        o32_graphics_buffer(lx_guest_load_u32(a + 12), size, LX_GUEST_WRITE, TRUE, &output))
        result = DeviceCapabilities(device_name, port_name, capability, output.host,
                                    (PVOID)(uintptr_t)lx_guest_load_u32(a + 16));
    else
        open32SetError(ERROR_INVALID_PARAMETER);
    o32_graphics_release(&output);
    free(device_name);
    free(port_name);
    return result;
}
#endif
