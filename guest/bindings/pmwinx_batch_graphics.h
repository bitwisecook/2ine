/* Public domain. No rights reserved. Wholly AI-authored. */
/* Checked graphics ABI: stack32 words, byte snapshots, no host SDK structures. */
#ifndef TWOINE_GUEST_PMWINX_BATCH_GRAPHICS_H
#define TWOINE_GUEST_PMWINX_BATCH_GRAPHICS_H
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
        open32SetError(87);
        return FALSE;
    }
    if (size > 64u * 1024 * 1024)
    {
        open32SetError(87);
        return FALSE;
    }
    if (!size)
    {
        out->copy = calloc(1, 1);
        out->host = out->copy;
        return out->host != NULL;
    }
    if (!lx_execution_borrow(GLoaderState.execution, address, (size_t)size, mode, &out->lease))
    {
        open32SetError(87);
        return FALSE;
    }
    if (mode == LX_GUEST_READ)
    {
        out->copy = malloc((size_t)size);
        if (!out->copy)
        {
            open32SetError(8);
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
        valid = o32_graphics_buffer(a3, (uint64)(a2 * 4ull), LX_GUEST_READ, FALSE, &p3);
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
    char *s1 = a1 ? lx_execution_copy_string(GLoaderState.execution, a1, 4096) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a1 && !s1)
    {
        open32SetError(87);
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
        valid = o32_graphics_buffer(a0, (uint64)(12), LX_GUEST_READ, FALSE, &p0);
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
        valid = o32_graphics_buffer(a0, (uint64)(16), LX_GUEST_READ, FALSE, &p0);
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
    char *s0 = a0 ? lx_execution_copy_string(GLoaderState.execution, a0, 4096) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a0 && !s0)
    {
        open32SetError(87);
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
        valid = o32_graphics_buffer(a0, (uint64)(a1 <= 65536 ? a1 * 8ull : UINT64_MAX),
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
        valid = o32_graphics_buffer(a1, (uint64)(16), LX_GUEST_READ, FALSE, &p1);
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
        valid = o32_graphics_buffer(a4, (uint64)(16), LX_GUEST_READ, TRUE, &p4);
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
    char *s1 = a1 ? lx_execution_copy_string(GLoaderState.execution, a1, 4096) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a1 && !s1)
    {
        open32SetError(87);
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
        valid = o32_graphics_buffer(
            a3, (uint64)((a2 >= a1 && a2 <= 255) ? ((a2 - a1 + 1) * 12ull) : UINT64_MAX),
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
        valid = o32_graphics_buffer(
            a3, (uint64)((a2 >= a1 && a2 <= 255) ? ((a2 - a1 + 1) * 4ull) : UINT64_MAX),
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
        valid = o32_graphics_buffer(a1, (uint64)(8), LX_GUEST_WRITE, FALSE, &p1);
    if (valid)
        result = (uint32)GetCurrentPositionEx((ULONG)a0, (PVOID)p1.host);
    o32_graphics_release(&p1);
    return result;
}
static uint32 bridge32_GetEnhMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    char *s0 = a0 ? lx_execution_copy_string(GLoaderState.execution, a0, 4096) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a0 && !s0)
    {
        open32SetError(87);
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
        valid = o32_graphics_buffer(a2, (uint64)(a1 * 8ull), LX_GUEST_WRITE, TRUE, &p2);
    if (valid)
        result = (uint32)GetKerningPairs((ULONG)a0, (ULONG)a1, (PVOID)p2.host);
    o32_graphics_release(&p2);
    return result;
}
static uint32 bridge32_GetMetaFile(uint8 *a)
{
    ULONG a0 = lx_guest_load_u32(a + 0);
    char *s0 = a0 ? lx_execution_copy_string(GLoaderState.execution, a0, 4096) : NULL;
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a0 && !s0)
    {
        open32SetError(87);
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
        valid = o32_graphics_buffer(a3, (uint64)(a2 * 4ull), LX_GUEST_WRITE, TRUE, &p3);
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
        valid = o32_graphics_buffer(a1, (uint64)(8), LX_GUEST_WRITE, FALSE, &p1);
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
        valid = o32_graphics_buffer(a1, (uint64)(8), LX_GUEST_WRITE, FALSE, &p1);
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
        valid = o32_graphics_buffer(a1, (uint64)(8), LX_GUEST_WRITE, FALSE, &p1);
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
        valid = o32_graphics_buffer(a1, (uint64)(8), LX_GUEST_WRITE, FALSE, &p1);
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
        valid = o32_graphics_buffer(a3, (uint64)(8), LX_GUEST_WRITE, TRUE, &p3);
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
        valid = o32_graphics_buffer(a3, (uint64)(8), LX_GUEST_WRITE, TRUE, &p3);
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
        valid = o32_graphics_buffer(a2, (uint64)(16), LX_GUEST_READ, FALSE, &p2);
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
        valid = o32_graphics_buffer(a1, (uint64)(16), LX_GUEST_READ, FALSE, &p1);
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
        valid = o32_graphics_buffer(a1, (uint64)(16), LX_GUEST_READ, FALSE, &p1);
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
        valid = o32_graphics_buffer(a5, (uint64)(8), LX_GUEST_WRITE, TRUE, &p5);
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
        valid = o32_graphics_buffer(a5, (uint64)(8), LX_GUEST_WRITE, TRUE, &p5);
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
        valid = o32_graphics_buffer(a3, (uint64)(16), LX_GUEST_READ, TRUE, &p3);
    if (valid)
        valid = o32_graphics_buffer(a4, (uint64)(16), LX_GUEST_READ, TRUE, &p4);
    if (valid)
        valid = o32_graphics_buffer(a6, (uint64)(16), LX_GUEST_WRITE, TRUE, &p6);
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
        valid = o32_graphics_buffer(a3, (uint64)(16), LX_GUEST_READ, TRUE, &p3);
    if (valid)
        valid = o32_graphics_buffer(a4, (uint64)(16), LX_GUEST_READ, TRUE, &p4);
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
        valid = o32_graphics_buffer(a3, (uint64)(16), LX_GUEST_READ, TRUE, &p3);
    if (valid)
        valid = o32_graphics_buffer(a4, (uint64)(16), LX_GUEST_READ, TRUE, &p4);
    if (valid)
        valid = o32_graphics_buffer(a6, (uint64)(16), LX_GUEST_WRITE, TRUE, &p6);
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
        valid = o32_graphics_buffer(a3, (uint64)(a2 * 4ull), LX_GUEST_READ, FALSE, &p3);
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
        valid = o32_graphics_buffer(a3, (uint64)(16), LX_GUEST_READ, TRUE, &p3);
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
        char *copy =
            address ? lx_execution_copy_string(GLoaderState.execution, address, 65537) : NULL;
        count = copy ? (ULONG)strlen(copy) : UINT32_MAX;
        free(copy);
    }
    if (o32_graphics_buffer(address, count <= 65536 ? count : UINT64_MAX, LX_GUEST_READ, FALSE,
                            &text) &&
        o32_graphics_buffer(lx_guest_load_u32(a + 24),
                            tab_count <= 65536 ? tab_count * 4ull : UINT64_MAX, LX_GUEST_READ, TRUE,
                            &tabs))
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
    char *s2 = a2 ? lx_execution_copy_string(GLoaderState.execution, a2, 4096) : NULL;
    O32GraphicsBuffer p3 = {0};
    uint32 result = 0;
    BOOL valid = TRUE;
    if (a2 && !s2)
    {
        open32SetError(87);
        valid = FALSE;
    }
    if (valid)
        valid = o32_graphics_buffer(a3, (uint64)(16), LX_GUEST_READ, TRUE, &p3);
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
        valid = o32_graphics_buffer(a0, (uint64)(24), LX_GUEST_READ, FALSE, &p0);
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
        valid = o32_graphics_buffer(a2, (uint64)(a3 <= 65536 ? a3 * 16ull : UINT64_MAX),
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
    uint8 header[40];
    size_t info_bytes = 0, expected_pixels = 0;
    if (!lx_execution_read(GLoaderState.execution, address, header, sizeof header))
    {
        open32SetError(87);
        return FALSE;
    }
    if (!open32DibBuffers(header, start, count, usage, &info_bytes, &expected_pixels) ||
        !o32_graphics_buffer(address, info_bytes, LX_GUEST_READ, FALSE, info))
        return FALSE;
    size_t actual_info = 0, actual_pixels = 0;
    if (!open32DibBuffers(info->host, start, count, usage, &actual_info, &actual_pixels) ||
        actual_info != info_bytes || actual_pixels != expected_pixels)
    {
        open32SetError(87);
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
    BOOL valid = o32_graphics_buffer(lx_guest_load_u32(a + 4), 40, LX_GUEST_READ, FALSE, &header);
    /* CBM_INIT without bits creates an uninitialized bitmap. */
    if (!lx_guest_load_u32(a + 12))
        options &= ~4u;
    if (valid && (options & 4))
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
    ULONG planes = lx_guest_load_u32(a + 12) & 255, depth = lx_guest_load_u32(a + 16) & 255;
    /* Without XOR bits the icon is a monochrome AND mask. */
    if (!lx_guest_load_u32(a + 24))
        planes = depth = 1;
    size_t mask_size = 0, pixel_size = 0;
    O32GraphicsBuffer mask = {0}, pixels = {0};
    ULONG result = 0;
    if (open32BitmapBytes(width, height, 1, 1, &mask_size) &&
        open32BitmapBytes(width, height, planes, depth, &pixel_size) &&
        o32_graphics_buffer(lx_guest_load_u32(a + 20), mask_size, LX_GUEST_READ, FALSE, &mask) &&
        o32_graphics_buffer(lx_guest_load_u32(a + 24), pixel_size, LX_GUEST_READ, TRUE, &pixels))
        result =
            CreateIcon(lx_guest_load_u32(a), width, height, planes, depth, mask.host, pixels.host);
    o32_graphics_release(&pixels);
    o32_graphics_release(&mask);
    return result;
}
static uint32 o32_graphics_polygons(uint8 *a, unsigned kind)
{
    ULONG counts_address = lx_guest_load_u32(a + (kind == 0 ? 4 : 8)),
          polygons = lx_guest_load_u32(a + (kind == 0 ? 8 : 12)),
          points_address = lx_guest_load_u32(a + (kind == 0 ? 0 : 4));
    O32GraphicsBuffer counts = {0}, points = {0};
    ULONG result = 0;
    uint64 total = 0;
    BOOL valid = polygons <= 65536 && o32_graphics_buffer(counts_address, (uint64)polygons * 4,
                                                          LX_GUEST_READ, FALSE, &counts);
    if (!valid)
        open32SetError(87);
    if (valid)
    {
        for (ULONG i = 0; i < polygons; i++)
        {
            ULONG count = lx_guest_load_u32((uint8 *)counts.host + i * 4);
            if (count > 65536 || total + count > 65536)
            {
                valid = FALSE;
                open32SetError(87);
                break;
            }
            total += count;
        }
    }
    if (valid)
        valid = o32_graphics_buffer(points_address, total * 8, LX_GUEST_READ, FALSE, &points);
    if (valid)
    {
        if (kind == 0)
            result = CreatePolyPolygonRgn(points.host, counts.host, (LONG)polygons,
                                          (LONG)lx_guest_load_u32(a + 12));
        else if (kind == 1)
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
    return o32_graphics_polygons(a, 0);
}
static uint32 bridge32_PolyPolygon(uint8 *a)
{
    return o32_graphics_polygons(a, 1);
}
static uint32 bridge32_PolyPolyline(uint8 *a)
{
    return o32_graphics_polygons(a, 2);
}
static uint32 bridge32_PlayEnhMetaFileRecord(uint8 *a)
{
    uint8 header[8];
    ULONG address = lx_guest_load_u32(a + 8), count = lx_guest_load_u32(a + 12), size = 0,
          result = 0;
    O32GraphicsBuffer record = {0}, table = {0};
    if (lx_execution_read(GLoaderState.execution, address, header, sizeof header))
        size = lx_guest_load_u32(header + 4);
    if (size < 8 || (size & 3) || count > 65535)
        open32SetError(87);
    else if (o32_graphics_buffer(address, size, LX_GUEST_READ, FALSE, &record) &&
             lx_guest_load_u32((uint8 *)record.host + 4) == size &&
             o32_graphics_buffer(lx_guest_load_u32(a + 4), (uint64)count * 4,
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
    size_t output_bytes = code == 12 || code == 13 || code == 14 ? 8 : 0;
    /* SETABORTPROC passes the procedure itself in the input pointer. */
    if (code == 9)
        result = (ULONG)SetAbortProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 12));
    else if (count < 0 || count > 65536)
        open32SetError(87);
    else if (o32_graphics_buffer(lx_guest_load_u32(a + 12), (ULONG)count, LX_GUEST_READ, count == 0,
                                 &input) &&
             o32_graphics_buffer(lx_guest_load_u32(a + 16), output_bytes, LX_GUEST_WRITE, TRUE,
                                 &output))
        result = (ULONG)Escape(lx_guest_load_u32(a), code, count, input.host, output.host);
    o32_graphics_release(&output);
    o32_graphics_release(&input);
    return result;
}
static uint32 bridge32_DeviceCapabilities(uint8 *a)
{
    ULONG device = lx_guest_load_u32(a), port = lx_guest_load_u32(a + 4),
          capability = lx_guest_load_u32(a + 8) & 65535;
    char *device_name =
             device ? lx_execution_copy_string(GLoaderState.execution, device, 4096) : NULL,
         *port_name = port ? lx_execution_copy_string(GLoaderState.execution, port, 4096) : NULL;
    O32GraphicsBuffer output = {0};
    ULONG result = UINT32_MAX;
    size_t size = capability == 2 || capability == 6    ? 2
                  : capability == 3 || capability == 13 ? 8
                  : capability == 12                    ? 24
                  : capability == 16                    ? 64
                                                        : 0;
    if ((!device || device_name) && (!port || port_name) &&
        o32_graphics_buffer(lx_guest_load_u32(a + 12), size, LX_GUEST_WRITE, TRUE, &output))
        result = DeviceCapabilities(device_name, port_name, capability, output.host,
                                    (PVOID)(uintptr_t)lx_guest_load_u32(a + 16));
    else
        open32SetError(87);
    o32_graphics_release(&output);
    free(device_name);
    free(port_name);
    return result;
}
#endif
