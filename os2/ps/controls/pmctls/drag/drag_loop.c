/* Public domain. No rights reserved. Wholly AI-authored. */
/* DrgDrag shows its images through the pointer, because only the pointer can cross host
   windows. Every DRAGIMAGE is composed into one pointer around the hot spot. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/private/pmp.h"
#include "os2/ps/controls/pmctls/drag/drag.h"
#include "emulator/abi.h"
#include "host/sync.h"
#include "host/clock.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "os2/include/private/pmgrep.h"

enum
{
    DRG_ICON = 1,
    DRG_BITMAP = 2,
    DRG_POLYGON = 4,
    DRG_STRETCH = 8,
    DRG_TRANSPARENT = 0x10,
    DRG_CLOSED = 0x20,
    DRAG_IMAGE_LIMIT = 64,
    DRAG_POINT_LIMIT = 4096,
    DRAG_CANVAS_LIMIT = 1024
};

typedef struct DragImage
{
    ULONG kind, handle, points;
    LONG x, y, width, height;
    LXPOINTERINFO icon;
} DragImage;

typedef struct DragCanvas
{
    LONG left, bottom, width, height;
    uint32_t *color;
    uint8 *hole;
} DragCanvas;

static BOOL drag_image_size(DragImage *image, const uint8 *wire)
{
    LxHandleBorrow borrow = {0};
    ULONG source = image->handle;
    if (image->kind & DRG_ICON)
    {
        if (!WinQueryPointerInfo(image->handle, &image->icon))
            return FALSE;
        source = image->icon.hbmColor;
    }
    PmBitmap *bitmap = lx_pm_bitmap(source, &borrow);
    if (bitmap)
    {
        image->width = (LONG)bitmap->width;
        image->height = (LONG)bitmap->height;
    }
    lx_handle_release(&borrow);
    if (!bitmap)
        return FALSE;
    if (image->kind & DRG_STRETCH)
    {
        image->width = (LONG)lx_guest_load_u32(wire + 8);
        image->height = (LONG)lx_guest_load_u32(wire + 12);
    }
    return image->width > 0 && image->height > 0;
}

static void drag_extent(DragCanvas *c, LONG x, LONG y)
{
    if (x < c->left)
    {
        c->width += c->left - x;
        c->left = x;
    }
    if (y < c->bottom)
    {
        c->height += c->bottom - y;
        c->bottom = y;
    }
    if (x - c->left >= c->width)
        c->width = x - c->left + 1;
    if (y - c->bottom >= c->height)
        c->height = y - c->bottom + 1;
}

static void drag_pixel(DragCanvas *c, LONG x, LONG y, uint32_t rgb)
{
    LONG cx = x - c->left, cy = y - c->bottom;
    if (cx < 0 || cy < 0 || cx >= c->width || cy >= c->height)
        return;
    c->color[(size_t)cy * c->width + cx] = rgb;
    c->hole[(size_t)cy * c->width + cx] = 0;
}

static BOOL drag_outline_pel(void *context, LONG x, LONG y)
{
    drag_pixel(context, x, y, 0);
    return TRUE;
}

static void drag_line(DragCanvas *c, POINTL from, POINTL to)
{
    lx_pm_walk_line(from.x, from.y, to.x, to.y, TRUE, drag_outline_pel, c);
}

/* Icon AND masks occupy the first half of the mask bit map; set bits are transparent. */
static void drag_paint(DragCanvas *c, const DragImage *image)
{
    BOOL icon = (image->kind & DRG_ICON) != 0;
    LxHandleBorrow color_borrow = {0}, mask_borrow = {0};
    PmBitmap *color = lx_pm_bitmap(icon ? image->icon.hbmColor : image->handle, &color_borrow);
    PmBitmap *mask = icon ? lx_pm_bitmap(image->icon.hbmPointer, &mask_borrow) : NULL;
    if (color)
    {
        unsigned sw = color->width, sh = color->height;
#define DRAG_OPAQUE(px, py)                                                                        \
    ((px) >= 0 && (py) >= 0 && (px) < image->width && (py) < image->height &&                      \
     !(mask && (mask->pixels[(size_t)((py) * sh / image->height) * sw +                            \
                             (size_t)(px) * sw / image->width] &                                   \
                0xffffff)))
        for (LONG y = 0; y < image->height; y++)
            for (LONG x = 0; x < image->width; x++)
            {
                if (!DRAG_OPAQUE(x, y))
                    continue;
                /* DRG_TRANSPARENT keeps only the icon's outline. */
                if ((image->kind & DRG_TRANSPARENT) && icon && DRAG_OPAQUE(x - 1, y) &&
                    DRAG_OPAQUE(x + 1, y) && DRAG_OPAQUE(x, y - 1) && DRAG_OPAQUE(x, y + 1))
                    continue;
                uint32_t rgb = image->kind & DRG_TRANSPARENT && icon
                                   ? 0
                                   : color->pixels[(size_t)((uint64_t)y * sh / image->height) * sw +
                                                   (size_t)((uint64_t)x * sw / image->width)];
                drag_pixel(c, image->x + x, image->y + y, rgb & 0xffffff);
            }
#undef DRAG_OPAQUE
    }
    lx_handle_release(&mask_borrow);
    lx_handle_release(&color_borrow);
}

