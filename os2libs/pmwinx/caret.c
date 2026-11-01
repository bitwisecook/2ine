/* Public domain. No rights reserved. Wholly AI-authored. */
/* One queue-thread caret uses reversible raster inversion and a private PM timer. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/timers.h"
typedef struct Caret
{
    ULONG owner, bitmap, timer;
    LONG x, y, width, height, visibility;
    unsigned painting;
    BOOL drawn, phase;
    LxHandleBorrow image;
} Caret;
static _Thread_local Caret caret;
static _Thread_local ULONG blink_time = 530;
static BOOL invert_caret(void)
{
    RECTL bounds;
    if (!caret.owner || !WinQueryWindowRect(caret.owner, &bounds))
        return FALSE;
    int64_t left = caret.x, top = caret.y, right = left + caret.width, bottom = top + caret.height;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > bounds.xRight)
        right = bounds.xRight;
    if (bottom > bounds.yTop)
        bottom = bounds.yTop;
    if (right <= left || bottom <= top)
        return TRUE;
    unsigned width = (unsigned)(right - left), height = (unsigned)(bottom - top);
    uint32 *pixels = malloc((size_t)width * height * 4);
    HPS ps = pixels ? WinGetPS(caret.owner) : 0;
    BOOL result =
        ps && lx_pm_raster_read(ps, (LONG)left, bounds.yTop - (LONG)bottom, width, height, pixels);
    PmBitmap *image = caret.image.resource;
    if (result)
    {
        for (unsigned y = 0; y < height; y++)
            for (unsigned x = 0; x < width; x++)
            {
                unsigned local_x = (unsigned)(left - caret.x) + x;
                unsigned local_y = (unsigned)(bottom - caret.y - 1) - y;
                uint32 mask =
                    image ? image->pixels[(size_t)(image->height - 1 - local_y) * image->width +
                                          local_x]
                    : caret.bitmap == 1 && ((local_x + local_y) & 1) ? 0
                                                                     : 0xFFFFFF;
                pixels[(size_t)y * width + x] ^= mask;
            }
        result =
            lx_pm_raster_write(ps, (LONG)left, bounds.yTop - (LONG)bottom, width, height, pixels);
    }
    if (ps)
        WinReleasePS(ps);
    free(pixels);
    return result;
}
static void display(BOOL draw)
{
    if (caret.drawn != draw && invert_caret())
        caret.drawn = draw;
}
static void blink(void)
{
    if (!caret.owner || caret.visibility < 0 || caret.painting)
        return;
    caret.phase = !caret.phase;
    display(caret.phase);
}
BOOL DestroyCaret(void)
{
    if (!caret.owner)
    {
        open32SetError(5);
        return FALSE;
    }
    display(FALSE);
    if (caret.timer)
        lx_pm_timer_stop(caret.timer);
    lx_handle_release(&caret.image);
    memset(&caret, 0, sizeof caret);
    return TRUE;
}
BOOL CreateCaret(ULONG owner, ULONG bitmap, LONG width, LONG height)
{
    if (!open32FindWindow(owner))
    {
        open32SetError(1400);
        return FALSE;
    }
    LxHandleBorrow image = {0};
    if (bitmap > 1)
    {
        PmBitmap *source = lx_pm_bitmap(bitmap, &image);
        if (!source)
        {
            open32SetError(6);
            return FALSE;
        }
        width = (LONG)source->width;
        height = (LONG)source->height;
    }
    if (!width)
        width = GetSystemMetrics(5);
    if (!height)
        height = GetSystemMetrics(6);
    if (width < 1 || height < 1 || width > 512 || height > 512)
    {
        lx_handle_release(&image);
        open32SetError(87);
        return FALSE;
    }
    if (caret.owner)
        DestroyCaret();
    caret = (Caret){.owner = owner,
                    .bitmap = bitmap,
                    .width = width,
                    .height = height,
                    .visibility = -1,
                    .phase = TRUE,
                    .image = image};
    if (blink_time != UINT32_MAX)
        caret.timer = lx_pm_timer_create(owner, blink_time, blink);
    if (!caret.timer && blink_time != UINT32_MAX)
    {
        DestroyCaret();
        open32SetError(8);
        return FALSE;
    }
    return TRUE;
}
BOOL HideCaret(ULONG owner)
{
    if (!caret.owner || (owner && caret.owner != owner))
    {
        open32SetError(5);
        return FALSE;
    }
    if (caret.visibility == INT32_MIN)
    {
        open32SetError(87);
        return FALSE;
    }
    caret.visibility--;
    if (caret.visibility < 0)
        display(FALSE);
    return TRUE;
}
BOOL ShowCaret(ULONG owner)
{
    if (!caret.owner || (owner && caret.owner != owner))
    {
        open32SetError(5);
        return FALSE;
    }
    if (caret.visibility == INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    caret.visibility++;
    if (caret.visibility >= 0 && !caret.painting)
    {
        caret.phase = TRUE;
        display(TRUE);
    }
    return TRUE;
}
BOOL SetCaretPos(LONG x, LONG y)
{
    if (!caret.owner)
    {
        open32SetError(5);
        return FALSE;
    }
    display(FALSE);
    caret.x = x;
    caret.y = y;
    caret.phase = TRUE;
    if (caret.visibility >= 0 && !caret.painting)
        display(TRUE);
    return TRUE;
}
BOOL GetCaretPos(PVOID output)
{
    if (!caret.owner || !output)
    {
        open32SetError(output ? 5 : 87);
        return FALSE;
    }
    lx_guest_store_u32(output, (ULONG)caret.x);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)caret.y);
    return TRUE;
}
ULONG GetCaretBlinkTime(void)
{
    return blink_time;
}
BOOL SetCaretBlinkTime(ULONG interval)
{
    if (!interval)
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG replacement = caret.owner && interval != UINT32_MAX
                            ? lx_pm_timer_create(caret.owner, interval, blink)
                            : 0;
    if (caret.owner && interval != UINT32_MAX && !replacement)
    {
        open32SetError(8);
        return FALSE;
    }
    if (caret.timer)
        lx_pm_timer_stop(caret.timer);
    caret.timer = replacement;
    blink_time = interval;
    caret.phase = TRUE;
    if (caret.owner && caret.visibility >= 0 && !caret.painting)
        display(TRUE);
    return TRUE;
}
void open32CaretPaint(ULONG owner, BOOL begin)
{
    if (caret.owner != owner)
        return;
    if (begin)
    {
        display(FALSE);
        caret.painting++;
    }
    else if (caret.painting && !--caret.painting && caret.visibility >= 0)
        display(caret.phase);
}
void open32CaretWindowDestroyed(ULONG owner)
{
    if (caret.owner == owner)
        DestroyCaret();
}
void open32CaretCleanup(void)
{
    if (caret.owner)
        DestroyCaret();
    blink_time = 530;
}
