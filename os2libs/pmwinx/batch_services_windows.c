/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window services preserve Open32's handles and top-left client coordinates. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/input.h"
#include "guest/handles.h"
#include <stdio.h>

ULONG GetForegroundWindow(void)
{
    return open32PublicWindow(GetActiveWindow());
}
ULONG GetCursor(void)
{
    return open32Presentation() ? WinQueryPointer(HWND_DESKTOP) : 0;
}
ULONG GetDoubleClickTime(void)
{
    return (ULONG)WinQuerySysValue(HWND_DESKTOP, 1);
}
ULONG GetTopWindow(ULONG parent)
{
    return open32PublicWindow(
        WinQueryWindow(parent ? open32ClientWindow(parent) : HWND_DESKTOP, 2));
}
ULONG GetNextWindow(ULONG window, ULONG direction)
{
    if (direction != 2 && direction != 3)
        return 0;
    return open32PublicWindow(
        WinQueryWindow(window ? window : HWND_DESKTOP, direction == 2 ? 0 : 1));
}
BOOL IsChild(ULONG parent, ULONG child)
{
    if (!parent)
        parent = HWND_DESKTOP;
    for (unsigned depth = 0; child && depth < 256; depth++)
    {
        child = WinQueryWindow(child, 5);
        if (child == open32ClientWindow(parent) || child == parent)
            return TRUE;
    }
    return FALSE;
}
ULONG GetLastActivePopup(ULONG window)
{
    ULONG target = window ? window : HWND_DESKTOP;
    if (target != HWND_DESKTOP && !WinIsWindow(0, target))
        return 0;
    if (open32PublicWindow(GetActiveWindow()) == target)
        return window;
    /* The uppermost of the window and its owned popups is the answer. */
    for (ULONG at = WinQueryWindow(HWND_DESKTOP, 2); at; at = WinQueryWindow(at, 0))
    {
        if (at == target)
            return at;
        if (((ULONG)GetWindowLong(at, -16) & 0x80000000u) &&
            open32PublicWindow(WinQueryWindow(at, 4)) == target)
            return at;
    }
    return window;
}
ULONG ChildWindowFromPoint(ULONG parent, LONG x, LONG y)
{
    RECTL bounds;
    ULONG native = open32ClientWindow(parent);
    if (!WinQueryWindowRect(native, &bounds))
    {
        open32SetError(1400);
        return 0;
    }
    POINTL point = {x, bounds.yTop - y};
    ULONG child = WinWindowFromPoint(native, &point, TRUE);
    return !child ? 0 : child == native ? parent : open32PublicWindow(child);
}
ULONG FindWindow(PSZ class_name, PSZ title)
{
    if (!class_name)
    {
        open32SetError(87);
        return 0;
    }
    for (ULONG window = WinQueryWindow(HWND_DESKTOP, 2); window; window = WinQueryWindow(window, 0))
    {
        Open32Window *w = open32FindWindow(window);
        if (!w && !open32NativeDialogWindow(window))
            continue;
        char name[256], text[100];
        if ((uintptr_t)class_name <= 0xFFFF)
        {
            if (!w || w->class->atom != (ULONG)(uintptr_t)class_name)
                continue;
        }
        else
        {
            GetClassName(window, name, sizeof name);
            if (strcasecmp(name, class_name))
                continue;
        }
        if (title)
        {
            GetWindowText(window, text, sizeof text);
            if (strcmp(text, title))
                continue;
        }
        return open32PublicWindow(window);
    }
    return 0;
}
static BOOL enum_top(ULONG thread, ULONG procedure, ULONG parameter)
{
    if (!open32ValidProcedure(procedure))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG *snapshot = NULL;
    size_t count = 0, capacity = 0;
    for (ULONG window = WinQueryWindow(HWND_DESKTOP, 2); window; window = WinQueryWindow(window, 0))
    {
        if (!open32FindWindow(window) && !open32NativeDialogWindow(window))
            continue;
        ULONG pid = 0, tid = 0;
        if (thread && (!WinQueryWindowProcess(window, &pid, &tid) || tid != thread))
            continue;
        if (count == capacity)
        {
            size_t next = capacity ? capacity * 2 : 32;
            ULONG *grown = realloc(snapshot, next * sizeof *grown);
            if (!grown)
            {
                free(snapshot);
                open32SetError(8);
                return FALSE;
            }
            snapshot = grown;
            capacity = next;
        }
        snapshot[count++] = open32PublicWindow(window);
    }
    BOOL result = TRUE;
    for (size_t i = 0; i < count; i++)
        if (IsWindow(snapshot[i]) && !lx_execution_callback32_4(GLoaderState.execution, procedure,
                                                                snapshot[i], parameter, 0, 0))
        {
            result = FALSE;
            break;
        }
    free(snapshot);
    return result;
}
BOOL EnumWindows(ULONG procedure, ULONG parameter)
{
    return enum_top(0, procedure, parameter);
}
BOOL EnumThreadWindows(ULONG thread, ULONG procedure, ULONG parameter)
{
    return enum_top(thread, procedure, parameter);
}
LONG MapWindowPoints(ULONG from, ULONG to, PVOID points, ULONG count)
{
    if (!points || !count || count > 65536)
    {
        open32SetError(87);
        return 0;
    }
    uint8 origin[8] = {0};
    if (from && !ClientToScreen(from, origin))
        return 0;
    if (to && !ScreenToClient(to, origin))
        return 0;
    LONG dx = (LONG)lx_guest_load_u32(origin), dy = (LONG)lx_guest_load_u32(origin + 4);
    for (ULONG i = 0; i < count; i++)
    {
        uint8 *point = (uint8 *)points + i * 8;
        lx_guest_store_u32(point, lx_guest_load_u32(point) + (ULONG)dx);
        lx_guest_store_u32(point + 4, lx_guest_load_u32(point + 4) + (ULONG)dy);
    }
    return (LONG)((ULONG)(USHORT)dx | (ULONG)(USHORT)dy << 16);
}
USHORT GetWindowWord(ULONG handle, LONG index)
{
    Open32Window *w = open32FindWindow(handle);
    if (index >= 0 && w && index <= (LONG)w->class->window_extra - 2)
        return lx_guest_load_u16(w->extra + index);
    if (index == -12)
        return (USHORT)GetWindowLong(handle, index);
    if (index == -6)
        return (USHORT)GetWindowLong(handle, index);
    if (index == -8)
        return (USHORT)GetWindowLong(handle, index);
    if (!w && index >= 0)
        return WinQueryWindowUShort(open32ClientWindow(handle), index);
    open32SetError(1413);
    return 0;
}
USHORT SetWindowWord(ULONG handle, LONG index, USHORT value)
{
    USHORT old = GetWindowWord(handle, index);
    Open32Window *w = open32FindWindow(handle);
    if (index >= 0 && w && index <= (LONG)w->class->window_extra - 2)
        lx_guest_store_u16(w->extra + index, value);
    else if (index == -12 || index == -6 || index == -8)
        SetWindowLong(handle, index, value);
    else if (!w && index >= 0)
        WinSetWindowUShort(open32ClientWindow(handle), index, value);
    else
        open32SetError(1413);
    return old;
}
LONG GetDlgCtrlID(ULONG window)
{
    return (LONG)WinQueryWindowUShort(open32ClientWindow(window), -1);
}
BOOL CheckRadioButton(ULONG dialog, LONG first, LONG last, LONG checked)
{
    BOOL result = TRUE;
    for (int64_t id = first; id <= last; id++)
        result = CheckDlgButton(dialog, (LONG)id, id == checked);
    return result;
}
ULONG GetDlgItemInt(ULONG dialog, LONG id, PVOID translated, BOOL sign)
{
    char text[64], *end = text;
    ULONG result = 0;
    BOOL success = FALSE;
    if (GetDlgItemText(dialog, id, text, sizeof text))
    {
        if (sign)
        {
            long long n = strtoll(text, &end, 10);
            success = end != text && n >= INT32_MIN && n <= INT32_MAX;
            result = (ULONG)n;
        }
        else
        {
            unsigned long long n = strtoull(text, &end, 10);
            success = end != text && *text != '-' && n <= UINT32_MAX;
            result = (ULONG)n;
        }
    }
    if (translated)
        lx_guest_store_u32(translated, success);
    return success ? result : 0;
}
BOOL SetDlgItemInt(ULONG dialog, LONG id, ULONG value, BOOL sign)
{
    char text[32];
    if (sign)
        snprintf(text, sizeof text, "%d", (LONG)value);
    else
        snprintf(text, sizeof text, "%u", value);
    return SetDlgItemText(dialog, id, text);
}
ULONG GetNextDlgTabItem(ULONG dialog, ULONG control, BOOL previous)
{
    ULONG first = WinQueryWindow(open32ClientWindow(dialog), 2), candidate = 0;
    BOOL passed = !control;
    for (ULONG child = first; child; child = WinQueryWindow(child, 0))
    {
        ULONG style = (ULONG)GetWindowLong(child, -16);
        BOOL eligible = (style & 0x10010000u) == 0x10010000u && !(style & 0x08000000u);
        if (child == control)
        {
            if (previous && candidate)
                return candidate;
            passed = TRUE;
            continue;
        }
        if (eligible)
        {
            if (passed && !previous)
                return open32PublicWindow(child);
            candidate = open32PublicWindow(child);
        }
    }
    if (previous)
        return candidate;
    for (ULONG child = first; child && child != control; child = WinQueryWindow(child, 0))
    {
        ULONG style = (ULONG)GetWindowLong(child, -16);
        if ((style & 0x10010000u) == 0x10010000u && !(style & 0x08000000u))
            return open32PublicWindow(child);
    }
    return control;
}
BOOL MapDialogRect(ULONG dialog, PVOID rectangle)
{
    if (!rectangle || !IsWindow(dialog))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG units = GetDialogBaseUnits();
    for (unsigned i = 0; i < 4; i++)
    {
        LONG n = (LONG)lx_guest_load_u32((uint8 *)rectangle + i * 4);
        LONG unit = i & 1 ? units >> 16 : units & 0xFFFF;
        lx_guest_store_u32((uint8 *)rectangle + i * 4,
                           (ULONG)((int64_t)n * unit / (i & 1 ? 8 : 4)));
    }
    return TRUE;
}
LONG MessageBox(ULONG owner, PSZ text, PSZ caption, ULONG flags)
{
    /* Windows and PM number the button, icon and modality groups differently.
       An unknown group selects the default; result numbers coincide through IDNO. */
    static const ULONG buttons[] = {0, 1, 3, 5, 4, 2};
    static const ULONG icons[] = {0, 0x40, 0x10, 0x20, 0x30};
    ULONG group = flags & 15, icon = (flags >> 4) & 15, button = (flags >> 8) & 15;
    ULONG native = 0x4000 | (group < 6 ? buttons[group] : 0) | (icon < 5 ? icons[icon] : 0) |
                   (button == 1 || button == 2 ? button << 8 : 0) |
                   ((flags & 0x3000) == 0x1000 ? 0x1000 : 0);
    ULONG result = WinMessageBox(HWND_DESKTOP, owner ? owner : HWND_DESKTOP, text ? text : "",
                                 caption ? caption : "", 0, native);
    static const ULONG answers[] = {0, 1, 2, 3, 4, 5, 6, 7};
    return result < sizeof answers / sizeof *answers ? answers[result] : 0;
}

