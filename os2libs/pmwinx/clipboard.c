/* Public domain. No rights reserved. Wholly AI-authored. */
/* Clipboard viewers form a checked notification chain within this worker. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin.h"
#include "host/sync.h"
#include "host/clipboard.h"

typedef struct ClipboardMemory
{
    ULONG format, handle;
    uint64_t serial;
    struct ClipboardMemory *next;
} ClipboardMemory;

static ClipboardMemory *clipboard_memory;

static void discard_clipboard_memory(int all)
{
    ClipboardMemory **slot = &clipboard_memory;
    while (*slot)
    {
        ClipboardMemory *item = *slot;
        if (all || item->serial != lx_pm_clipboard_format_serial(item->format))
        {
            *slot = item->next;
            GlobalFree(item->handle);
            free(item);
        }
        else
            slot = &item->next;
    }
}

typedef struct Viewer
{
    ULONG window;
    struct Viewer *next;
} Viewer;

static Viewer *viewers;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;

static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}

ULONG SetClipboardViewer(ULONG window)
{
    if (!open32FindWindow(window))
    {
        open32SetError(1400);
        return 0;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    for (Viewer *viewer = viewers; viewer; viewer = viewer->next)
        if (viewer->window == window)
        {
            ULONG next = viewer->next ? viewer->next->window : 0;
            lx_host_monitor_unlock(monitor);
            return next;
        }
    Viewer *viewer = calloc(1, sizeof *viewer);
    if (!viewer)
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(8);
        return 0;
    }
    ULONG previous = viewers ? viewers->window : 0;
    viewer->window = window;
    viewer->next = viewers;
    viewers = viewer;
    lx_host_monitor_unlock(monitor);
    SendMessage(window, 0x308, 0, 0);
    return previous;
}

ULONG GetClipboardViewer(void)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    ULONG window = viewers ? viewers->window : 0;
    lx_host_monitor_unlock(monitor);
    return window;
}

BOOL ChangeClipboardChain(ULONG window, ULONG next)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Viewer **link = &viewers;
    while (*link && (*link)->window != window)
        link = &(*link)->next;
    if (!*link)
    {
        lx_host_monitor_unlock(monitor);
        return FALSE;
    }
    Viewer *removed = *link;
    *link = removed->next;
    ULONG head = viewers ? viewers->window : 0;
    free(removed);
    lx_host_monitor_unlock(monitor);
    if (head && head != window)
        SendMessage(head, 0x30D, window, next);
    return TRUE;
}

void open32ClipboardWindowDestroyed(ULONG window)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Viewer **link = &viewers;
    while (*link && (*link)->window != window)
        link = &(*link)->next;
    if (*link)
    {
        Viewer *removed = *link;
        *link = removed->next;
        free(removed);
    }
    lx_host_monitor_unlock(monitor);
}

void open32ClipboardCleanup(void)
{
    discard_clipboard_memory(1);
    while (viewers)
    {
        Viewer *viewer = viewers;
        viewers = viewer->next;
        free(viewer);
    }
}

BOOL IsClipboardFormatAvailable(ULONG format)
{
    ULONG flags;
    return format && format <= 0xffff &&
           WinQueryClipbrdFmtInfo(open32Presentation(), format, &flags);
}

static _Thread_local ULONG opened_for;

BOOL OpenClipboard(ULONG window)
{
    if (window && !open32FindWindow(window))
    {
        open32SetError(1400);
        return FALSE;
    }
    if (!WinOpenClipbrd(open32Presentation()))
    {
        open32SetError(5);
        return FALSE;
    }
    opened_for = window;
    return TRUE;
}

BOOL CloseClipboard(void)
{
    if (!WinCloseClipbrd(open32Presentation()))
    {
        open32SetError(1418);
        return FALSE;
    }
    opened_for = 0;
    return TRUE;
}

BOOL EmptyClipboard(void)
{
    ULONG hab = open32Presentation();
    if (!WinEmptyClipbrd(hab) ||
        !WinSetClipbrdOwner(hab, opened_for ? open32ClientWindow(opened_for) : 0))
    {
        open32SetError(1418);
        return FALSE;
    }
    discard_clipboard_memory(1);
    return TRUE;
}

ULONG CountClipboardFormats(void)
{
    if (!open32Presentation())
        return 0;
    return lx_pm_clipboard_count();
}

ULONG EnumClipboardFormats(ULONG format)
{
    ULONG hab = open32Presentation(), next = WinEnumClipbrdFmts(hab, format);
    while (next && next != 1 && next != 2 && next < 0xc000)
        next = WinEnumClipbrdFmts(hab, next);
    return next;
}

ULONG GetClipboardFormatName(ULONG format, PSZ output, LONG capacity)
{
    if (format < 0xc000 || format > 0xffff || !output || capacity <= 0)
    {
        open32SetError(87);
        return 0;
    }
    return lx_pm_clipboard_format_name(format, output, (ULONG)capacity);
}

ULONG GetClipboardOwner(void)
{
    return open32PublicWindow(WinQueryClipbrdOwner(open32Presentation()));
}

static ClipboardMemory *remember_memory(ULONG format, ULONG handle)
{
    ClipboardMemory *item = calloc(1, sizeof *item);
    if (item)
    {
        item->format = format;
        item->handle = handle;
        item->serial = lx_pm_clipboard_format_serial(format);
        item->next = clipboard_memory;
        clipboard_memory = item;
    }
    return item;
}

ULONG SetClipboardData(ULONG format, ULONG handle)
{
    if (!format || (format != 1 && format != 2 && format < 0xc000) || format > 0xffff)
    {
        open32SetError(87);
        return 0;
    }
    ULONG hab = open32Presentation();
    discard_clipboard_memory(0);
    if (!handle || format == 2)
    {
        if (!WinSetClipbrdData(hab, handle, format, format == 2 ? 0x200 : 0x400))
            open32SetError(1418);
        return handle;
    }
    ULONG size = GlobalSize(handle), address = GlobalLock(handle);
    void *bytes = size && size <= 16 * 1024 * 1024 ? malloc(size) : NULL;
    int valid =
        address && bytes && lx_execution_read(GProcessState.execution, address, bytes, size);
    if (address)
        GlobalUnlock(handle);
    if (!valid)
    {
        free(bytes);
        open32SetError(6);
        return 0;
    }
    size_t length = size;
    if (format == 1)
    {
        const char *end = memchr(bytes, 0, size);
        void *utf8 = end ? lx_host_clipboard_text_utf8(
                               1004, bytes, (const char *)end - (const char *)bytes, &length)
                         : NULL;
        free(bytes);
        bytes = utf8 ? lx_host_clipboard_text_os2(
                           GProcessState.process_codepage ? GProcessState.process_codepage : 437,
                           utf8, length, &length)
                     : NULL;
        free(utf8);
    }
    BOOL copied = bytes && lx_pm_clipboard_set_bytes(hab, format, bytes, length);
    free(bytes);
    if (!copied)
    {
        open32SetError(1418);
        return 0;
    }
    discard_clipboard_memory(0);
    if (!remember_memory(format, handle))
    {
        open32SetError(8);
        return 0;
    }
    return handle;
}

ULONG GetClipboardData(ULONG format)
{
    ULONG hab = open32Presentation();
    discard_clipboard_memory(0);
    if (format == 2)
        return WinQueryClipbrdData(hab, format);
    if (!format || (format != 1 && format < 0xc000) || format > 0xffff)
    {
        open32SetError(87);
        return 0;
    }
    for (ClipboardMemory *item = clipboard_memory; item; item = item->next)
        if (item->format == format)
            return item->handle;
    size_t size = 0;
    void *bytes = lx_pm_clipboard_copy_bytes(hab, format, &size);
    if (bytes && format == 1)
    {
        void *utf8 = lx_host_clipboard_text_utf8(
            GProcessState.process_codepage ? GProcessState.process_codepage : 437, bytes, size,
            &size);
        free(bytes);
        bytes = utf8 ? lx_host_clipboard_text_os2(1004, utf8, size, &size) : NULL;
        free(utf8);
    }
    if (!bytes || size >= 16 * 1024 * 1024)
    {
        free(bytes);
        open32SetError(6);
        return 0;
    }
    ULONG handle = GlobalAlloc(0x42, (ULONG)size + 1), address = handle ? GlobalLock(handle) : 0;
    BOOL copied = address && lx_execution_write(GProcessState.execution, address, bytes, size + 1);
    if (address)
        GlobalUnlock(handle);
    free(bytes);
    if (!copied || !remember_memory(format, handle))
    {
        if (handle)
            GlobalFree(handle);
        open32SetError(8);
        return 0;
    }
    return handle;
}
