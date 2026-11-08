/* Public domain. No rights reserved. Wholly AI-authored. */
/* DROPFILES is a guest memory object, never a host DROPFILES pointer. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmdrag.h"
#include <stdlib.h>
#include <string.h>

enum
{
    DM_DRAGOVER = 0x32e,
    DM_DROP = 0x32f,
    WM_DROPFILES = 0x233,
    DO_DEFAULT = 0xbffe,
    DOR_DROP = 1,
    DOR_NODROPOP = 2,
    DOR_NEVERDROP = 3,
    DROP_HEADER = 20,
    DROP_PATH = 260,
    GHND = 0x42,
    DRAGITEM_NAMES = 24,
    DRAGITEM_CONTAINER = 16,
    DRAGITEM_SOURCE = 20
};

VOID DragAcceptFiles(ULONG window, BOOL accept)
{
    LONG style = GetWindowLong(window, -20);
    SetWindowLong(window, -20, accept ? style | 0x10 : style & ~0x10);
}

VOID DragFinish(ULONG drop)
{
    GlobalFree(drop);
}

static ULONG drop_address(ULONG drop, uint8 header[20])
{
    ULONG address = GlobalLock(drop);
    if (!address || !lx_execution_read(GLoaderState.execution, address, header, 20))
    {
        open32SetError(6);
        return 0;
    }
    return address;
}

ULONG DragQueryFile(ULONG drop, ULONG index, PSZ output, ULONG capacity)
{
    uint8 header[20];
    ULONG base = drop_address(drop, header);
    if (!base)
        return 0;
    ULONG offset = lx_guest_load_u32(header), wide = lx_guest_load_u32(header + 16), address;
    if (offset < 20 || offset > 16u * 1024u * 1024u || base > UINT32_MAX - offset)
    {
        GlobalUnlock(drop);
        open32SetError(87);
        return 0;
    }
    address = base + offset;
    ULONG count = 0, result = 0;
    for (ULONG scanned = 0; scanned < 16u * 1024u * 1024u;)
    {
        ULONG start = address, length = 0;
        USHORT c = 0;
        for (;;)
        {
            uint8 wire[2];
            ULONG step = wide ? 2 : 1;
            if (address > UINT32_MAX - step ||
                !lx_execution_read(GLoaderState.execution, address, wire, step))
                goto drop_done;
            c = wide ? lx_guest_load_u16(wire) : wire[0];
            address += step;
            scanned += step;
            if (!c)
                break;
            if (count == index && output && length + 1 < capacity)
                output[length] = c < 256 ? (char)c : '?';
            length++;
            if (scanned >= 16u * 1024u * 1024u)
                goto drop_done;
        }
        if (address - start == (wide ? 2u : 1u))
        {
            if (index == UINT32_MAX)
                result = count;
            break;
        }
        if (count == index)
        {
            if (output && capacity)
                output[length < capacity ? length : capacity - 1] = 0;
            result = length;
            break;
        }
        count++;
    }
drop_done:
    GlobalUnlock(drop);
    return result;
}

BOOL DragQueryPoint(ULONG drop, PVOID output)
{
    uint8 header[20];
    ULONG address = drop_address(drop, header);
    if (!address)
        return FALSE;
    if (output)
        memcpy(output, header + 4, 8);
    BOOL result = !lx_guest_load_u32(header + 12);
    GlobalUnlock(drop);
    return result;
}

static BOOL item_names(ULONG item, uint8 wire[DRAGITEM_NAMES])
{
    return item && lx_execution_read(GLoaderState.execution, item, wire, DRAGITEM_NAMES);
}

/* DOR_NEVERDROP unless every item is a file named by container and source. */
static ULONG drag_over_response(ULONG info)
{
    USHORT response = DOR_NEVERDROP;
    if (info && DrgAccessDraginfo(info))
    {
        uint8 header[8], names[DRAGITEM_NAMES];
        BOOL default_operation = lx_execution_read(GLoaderState.execution, info, header, 8) &&
                                 lx_guest_load_u16(header + 6) == DO_DEFAULT;
        response = default_operation ? DOR_DROP : DOR_NODROPOP;
        ULONG count = DrgQueryDragitemCount(info);
        for (ULONG i = 0; i < count && response != DOR_NEVERDROP; i++)
        {
            ULONG item = DrgQueryDragitemPtr(info, i);
            if (!item_names(item, names) || !DrgVerifyRMF(item, (PSZ) "DRM_OS2FILE", NULL) ||
                !DrgQueryStrNameLen(lx_guest_load_u32(names + DRAGITEM_CONTAINER)) ||
                !DrgQueryStrNameLen(lx_guest_load_u32(names + DRAGITEM_SOURCE)))
                response = DOR_NEVERDROP;
        }
        DrgFreeDraginfo(info);
    }
    return 0xbfff0000u | response;
}