/* Reads a polygon's points, relative to the image origin; returns the count read. */
static ULONG drag_points(const DragImage *image, POINTL *points)
{
    ULONG count = image->points > DRAG_POINT_LIMIT ? DRAG_POINT_LIMIT : image->points;
    if (!count || !lx_execution_read(GProcessState.execution, image->handle, points,
                                     (size_t)count * sizeof *points))
        return 0;
    for (ULONG i = 0; i < count; i++)
    {
        points[i].x += image->x;
        points[i].y += image->y;
    }
    return count;
}

static HPOINTER drag_pointer(ULONG address, ULONG count)
{
    if (count > DRAG_IMAGE_LIMIT)
        count = DRAG_IMAGE_LIMIT;
    DragImage *images = calloc(count ? count : 1, sizeof *images);
    POINTL *points = malloc(DRAG_POINT_LIMIT * sizeof *points);
    DragCanvas canvas = {0, 0, 1, 1, NULL, NULL};
    HPOINTER result = 0;
    ULONG used = 0, mask = 0, color = 0;
    if (!images || !points)
        goto done;
    for (ULONG i = 0; i < count; i++)
    {
        uint8 wire[24];
        if (!lx_execution_read(GProcessState.execution, address, wire, sizeof wire))
            break;
        ULONG size = lx_guest_load_u16(wire);
        address += size >= sizeof wire ? size : sizeof wire;
        DragImage *image = &images[used];
        image->kind = lx_guest_load_u32(wire + 16);
        image->handle = lx_guest_load_u32(wire + 4);
        image->points = lx_guest_load_u16(wire + 2);
        image->x = (SHORT)lx_guest_load_u16(wire + 20);
        image->y = (SHORT)lx_guest_load_u16(wire + 22);
        if (image->kind & DRG_POLYGON)
        {
            ULONG n = drag_points(image, points);
            if (!n)
                continue;
            for (ULONG p = 0; p < n; p++)
                drag_extent(&canvas, points[p].x, points[p].y);
        }
        else if ((image->kind & (DRG_ICON | DRG_BITMAP)) && drag_image_size(image, wire))
        {
            drag_extent(&canvas, image->x, image->y);
            drag_extent(&canvas, image->x + image->width - 1, image->y + image->height - 1);
        }
        else
            continue;
        used++;
    }
    if (!used || canvas.width > DRAG_CANVAS_LIMIT || canvas.height > DRAG_CANVAS_LIMIT)
        goto done;
    size_t pixels = (size_t)canvas.width * canvas.height;
    canvas.color = calloc(pixels, sizeof *canvas.color);
    canvas.hole = malloc(pixels);
    if (!canvas.color || !canvas.hole)
        goto done;
    memset(canvas.hole, 1, pixels);
    for (ULONG i = 0; i < used; i++)
    {
        if (!(images[i].kind & DRG_POLYGON))
        {
            drag_paint(&canvas, &images[i]);
            continue;
        }
        ULONG n = drag_points(&images[i], points);
        for (ULONG p = 1; p < n; p++)
            drag_line(&canvas, points[p - 1], points[p]);
        if (n > 1 && (images[i].kind & DRG_CLOSED))
            drag_line(&canvas, points[n - 1], points[0]);
    }
    mask = lx_pm_bitmap_create(canvas.width, canvas.height * 2, 1, 1);
    color = lx_pm_bitmap_create(canvas.width, canvas.height, 1, 24);
    LxHandleBorrow mask_borrow = {0}, color_borrow = {0};
    PmBitmap *m = mask ? lx_pm_bitmap(mask, &mask_borrow) : NULL;
    PmBitmap *c = color ? lx_pm_bitmap(color, &color_borrow) : NULL;
    if (m && c)
    {
        memcpy(c->pixels, canvas.color, pixels * sizeof *canvas.color);
        for (size_t i = 0; i < pixels; i++)
            m->pixels[i] = canvas.hole[i] ? 0xffffff : 0;
    }
    BOOL ready = m && c;
    lx_handle_release(&color_borrow);
    lx_handle_release(&mask_borrow);
    if (ready)
    {
        LXPOINTERINFO info = {.fPointer = 1,
                              .xHotspot = -canvas.left,
                              .yHotspot = -canvas.bottom,
                              .hbmPointer = mask,
                              .hbmColor = color};
        result = WinCreatePointerIndirect(HWND_DESKTOP, &info);
    }
done:
    if (mask)
        lx_pm_bitmap_delete(mask);
    if (color)
        lx_pm_bitmap_delete(color);
    free(canvas.color);
    free(canvas.hole);
    free(points);
    free(images);
    return result;
}

