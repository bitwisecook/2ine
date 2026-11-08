/* Public domain. No rights reserved. Wholly AI-authored. */
/* Drag string handles retain independent, checked copies of caller strings. */
#include "os2libs/os2native.h"
#include "os2libs/pmdrag.h"
#include "os2libs/generated/pmdrag-lx.h"

ULONG DrgAddStrHandle(PSZ name)
{
    if (!name)
        return 0;
    char *copy = strdup(name);
    if (!copy)
        return 0;
    ULONG handle = lx_handle_create(LX_HANDLE_PM_DRAG_STRING, copy, free);
    if (!handle)
        free(copy);
    return handle;
}

BOOL DrgDeleteStrHandle(ULONG string)
{
    return lx_handle_close(string, LX_HANDLE_PM_DRAG_STRING);
}

ULONG DrgQueryStrName(ULONG string, ULONG capacity, PSZ output)
{
    if (!output || !capacity)
        return 0;
    *output = 0;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(string, LX_HANDLE_PM_DRAG_STRING, &borrow))
        return 0;
    size_t length = strlen(borrow.resource), copied = length < capacity ? length : capacity - 1;
    memcpy(output, borrow.resource, copied);
    output[copied] = 0;
    lx_handle_release(&borrow);
    return (ULONG)copied;
}

ULONG DrgQueryStrNameLen(ULONG string)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(string, LX_HANDLE_PM_DRAG_STRING, &borrow))
        return 0;
    ULONG length = (ULONG)strlen(borrow.resource);
    lx_handle_release(&borrow);
    return length;
}

/* Drag structures are wire objects. Their public pointers always name mapped
   guest memory, and never expose the host allocation that tracks ownership. */
#include "emulator/abi.h"
#include "host/sync.h"
#include "host/clock.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmwin/graphics.h"

typedef struct DragAllocation
{
    struct DragAllocation *next;
    LxGuestMapping *mapping;
    ULONG address, size, kind, references;
    uint8 *shared; /* One flag per transfer element already handed to a receiver. */
} DragAllocation;

static DragAllocation *drag_allocations;
static LxHostOnce drag_once = LX_HOST_ONCE_INIT;
static LxHostMutex *drag_mutex;

static void drag_initialize(void)
{
    drag_mutex = lx_host_mutex_create(0, 0);
}

static ULONG drag_allocate(ULONG count, ULONG item_size, ULONG header, ULONG kind)
{
    if (!count || count > 65535 || count > (UINT32_MAX - header) / item_size)
        return 0;
    ULONG size = header + count * item_size;
    DragAllocation *allocation = calloc(1, sizeof *allocation);
    if (!allocation)
        return 0;
    allocation->mapping =
        lx_execution_map(GProcessState.execution, 0, (size + 4095u) & ~4095u, 3, 0);
    if (!allocation->mapping)
    {
        free(allocation);
        return 0;
    }
    allocation->address = lx_execution_mapping_address(allocation->mapping);
    allocation->size = size;
    allocation->kind = kind;
    /* Each transfer element is freed separately; draginfo has one owner reference. */
    allocation->references = kind == 2 ? count : 1;
    if (kind == 2 && !(allocation->shared = calloc(count, 1)))
    {
        lx_execution_unmap(GProcessState.execution, allocation->mapping);
        free(allocation);
        return 0;
    }
    uint8 *wire = lx_execution_mapping_host(allocation->mapping);
    if (kind == 1)
    {
        lx_guest_store_u32(wire, size);
        lx_guest_store_u16(wire + 4, item_size);
        lx_guest_store_u16(wire + 6, 0xbffe); /* DO_DEFAULT */
        lx_guest_store_u16(wire + 16, count);
    }
    else
        for (ULONG i = 0; i < count; i++)
            lx_guest_store_u32(wire + i * item_size, item_size);
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    allocation->next = drag_allocations;
    drag_allocations = allocation;
    lx_host_mutex_unlock(drag_mutex);
    return allocation->address;
}

ULONG DrgAllocDraginfo(ULONG count)
{
    return drag_allocate(count, 36, 20, 1);
}

ULONG DrgAllocDragtransfer(ULONG count)
{
    return drag_allocate(count, 28, 0, 2);
}

