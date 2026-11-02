/* Public domain. No rights reserved. Wholly AI-authored. */
/* Saved DCs retain selected objects and independent clip copies until restored or released. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
typedef struct Open32SavedDC
{
    Open32DC state;
    LxHandleBorrow bitmap;
    ULONG bitmap_handle;
    LONG level, native_id;
    struct Open32SavedDC *next;
} Open32SavedDC;
static void release_state(Open32SavedDC *saved)
{
    lx_handle_release(&saved->state.font);
    lx_handle_release(&saved->state.brush);
    lx_handle_release(&saved->state.pen);
    lx_handle_release(&saved->state.palette);
    lx_handle_release(&saved->bitmap);
    if (saved->state.clip_region)
        GpiDestroyRegion(saved->state.ps, saved->state.clip_region);
    free(saved);
}
LONG SaveDC(ULONG handle)
{
    TRACE_NATIVE("SaveDC(%u)", (uint)handle);
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(6);
        return 0;
    }
    Open32SavedDC *saved = calloc(1, sizeof *saved);
    if (!saved)
        goto memory_error;
    saved->state = *dc;
    saved->state.font = (LxHandleBorrow){0};
    saved->state.brush = (LxHandleBorrow){0};
    saved->state.pen = (LxHandleBorrow){0};
    saved->state.palette = (LxHandleBorrow){0};
    saved->state.clip_region = 0;
    if (!lx_handle_borrow(dc->font_handle, LX_HANDLE_OPEN32_FONT, &saved->state.font) ||
        !lx_handle_borrow(dc->brush_handle, LX_HANDLE_OPEN32_BRUSH, &saved->state.brush) ||
        !lx_handle_borrow(dc->pen_handle, LX_HANDLE_OPEN32_PEN, &saved->state.pen) ||
        !lx_handle_borrow(dc->palette_handle, LX_HANDLE_OPEN32_PALETTE, &saved->state.palette))
        goto failed;
    PresentationSpace *ps = lx_pm_font_space(handle);
    PmBitmap *bitmap = ps ? ps->bitmap_lease.resource : NULL;
    if (bitmap)
    {
        saved->bitmap_handle = bitmap->handle;
        if (!lx_handle_borrow(bitmap->handle, LX_HANDLE_PM_BITMAP, &saved->bitmap))
            goto failed;
    }
    if (dc->clip_region)
    {
        saved->state.clip_region = GpiCreateRegion(handle, 0, NULL);
        if (!saved->state.clip_region ||
            !GpiCombineRegion(handle, saved->state.clip_region, dc->clip_region, 0, 2))
            goto failed;
    }
    if (dc->saved && dc->saved->level == INT32_MAX)
        goto failed;
    saved->level = dc->saved ? dc->saved->level + 1 : 1;
    saved->native_id = GpiSavePS(handle);
    if (saved->native_id < 1)
        goto failed;
    saved->next = dc->saved;
    dc->saved = saved;
    return saved->level;
failed:
    release_state(saved);
memory_error:
    open32SetError(8);
    return 0;
}
BOOL RestoreDC(ULONG handle, LONG level)
{
    TRACE_NATIVE("RestoreDC(%u, %d)", (uint)handle, (int)level);
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !level || level == INT32_MIN)
    {
        open32SetError(dc ? 87 : 6);
        return FALSE;
    }
    Open32SavedDC *saved = dc->saved;
    LONG relative = level;
    while (saved && (level > 0 ? saved->level != level : ++relative != 0))
        saved = saved->next;
    if (!saved)
    {
        open32SetError(87);
        return FALSE;
    }
    if (dc->kind == 1 && GpiSetBitmap(handle, saved->bitmap_handle) == UINT32_MAX)
    {
        open32SetError(170);
        return FALSE;
    }
    if (!GpiRestorePS(handle, saved->native_id))
        return FALSE;
    if (dc->clip_region)
    {
        GpiSetClipRegion(handle, 0, NULL);
        GpiDestroyRegion(handle, dc->clip_region);
    }
    lx_handle_release(&dc->font);
    lx_handle_release(&dc->brush);
    lx_handle_release(&dc->pen);
    lx_handle_release(&dc->palette);
    Open32SavedDC *end = saved->next, *top = dc->saved;
    Open32DC *next = dc->next;
    *dc = saved->state;
    dc->next = next;
    dc->saved = end;
    saved->state.font = (LxHandleBorrow){0};
    saved->state.brush = (LxHandleBorrow){0};
    saved->state.pen = (LxHandleBorrow){0};
    saved->state.palette = (LxHandleBorrow){0};
    saved->state.clip_region = 0;
    if (dc->clip_region)
        GpiSetClipRegion(handle, dc->clip_region, NULL);
    /* Open32 replaces logical font set1 when selecting a different font. */
    BOOL restored = open32SelectFont(dc, dc->font_handle) != 0;
    while (top != end)
    {
        Open32SavedDC *discard = top;
        top = discard->next;
        release_state(discard);
    }
    return restored;
}
void open32SavedDCCleanup(Open32DC *dc)
{
    Open32SavedDC *oldest = dc->saved;
    while (oldest && oldest->next)
        oldest = oldest->next;
    if (oldest)
        GpiRestorePS(dc->ps, oldest->native_id);
    while (dc->saved)
    {
        Open32SavedDC *saved = dc->saved;
        dc->saved = saved->next;
        release_state(saved);
    }
}
