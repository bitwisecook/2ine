/* Public domain. No rights reserved. Wholly AI-authored. */
/* One queue-thread caret uses reversible raster inversion and a private PM timer. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmp.h"

enum
{
    /* CreateCaret's bitmap argument 1 asks for a grey (checkered) caret. */
    CARET_GREY_BITMAP = 1,
    /* Pixels; bounds the raster buffer each blink reads and writes. */
    CARET_EXTENT_MAX = 512,
    /* Inverting every RGB bit makes the second blink restore the pixels. */
    CARET_INVERT_MASK = 0xFFFFFF,
    /* Milliseconds; the Windows default caret blink time. */
    CARET_BLINK_TIME_DEFAULT = 530
};

typedef struct Caret
{
    ULONG owner, bitmap, timer;
    LONG x, y, width, height, visibility;
    unsigned painting;
    BOOL drawn, phase;
    LxHandleBorrow image;
} Caret;

static _Thread_local Caret caret;
static _Thread_local ULONG blink_time = CARET_BLINK_TIME_DEFAULT;

static BOOL invert_caret(void)
{
    RECTL bounds;
    ULONG drawable = open32ClientWindow(caret.owner);
    if (!caret.owner || !WinQueryWindowRect(drawable, &bounds))
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
    uint32 *pixels = malloc((size_t)width * height * sizeof *pixels);
    HPS ps = pixels ? WinGetPS(drawable) : 0;
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
                    : caret.bitmap == CARET_GREY_BITMAP && ((local_x + local_y) & 1)
                        ? 0
                        : CARET_INVERT_MASK;
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
        open32SetError(ERROR_ACCESS_DENIED);
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
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    LxHandleBorrow image = {0};
    if (bitmap > CARET_GREY_BITMAP)
    {
        PmBitmap *source = lx_pm_bitmap(bitmap, &image);
        if (!source)
        {
            open32SetError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
        width = (LONG)source->width;
        height = (LONG)source->height;
    }
    if (!width)
        width = GetSystemMetrics(WIN32_SM_CXBORDER);
    if (!height)
        height = GetSystemMetrics(WIN32_SM_CYBORDER);
    if (width < 1 || height < 1 || width > CARET_EXTENT_MAX || height > CARET_EXTENT_MAX)
    {
        lx_handle_release(&image);
        open32SetError(ERROR_INVALID_PARAMETER);
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
    if (blink_time != WIN32_INFINITE)
        caret.timer = lx_pm_timer_create(owner, blink_time, blink);
    if (!caret.timer && blink_time != WIN32_INFINITE)
    {
        DestroyCaret();
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

BOOL HideCaret(ULONG owner)
{
    if (!caret.owner || (owner && caret.owner != owner))
    {
        open32SetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (caret.visibility == INT32_MIN)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
        open32SetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (caret.visibility == INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
        open32SetError(ERROR_ACCESS_DENIED);
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
        open32SetError(output ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lx_guest_store_u32(output, (ULONG)caret.x);
    lx_guest_store_u32((uint8 *)output + WIN32_POINT_Y, (ULONG)caret.y);
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG replacement = caret.owner && interval != WIN32_INFINITE
                            ? lx_pm_timer_create(caret.owner, interval, blink)
                            : 0;
    if (caret.owner && interval != WIN32_INFINITE && !replacement)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
    blink_time = CARET_BLINK_TIME_DEFAULT;
}