static BOOL drag_info(ULONG address, uint8 wire[20])
{
    if (!address || !lx_execution_read(GProcessState.execution, address, wire, 20))
        return FALSE;
    ULONG size = lx_guest_load_u32(wire), item = lx_guest_load_u16(wire + 4);
    ULONG count = lx_guest_load_u16(wire + 16);
    return item >= 36 && size >= 20 && count <= (size - 20) / item &&
           lx_execution_memory(GProcessState.execution, address, size, LX_GUEST_READ) != NULL;
}

BOOL DrgAccessDraginfo(ULONG address)
{
    uint8 wire[20];
    if (!drag_info(address, wire))
        return FALSE;
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    for (DragAllocation *a = drag_allocations; a; a = a->next)
        if (a->address == address && a->kind == 1)
        {
            if (a->references == UINT32_MAX)
            {
                lx_host_mutex_unlock(drag_mutex);
                return FALSE;
            }
            a->references++;
            break;
        }
    lx_host_mutex_unlock(drag_mutex);
    return TRUE;
}

static BOOL drag_free(ULONG address, ULONG kind)
{
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    DragAllocation **link = &drag_allocations, *allocation = NULL;
    while (*link && ((*link)->kind != kind || address < (*link)->address ||
                     address - (*link)->address >= (kind == 2 ? (*link)->size : 1)))
        link = &(*link)->next;
    if (*link && !--(*link)->references)
    {
        allocation = *link;
        *link = allocation->next;
    }
    BOOL found = allocation || *link;
    lx_host_mutex_unlock(drag_mutex);
    if (allocation)
    {
        lx_execution_unmap(GProcessState.execution, allocation->mapping);
        free(allocation->shared);
        free(allocation);
    }
    return found;
}

BOOL DrgFreeDraginfo(ULONG address)
{
    return drag_free(address, 1);
}

BOOL DrgFreeDragtransfer(ULONG address)
{
    return drag_free(address, 2);
}

ULONG DrgQueryDragitemCount(ULONG address)
{
    uint8 wire[20];
    return drag_info(address, wire) ? lx_guest_load_u16(wire + 16) : 0;
}

ULONG DrgQueryDragitemPtr(ULONG address, ULONG index)
{
    uint8 wire[20];
    if (!drag_info(address, wire) || index >= lx_guest_load_u16(wire + 16))
        return 0;
    return address + 20 + index * lx_guest_load_u16(wire + 4);
}

BOOL DrgSetDragitem(ULONG address, ULONG item, ULONG bytes, ULONG index)
{
    uint8 wire[36];
    ULONG target = DrgQueryDragitemPtr(address, index), copied = bytes < 36 ? bytes : 36;
    if (!target)
        return FALSE;
    return !copied || (lx_execution_read(GProcessState.execution, item, wire, copied) &&
                       lx_execution_write(GProcessState.execution, target, wire, copied));
}

BOOL DrgDeleteDraginfoStrHandles(ULONG address)
{
    uint8 header[20], item[36];
    if (!drag_info(address, header))
        return FALSE;
    ULONG count = lx_guest_load_u16(header + 16), stride = lx_guest_load_u16(header + 4);
    if (!count)
        return TRUE;
    /* Items may share a string handle, which is deleted only once. */
    ULONG *strings = malloc((size_t)count * 5 * sizeof *strings), found = 0;
    if (!strings)
        return FALSE;
    BOOL result = TRUE;
    for (ULONG i = 0; i < count && result; i++)
    {
        result = lx_execution_read(GProcessState.execution, address + 20 + i * stride, item,
                                   sizeof item);
        for (unsigned offset = 8; result && offset <= 24; offset += 4)
        {
            ULONG string = lx_guest_load_u32(item + offset), known = 0;
            while (known < found && strings[known] != string)
                known++;
            if (string && known == found)
                strings[found++] = string;
        }
    }
    for (ULONG i = 0; i < found && result; i++)
        result = DrgDeleteStrHandle(strings[i]);
    free(strings);
    return result;
}

static BOOL rmf_token(const char *begin, const char *end, const char *wanted)
{
    if (!wanted)
        return TRUE;
    while (begin < end)
    {
        while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == ','))
            begin++;
        const char *finish = begin;
        while (finish < end && *finish != ',')
            finish++;
        const char *trim = finish;
        while (trim > begin && (trim[-1] == ' ' || trim[-1] == '\t'))
            trim--;
        if ((size_t)(trim - begin) == strlen(wanted) && !memcmp(begin, wanted, trim - begin))
            return TRUE;
        begin = finish < end ? finish + 1 : end;
    }
    return FALSE;
}

