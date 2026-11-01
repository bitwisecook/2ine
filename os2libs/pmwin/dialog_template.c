/* Public domain. No rights reserved. Wholly AI-authored. */
/* Packed templates are validated completely before any window is published. */
#include "os2libs/pmwin/internal.h"
#include "guest/abi.h"

enum
{
    TEMPLATE_HEADER = 14,
    TEMPLATE_ITEM = 30,
    TEMPLATE_DEPTH = 32
};
typedef struct
{
    const uint8 *bytes;
    size_t size, count, next;
    HWND *windows;
    uint32 guest;
    PFNWP procedure;
    PVOID parameters;
} DialogTemplate;
static USHORT word(const uint8 *p, size_t offset)
{
    return lx_guest_load_u16(p + offset);
}
static BOOL string_valid(const DialogTemplate *t, unsigned length, unsigned offset)
{
    return !length || (offset < t->size && length < t->size - offset &&
                       t->bytes[offset + length] == 0 && !memchr(t->bytes + offset, 0, length));
}
static BOOL parameters_valid(const DialogTemplate *t, unsigned offset)
{
    if (offset == 0xffff)
        return TRUE;
    if (offset > t->size || t->size - offset < 4)
        return FALSE;
    size_t size = lx_guest_load_u32(t->bytes + offset), at = offset + 4;
    if (size > t->size - at)
        return FALSE;
    size_t end = at + size;
    while (at < end)
    {
        if (end - at < 8 || !lx_guest_load_u32(t->bytes + at))
            return FALSE;
        size_t bytes = lx_guest_load_u32(t->bytes + at + 4);
        at += 8;
        if (!bytes || bytes > end - at)
            return FALSE;
        at += bytes;
    }
    return TRUE;
}
static BOOL validate_tree(DialogTemplate *t, unsigned depth)
{
    if (depth >= TEMPLATE_DEPTH || t->next >= t->count)
        return FALSE;
    const uint8 *item = t->bytes + word(t->bytes, 6) + t->next++ * TEMPLATE_ITEM;
    unsigned class_length = word(item, 4), class_offset = word(item, 6);
    if (!string_valid(t, class_length, class_offset) ||
        !string_valid(t, word(item, 8), word(item, 10)) ||
        (!class_length && (class_offset < 1 || class_offset > 0x70)) || (SHORT)word(item, 20) < 0 ||
        (SHORT)word(item, 22) < 0 || !parameters_valid(t, word(item, 26)))
        return FALSE;
    unsigned control = word(item, 28);
    if (control != 0xffff && (control > t->size || t->size - control < (depth ? 2u : 4u)))
        return FALSE;
    for (unsigned i = 0; i < word(item, 2); i++)
        if (!validate_tree(t, depth + 1))
            return FALSE;
    return TRUE;
}
static BOOL apply_parameters(HWND window, const DialogTemplate *t, unsigned offset)
{
    if (offset == 0xffff)
        return TRUE;
    size_t end = offset + 4 + lx_guest_load_u32(t->bytes + offset);
    for (size_t at = offset + 4; at < end;)
    {
        ULONG id = lx_guest_load_u32(t->bytes + at), size = lx_guest_load_u32(t->bytes + at + 4);
        if (!WinSetPresParam(window, id, size, (void *)(t->bytes + at + 8)))
            return FALSE;
        at += 8 + size;
    }
    return TRUE;
}
static HWND create_item(DialogTemplate *t, HWND parent, HWND owner, HWND root)
{
    size_t index = t->next++;
    const uint8 *item = t->bytes + word(t->bytes, 6) + index * TEMPLATE_ITEM;
    unsigned class_offset = word(item, 6), text_offset = word(item, 10);
    PSZ class_name = word(item, 4) ? (PSZ)(t->bytes + class_offset)
                                   : (PSZ)(uintptr_t)(0xffff0000u | class_offset);
    PSZ text = word(item, 8) ? (PSZ)(t->bytes + text_offset) : "";
    POINTL position[2] = {{(SHORT)word(item, 16), (SHORT)word(item, 18)},
                          {(SHORT)word(item, 20), (SHORT)word(item, 22)}};
    unsigned control = word(item, 28);
    PVOID control_data = control == 0xffff ? NULL : (PVOID)(t->bytes + control);
    FRAMECDATA frame = {sizeof frame,
                        index || control == 0xffff ? 0 : lx_guest_load_u32(t->bytes + control), 0,
                        word(item, 24)};
    if (index)
    {
        if (!WinMapDlgPoints(root, position, 2, TRUE))
            return 0;
        AnchorBlock *anchor = getAnchorBlockNoHAB();
        WindowClass *class = findRegisteredClass(anchor, class_name);
        if (control_data && t->guest && class && (uintptr_t)class->window_proc <= UINT32_MAX &&
            lx_execution_memory(GLoaderState.execution, (uint32)(uintptr_t)class->window_proc, 1,
                                LX_GUEST_EXECUTE))
            control_data = (PVOID)(uintptr_t)(t->guest + control);
    }
    HWND handle = WinCreateWindow(parent, class_name, text, lx_guest_load_u32(item + 12),
                                  position[0].x, position[0].y, position[1].x, position[1].y, owner,
                                  HWND_BOTTOM, word(item, 24), index ? control_data : &frame, NULL);
    if (!handle)
        return 0;
    t->windows[index] = handle;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = getWindowFromHWND(anchor, handle);
    if (!index && !initializeDialog(window))
        goto failed;
    if (!apply_parameters(handle, t, word(item, 26)))
        goto failed;
    if (!index)
    {
        root = handle;
        if (!WinMapDlgPoints(root, position, 2, TRUE) ||
            !WinSetWindowPos(handle, HWND_TOP, position[0].x, position[0].y, position[1].x,
                             position[1].y, SWP_MOVE | SWP_SIZE | SWP_NOADJUST | SWP_NOREDRAW))
            goto failed;
        window->window_proc = t->procedure ? t->procedure : WinDefDlgProc;
    }
    for (unsigned i = 0; i < word(item, 2); i++)
        if (!create_item(t, handle, root, root))
            goto failed;
    return handle;
failed:
    WinDestroyWindow(handle);
    return 0;
}
HWND createDialog(HWND parent, HWND owner, PFNWP procedure, const uint8 *data, size_t size,
                  PVOID parameters)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !data || size < TEMPLATE_HEADER)
        return 0;
    DialogTemplate t = {
        .bytes = data, .size = word(data, 0), .procedure = procedure, .parameters = parameters};
    unsigned items = word(data, 6);
    if (t.size > size || t.size < TEMPLATE_HEADER || items < TEMPLATE_HEADER || items > t.size ||
        word(data, 2) || (word(data, 4) != 437 && word(data, 4) != 0) || word(data, 12))
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_RESOURCE_FORMAT, 0);
    t.count = (t.size - items) / TEMPLATE_ITEM;
    if (!validate_tree(&t, 0) || !t.next || word(data + items, 4) || word(data + items, 6) != 1)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_RESOURCE_FORMAT, 0);
    t.count = t.next;
    t.next = 0;
    if (word(data, 10) != 0xffff && word(data, 10) >= t.count)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_RESOURCE_FORMAT, 0);
    t.windows = calloc(t.count, sizeof *t.windows);
    if (!t.windows)
        return 0;
    /* A callback's control pointer must address guest memory, never a host copy. */
    LxGuestMapping *mapping = NULL;
    if (GLoaderState.execution)
    {
        mapping = lx_execution_map(GLoaderState.execution, 0, (t.size + 4095) & ~(size_t)4095,
                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
        if (!mapping)
        {
            free(t.windows);
            return 0;
        }
        memcpy(lx_execution_mapping_host(mapping), data, t.size);
        t.guest = lx_execution_mapping_address(mapping);
    }
    Window *actual_owner = getWindowFromHWND(anchor, owner);
    Window *parent_window = getWindowFromHWND(anchor, parent);
    while (actual_owner && actual_owner->parent != parent_window)
        actual_owner = actual_owner->parent;
    HWND result = create_item(&t, parent, actual_owner ? actual_owner->hwnd : 0, 0);
    if (result)
    {
        HWND focus = 0;
        unsigned desired = word(data, 10);
        if (desired != 0xffff)
            focus = t.windows[desired];
        else
            for (size_t i = 1; i < t.count; i++)
                if (WinQueryWindowULong(t.windows[i], -2) & 0x10000) /* WS_TABSTOP */
                {
                    focus = t.windows[i];
                    break;
                }
        if (!initializeDialogProcedure(result, focus, parameters))
            result = 0;
    }
    if (mapping)
        lx_execution_unmap(GLoaderState.execution, mapping);
    free(t.windows);
    return result;
}
