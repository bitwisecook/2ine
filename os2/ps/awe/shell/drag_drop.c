/* Public domain. No rights reserved. Wholly AI-authored. */
/* DROPFILES is a guest memory object, never a host DROPFILES pointer. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/bsedos.h"
#include <stdlib.h>
#include <string.h>

enum
{
    DROP_PATH = CCHMAXPATH,
    /* A DROPFILES block whose file list exceeds this is treated as malformed. */
    DROP_SCAN_LIMIT = 16u * 1024u * 1024u,
    /* PM DRAGINFO: usOperation, and the drop point (xDrop, yDrop) in screen coordinates. */
    DRAGINFO_HEADER = 16,
    DRAGINFO_OPERATION = 6,
    DRAGINFO_X_DROP = 12,
    DRAGINFO_Y_DROP = 14,
    /* PM DRAGITEM up to hstrTargetName: the container and source name string handles. */
    DRAGITEM_NAMES = 24,
    DRAGITEM_CONTAINER = 16,
    DRAGITEM_SOURCE = 20
};

VOID DragAcceptFiles(ULONG window, BOOL accept)
{
    LONG style = GetWindowLong(window, WIN32_GWL_EXSTYLE);
    SetWindowLong(window, WIN32_GWL_EXSTYLE,
                  accept ? style | WIN32_WS_EX_ACCEPTFILES : style & ~WIN32_WS_EX_ACCEPTFILES);
}

VOID DragFinish(ULONG drop)
{
    GlobalFree(drop);
}

static ULONG drop_address(ULONG drop, uint8 header[WIN32_DROPFILES_SIZE])
{
    ULONG address = GlobalLock(drop);
    if (!address ||
        !lx_execution_read(GProcessState.execution, address, header, WIN32_DROPFILES_SIZE))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return address;
}

ULONG DragQueryFile(ULONG drop, ULONG index, PSZ output, ULONG capacity)
{
    uint8 header[WIN32_DROPFILES_SIZE];
    ULONG base = drop_address(drop, header);
    if (!base)
        return 0;
    ULONG offset = lx_guest_load_u32(header + WIN32_DROPFILES_FILES),
          wide = lx_guest_load_u32(header + WIN32_DROPFILES_WIDE), address;
    if (offset < WIN32_DROPFILES_SIZE || offset > DROP_SCAN_LIMIT || base > UINT32_MAX - offset)
    {
        GlobalUnlock(drop);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    address = base + offset;
    ULONG count = 0, result = 0;
    for (ULONG scanned = 0; scanned < DROP_SCAN_LIMIT;)
    {
        ULONG start = address, length = 0;
        USHORT c = 0;
        for (;;)
        {
            uint8 wire[sizeof(USHORT)];
            ULONG step = wide ? 2 : 1;
            if (address > UINT32_MAX - step ||
                !lx_execution_read(GProcessState.execution, address, wire, step))
                goto drop_done;
            c = wide ? lx_guest_load_u16(wire) : wire[0];
            address += step;
            scanned += step;
            if (!c)
                break;
            if (count == index && output && length + 1 < capacity)
                output[length] = c < 256 ? (char)c : '?';
            length++;
            if (scanned >= DROP_SCAN_LIMIT)
                goto drop_done;
        }
        /* An empty name ends the double-null-terminated list. */
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
    uint8 header[WIN32_DROPFILES_SIZE];
    ULONG address = drop_address(drop, header);
    if (!address)
        return FALSE;
    if (output)
        memcpy(output, header + WIN32_DROPFILES_POINT, WIN32_POINT_SIZE);
    BOOL result = !lx_guest_load_u32(header + WIN32_DROPFILES_NONCLIENT);
    GlobalUnlock(drop);
    return result;
}

static BOOL item_names(ULONG item, uint8 wire[DRAGITEM_NAMES])
{
    return item && lx_execution_read(GProcessState.execution, item, wire, DRAGITEM_NAMES);
}

/* DOR_NEVERDROP unless every item is a file named by container and source. */
static ULONG drag_over_response(ULONG info)
{
    USHORT response = DOR_NEVERDROP;
    if (info && DrgAccessDraginfo(info))
    {
        uint8 header[DRAGINFO_OPERATION + sizeof(USHORT)], names[DRAGITEM_NAMES];
        BOOL default_operation =
            lx_execution_read(GProcessState.execution, info, header, sizeof header) &&
            lx_guest_load_u16(header + DRAGINFO_OPERATION) == DO_DEFAULT;
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
    return (ULONG)DO_UNKNOWN << 16 | response;
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
    uint8 header[DRAGINFO_HEADER];
    if (!lx_execution_read(GProcessState.execution, info, header, sizeof header))
        return 0;
    ULONG count = DrgQueryDragitemCount(info), drop = 0;
    char *paths = calloc(count ? count : 1, DROP_PATH);
    if (!paths)
        return 0;
    size_t size = WIN32_DROPFILES_SIZE + 1;
    for (ULONG i = 0; i < count; i++)
    {
        char *path = paths + (size_t)i * DROP_PATH;
        if (item_path(DrgQueryDragitemPtr(info, i), path))
            size += strlen(path) + 1;
        else
            *path = 0;
    }
    POINTL point = {(SHORT)lx_guest_load_u16(header + DRAGINFO_X_DROP),
                    (SHORT)lx_guest_load_u16(header + DRAGINFO_Y_DROP)};
    SWP position = {0};
    uint8 *wire = calloc(1, size);
    if (wire && WinMapWindowPoints(HWND_DESKTOP, window, &point, 1) &&
        WinQueryWindowPos(window, &position) &&
        (drop = GlobalAlloc(WIN32_GMEM_MOVEABLE | WIN32_GMEM_ZEROINIT, (ULONG)size)))
    {
        /* The point is client-relative with y measured down from the top, as Windows has it. */
        size_t offset = WIN32_DROPFILES_SIZE;
        lx_guest_store_u32(wire + WIN32_DROPFILES_FILES, WIN32_DROPFILES_SIZE);
        lx_guest_store_u32(wire + WIN32_DROPFILES_POINT, (uint32)point.x);
        lx_guest_store_u32(wire + WIN32_DROPFILES_POINT + sizeof(LONG),
                           (uint32)(position.cy - point.y));
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
        BOOL written = address && lx_execution_write(GProcessState.execution, address, wire, size);
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
    if (drop && !PostMessage(window, WIN32_WM_DROPFILES, drop, 0))
        GlobalFree(drop);
}

/* Windows that did not accept files pass drag messages to their parent. */
ULONG open32DragMessage(ULONG window, BOOL accept, ULONG message, ULONG first, ULONG second)
{
    if (!accept)
    {
        HWND parent = WinQueryWindow(window, QW_PARENT);
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