static BOOL item_path(ULONG item, char path[DROP_PATH])
{
    uint8 names[DRAGITEM_NAMES];
    if (!item_names(item, names))
        return FALSE;
    ULONG length = DrgQueryStrName(lx_guest_load_u32(names + DRAGITEM_CONTAINER), DROP_PATH, path);
    return length && DrgQueryStrName(lx_guest_load_u32(names + DRAGITEM_SOURCE), DROP_PATH - length,
                                     path + length);
}

/* The DROPFILES block belongs to the guest; DragFinish releases it. */
static ULONG build_drop(HWND window, ULONG info)
{
    uint8 header[16];
    if (!lx_execution_read(GLoaderState.execution, info, header, sizeof header))
        return 0;
    ULONG count = DrgQueryDragitemCount(info), drop = 0;
    char *paths = calloc(count ? count : 1, DROP_PATH);
    if (!paths)
        return 0;
    size_t size = DROP_HEADER + 1;
    for (ULONG i = 0; i < count; i++)
    {
        char *path = paths + (size_t)i * DROP_PATH;
        if (item_path(DrgQueryDragitemPtr(info, i), path))
            size += strlen(path) + 1;
        else
            *path = 0;
    }
    POINTL point = {(SHORT)lx_guest_load_u16(header + 12), (SHORT)lx_guest_load_u16(header + 14)};
    SWP position = {0};
    uint8 *wire = calloc(1, size);
    if (wire && WinMapWindowPoints(HWND_DESKTOP, window, &point, 1) &&
        WinQueryWindowPos(window, &position) && (drop = GlobalAlloc(GHND, (ULONG)size)))
    {
        size_t offset = DROP_HEADER;
        lx_guest_store_u32(wire, DROP_HEADER);
        lx_guest_store_u32(wire + 4, (uint32)point.x);
        lx_guest_store_u32(wire + 8, (uint32)(position.cy - point.y));
        for (ULONG i = 0; i < count; i++)
        {
            const char *path = paths + (size_t)i * DROP_PATH;
            if (*path)
            {
                size_t length = strlen(path) + 1;
                memcpy(wire + offset, path, length);
                offset += length;
            }
        }
        ULONG address = GlobalLock(drop);
        BOOL written = address && lx_execution_write(GLoaderState.execution, address, wire, size);
        GlobalUnlock(drop);
        if (!written)
        {
            GlobalFree(drop);
            drop = 0;
        }
    }
    free(wire);
    free(paths);
    return drop;
}

static void post_dropped_files(HWND window, ULONG info)
{
    if (!info || !DrgAccessDraginfo(info))
        return;
    ULONG drop = build_drop(window, info);
    DrgFreeDraginfo(info);
    if (drop && !PostMessage(window, WM_DROPFILES, drop, 0))
        GlobalFree(drop);
}

/* Windows that did not accept files pass drag messages to their parent. */
ULONG open32DragMessage(ULONG window, BOOL accept, ULONG message, ULONG first, ULONG second)
{
    if (!accept)
    {
        HWND parent = WinQueryWindow(window, 5);
        if (!parent || parent == HWND_DESKTOP)
            return 1;
        return (ULONG)(uintptr_t)WinSendMsg(parent, message, (MPARAM)(uintptr_t)first,
                                            (MPARAM)(uintptr_t)second);
    }
    if (message == DM_DRAGOVER)
        return drag_over_response(first);
    post_dropped_files(window, first);
    return 1;
}