HWND DrgDrag(HWND source, ULONG info, ULONG images, ULONG count, LONG terminate, ULONG reserved)
{
    uint8 header[DRAGINFO_WIRE_SIZE];
    HAB hab = WinQueryAnchorBlock(source);
    if (!hab || reserved || !drag_info(info, header) || count > 65535 ||
        (count && !lx_execution_memory(GProcessState.execution, images, count * 24, LX_GUEST_READ)))
        return 0;
    lx_guest_store_u32(header + 8, source);
    HWND previous = 0, result = 0;
    LONG button = terminate == 0x38 ? 2 : terminate;
    if (button < 1 || button > 3)
        button = 2;
    BOOL accepted = FALSE;
    HPOINTER cursor = 0, created = count ? drag_pointer(images, count) : 0;
    if (created)
    {
        cursor = WinQueryPointer(HWND_DESKTOP);
        WinSetPointer(HWND_DESKTOP, created);
    }
    WinSetCapture(HWND_DESKTOP, 0);
    for (;;)
    {
        POINTL point;
        if (!WinQueryPointerPos(HWND_DESKTOP, &point))
            break;
        lx_guest_store_u16(header + 12, point.x);
        lx_guest_store_u16(header + 14, point.y);
        if (!lx_execution_write(GProcessState.execution, info, header, sizeof header))
            break;
        HWND target = WinWindowFromPoint(HWND_DESKTOP, &point, TRUE);
        if (target != previous)
        {
            if (previous)
                WinSendMsg(previous, 0x32d, (MPARAM)(uintptr_t)info, 0);
            previous = target;
        }
        if (target)
        {
            ULONG position = (ULONG)(USHORT)point.x | (ULONG)(USHORT)point.y << 16;
            ULONG reply = (ULONG)(uintptr_t)WinSendMsg(target, 0x32e, (MPARAM)(uintptr_t)info,
                                                       (MPARAM)(uintptr_t)position);
            accepted = (reply & 0xffff) == 1;
            if (accepted)
                lx_guest_store_u16(header + 6, reply >> 16);
        }
        else
            accepted = FALSE;
        if (WinGetKeyState(HWND_DESKTOP, 0x0f) & 0x8000)
            break;                                       /* escape */
        if (WinGetKeyState(HWND_DESKTOP, 0x20) & 0x8000) /* VK_F1 asks the target for help */
        {
            if (target)
                WinSendMsg(target, 0x32c, (MPARAM)(uintptr_t)info, 0); /* DM_DROPHELP */
            break;
        }
        if (!(WinGetKeyState(HWND_DESKTOP, button) & 0x8000))
        {
            if (accepted && target)
            {
                lx_execution_write(GProcessState.execution, info, header, sizeof header);
                WinSendMsg(target, 0x32f, (MPARAM)(uintptr_t)info, 0);
                result = target;
            }
            break;
        }
        QMSG message;
        while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
            WinDispatchMsg(hab, &message);
        lx_execution_poll_termination(GProcessState.execution);
        lx_host_sleep_ms(5);
    }
    if (previous && !result)
        WinSendMsg(previous, 0x32d, (MPARAM)(uintptr_t)info, 0);
    if (cursor)
        WinSetPointer(HWND_DESKTOP, cursor);
    if (created)
        WinDestroyPointer(created);
    return result;
}