BOOL DrgVerifyRMF(ULONG item_address, PSZ mechanism, PSZ format)
{
    uint8 item[36];
    if (!lx_execution_read(GProcessState.execution, item_address, item, sizeof item))
        return FALSE;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(lx_guest_load_u32(item + 12), LX_HANDLE_PM_DRAG_STRING, &borrow))
        return FALSE;
    const char *p = borrow.resource;
    BOOL result = FALSE;
    while (*p && !result)
    {
        if (*p == '<')
        {
            const char *comma = strchr(p + 1, ','), *end = strchr(p + 1, '>');
            if (!comma || !end || comma >= end)
                break;
            result = rmf_token(p + 1, comma, mechanism) && rmf_token(comma + 1, end, format);
            p = end + 1;
        }
        else if (*p == '(')
        {
            const char *end = strchr(p + 1, ')');
            if (!end)
                break;
            const char *second = end + 1;
            while (*second == ' ' || *second == '\t')
                second++;
            if (*second++ != 'x')
            {
                p = end + 1;
                continue;
            }
            while (*second == ' ' || *second == '\t')
                second++;
            if (*second != '(')
                break;
            const char *finish = strchr(second + 1, ')');
            if (!finish)
                break;
            result = rmf_token(p + 1, end, mechanism) && rmf_token(second + 1, finish, format);
            p = finish + 1;
        }
        else
            p++;
    }
    lx_handle_release(&borrow);
    return result;
}

/* The drag PS clips to siblings and children whatever the window's own styles. */
HPS DrgGetPS(HWND window)
{
    return lx_pm_get_clip_ps(window, 0x18);
}

BOOL DrgReleasePS(HPS space)
{
    return WinReleasePS(space);
}

/* Clears fsReply and gives the receiver its own free of the element, once per element. */
static void drag_transfer_sent(ULONG transfer, ULONG message)
{
    uint8 reply[2] = {0, 0};
    lx_execution_write(GProcessState.execution, transfer + 26, reply, sizeof reply);
    if (message != 0x329 && message != 0x327) /* DM_RENDER, DM_RENDERPREPARE */
        return;
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    for (DragAllocation *a = drag_allocations; a; a = a->next)
        if (a->kind == 2 && transfer >= a->address && transfer - a->address < a->size)
        {
            ULONG element = (transfer - a->address) / 28;
            if (!a->shared[element])
            {
                a->shared[element] = 1;
                a->references++;
            }
            break;
        }
    lx_host_mutex_unlock(drag_mutex);
}

MRESULT DrgSendTransferMsg(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == 0x329 || message == 0x327)
        drag_transfer_sent((ULONG)(uintptr_t)first, message);
    return WinSendMsg(window, message, first, second);
}

BOOL DrgPostTransferMsg(HWND window, ULONG message, ULONG transfer, ULONG flags, ULONG reserved,
                        BOOL retry)
{
    (void)reserved;
    uint8 wire[28];
    if (!lx_execution_read(GProcessState.execution, transfer, wire, sizeof wire) ||
        lx_guest_load_u32(wire) < sizeof wire)
        return FALSE;
    drag_transfer_sent(transfer, message);
    do
    {
        if (WinPostMsg(window, message, (MPARAM)(uintptr_t)transfer, (MPARAM)(uintptr_t)flags))
            return TRUE;
        if (!retry || !WinIsWindow(WinQueryAnchorBlock(window), window))
            return FALSE;
        lx_execution_poll_termination(GProcessState.execution);
        lx_host_sleep_ms(1);
    } while (retry);
    return FALSE;
}

/* DrgDrag shows its images through the pointer, because only the pointer can cross host
   windows. Every DRAGIMAGE is composed into one pointer around the hot spot. */
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

static void drag_line(DragCanvas *c, POINTL from, POINTL to)
{
    LONG dx = labs(to.x - from.x), dy = labs(to.y - from.y), error = dx - dy;
    LONG sx = from.x < to.x ? 1 : -1, sy = from.y < to.y ? 1 : -1;
    for (;;)
    {
        drag_pixel(c, from.x, from.y, 0);
        if (from.x == to.x && from.y == to.y)
            return;
        LONG twice = error * 2;
        if (twice > -dy)
        {
            error -= dy;
            from.x += sx;
        }
        if (twice < dx)
        {
            error += dx;
            from.y += sy;
        }
    }
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
    uint8 header[20];
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