typedef struct DeferredWindow
{
    ULONG window, after, flags;
    LONG x, y, w, h;
} DeferredWindow;
typedef struct DeferredBatch
{
    size_t count, capacity;
    DeferredWindow *items;
    ULONG handle;
    void *owner;
    struct DeferredBatch *next;
} DeferredBatch;
static _Thread_local DeferredBatch *deferred_batches;
static void destroy_defer(void *resource)
{
    DeferredBatch *batch = resource;
    DeferredBatch **link = &deferred_batches;
    while (*link && *link != batch)
        link = &(*link)->next;
    if (*link)
        *link = batch->next;
    free(batch->items);
    free(batch);
}
ULONG BeginDeferWindowPos(LONG count)
{
    if (count < 0 || count > 65536)
    {
        open32SetError(87);
        return 0;
    }
    DeferredBatch *batch = calloc(1, sizeof *batch);
    if (batch)
    {
        batch->capacity = count ? (size_t)count : 8;
        batch->items = calloc(batch->capacity, sizeof *batch->items);
    }
    ULONG handle =
        batch && batch->items ? lx_handle_create(LX_HANDLE_OPEN32_DEFER, batch, destroy_defer) : 0;
    if (!handle)
    {
        if (batch)
            destroy_defer(batch);
        open32SetError(8);
    }
    if (handle)
    {
        batch->handle = handle;
        batch->owner = lxHostThreadState();
        batch->next = deferred_batches;
        deferred_batches = batch;
    }
    return handle;
}
ULONG DeferWindowPos(ULONG handle, ULONG window, ULONG after, LONG x, LONG y, LONG width,
                     LONG height, ULONG flags)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DEFER, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    DeferredBatch *batch = borrow.resource;
    if (batch->owner != lxHostThreadState())
    {
        lx_handle_release(&borrow);
        open32SetError(6);
        return 0;
    }
    BOOL valid = IsWindow(window);
    if (valid && batch->count == batch->capacity)
    {
        size_t capacity = batch->capacity * 2;
        DeferredWindow *items =
            capacity <= 65536 ? realloc(batch->items, capacity * sizeof *items) : NULL;
        if (!items)
            valid = FALSE;
        else
        {
            batch->items = items;
            batch->capacity = capacity;
        }
    }
    if (valid)
        batch->items[batch->count++] = (DeferredWindow){window, after, flags, x, y, width, height};
    lx_handle_release(&borrow);
    if (!valid)
    {
        lx_handle_close(handle, LX_HANDLE_OPEN32_DEFER);
        open32SetError(1400);
        return 0;
    }
    return handle;
}
BOOL EndDeferWindowPos(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DEFER, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    DeferredBatch *batch = borrow.resource;
    if (batch->owner != lxHostThreadState())
    {
        lx_handle_release(&borrow);
        open32SetError(6);
        return FALSE;
    }
    BOOL valid = TRUE;
    for (size_t i = 0; i < batch->count; i++)
        if (!IsWindow(batch->items[i].window))
            valid = FALSE;
    if (valid)
        for (size_t i = 0; i < batch->count; i++)
        {
            DeferredWindow *w = batch->items + i;
            if (!SetWindowPos(w->window, w->after, w->x, w->y, w->w, w->h, w->flags))
            {
                valid = FALSE;
                break;
            }
        }
    lx_handle_release(&borrow);
    lx_handle_close(handle, LX_HANDLE_OPEN32_DEFER);
    return valid;
}

LONG GetPriorityClipboardFormat(PVOID formats, LONG count)
{
    if (count < 0 || (count && !formats))
    {
        open32SetError(87);
        return -1;
    }
    if (!CountClipboardFormats())
        return 0;
    for (LONG i = 0; i < count; i++)
    {
        ULONG format = lx_guest_load_u32((uint8 *)formats + i * 4);
        if (IsClipboardFormatAvailable(format))
            return (LONG)format;
    }
    return -1;
}
BOOL TranslateMDISysAccel(ULONG client, PVOID message)
{
    if (!message)
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG kind = lx_guest_load_u32((uint8 *)message + 4),
          key = lx_guest_load_u32((uint8 *)message + 8);
    if ((kind != 0x100 && kind != 0x104) || !(GetKeyState(17) & 0x8000))
        return FALSE;
    ULONG active = (ULONG)SendMessage(client, 0x229, 0, 0);
    if (!active)
        return FALSE;
    if (key == 0x73)
        SendMessage(client, 0x221, active, 0); /* Ctrl+F4 */
    else if (key == 0x75 || key == 9)
        SendMessage(client, 0x224, active, (GetKeyState(16) & 0x8000) != 0);
    else
        return FALSE;
    return TRUE;
}
BOOL IsDialogMessage(ULONG dialog, PVOID message)
{
    if (!message || !IsWindow(dialog))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG target = lx_guest_load_u32(message), kind = lx_guest_load_u32((uint8 *)message + 4),
          key = lx_guest_load_u32((uint8 *)message + 8);
    if (target != dialog && !IsChild(dialog, target))
        return FALSE;
    if (kind == 0x100)
    {
        ULONG code = (ULONG)SendMessage(target, 0x87, key, 0);
        if (code & 4)
            return FALSE;
        if (key == 9 && !(code & 2))
        {
            ULONG next = GetNextDlgTabItem(dialog, target, (GetKeyState(16) & 0x8000) != 0);
            if (next)
                SetFocus(next);
            return TRUE;
        }
        if ((key == 13 || key == 27) && !(code & 4))
        {
            ULONG id = key == 13 ? 1 : 2;
            SendMessage(dialog, 0x111, id, GetDlgItem(dialog, id));
            return TRUE;
        }
    }
    TranslateMessage(message);
    DispatchMessage(message);
    return TRUE;
}
BOOL DlgDirSelectEx(ULONG dialog, PSZ output, LONG capacity, LONG id)
{
    if (!output || capacity <= 0)
    {
        open32SetError(87);
        return FALSE;
    }
    LONG index = SendDlgItemMessage(dialog, id, 0x188, 0, 0);
    if (index < 0)
    {
        output[0] = 0;
        return FALSE;
    }
    LONG length = SendDlgItemMessage(dialog, id, 0x18A, index, 0);
    if (length < 0 || length > 32767)
    {
        output[0] = 0;
        return FALSE;
    }
    LxGuestMapping *mapping =
        lx_execution_map(GLoaderState.execution, 0, ((size_t)length + 4096) & ~(size_t)4095, 3, 0);
    if (!mapping)
    {
        open32SetError(8);
        return FALSE;
    }
    char *text = lx_execution_mapping_host(mapping);
    SendDlgItemMessage(dialog, id, 0x189, index, lx_execution_mapping_address(mapping));
    BOOL directory = length > 1 && text[0] == '[' && text[length - 1] == ']';
    if (directory)
    {
        text[length - 1] = 0;
        memmove(text, text + 1, length--);
        if (length >= 3 && text[0] == '-' && text[length - 2] == '-')
        {
            text[0] = text[1];
            text[1] = ':';
            text[2] = 0;
        }
        else
        {
            text[length - 1] = '\\';
            text[length] = 0;
        }
    }
    size_t count = strlen(text);
    if (count >= (size_t)capacity)
        count = capacity - 1;
    memcpy(output, text, count);
    output[count] = 0;
    lx_execution_unmap(GLoaderState.execution, mapping);
    return directory;
}

void open32DeferredCleanup(void)
{
    while (deferred_batches)
    {
        DeferredBatch *batch = deferred_batches;
        deferred_batches = batch->next;
        lx_handle_close(batch->handle, LX_HANDLE_OPEN32_DEFER);
    }
}
