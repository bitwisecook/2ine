/* Public domain. No rights reserved. Wholly AI-authored. */
/* Dialogs from packed DLGTEMPLATE resources. A template is validated completely before any
   window is published. */
#include "os2/include/private/pmwinp.h"
#include "emulator/abi.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmgrep.h"

enum
{
    TEMPLATE_HEADER = offsetof(DLGTEMPLATE, adlgti),
    TEMPLATE_ITEM = sizeof(DLGTITEM),
    TEMPLATE_DEPTH = 32,
    /* An offset or item index that is absent. */
    TEMPLATE_NONE = 0xffff,
    /* Item text naming a resource: 0xff, then its USHORT id. */
    ORDINAL_TEXT_MARKER = 0xff,
    ORDINAL_TEXT_SIZE = 3,
    /* SS_* kinds occupy the low bits of a static control's style. */
    STATIC_KIND_MASK = 0x3f
};

/* PRESPARAMS: a ULONG byte count, then PARAM entries of ULONG id, ULONG cb and the value. */
enum
{
    PRESPARAMS_HEADER = 4,
    PARAM_CB_OFFSET = 4,
    PARAM_HEADER = 8
};

_Static_assert(offsetof(DLGTEMPLATE, adlgti) == 14 && sizeof(DLGTITEM) == 30,
               "DLGTEMPLATE and DLGTITEM have the guest layout");

#define ITEM_WORD(item, field) word(item, offsetof(DLGTITEM, field))
#define TEMPLATE_WORD(bytes, field) word(bytes, offsetof(DLGTEMPLATE, field))

typedef struct
{
    const uint8 *bytes;
    size_t size, count, next;
    HWND *windows;
    uint32 guest;
    PFNWP procedure;
    HMODULE module;
    PVOID parameters;
    POINTL content_origin;
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

static BOOL text_valid(const DialogTemplate *t, unsigned length, unsigned offset)
{
    if (length == ORDINAL_TEXT_SIZE && offset <= t->size && t->size - offset >= ORDINAL_TEXT_SIZE &&
        t->bytes[offset] == ORDINAL_TEXT_MARKER)
        return TRUE;
    return string_valid(t, length, offset);
}

static BOOL parameters_valid(const DialogTemplate *t, unsigned offset)
{
    if (offset == TEMPLATE_NONE)
        return TRUE;
    if (offset > t->size || t->size - offset < PRESPARAMS_HEADER)
        return FALSE;
    size_t size = lx_guest_load_u32(t->bytes + offset), at = offset + PRESPARAMS_HEADER;
    if (size > t->size - at)
        return FALSE;
    size_t end = at + size;
    while (at < end)
    {
        if (end - at < PARAM_HEADER || !lx_guest_load_u32(t->bytes + at))
            return FALSE;
        size_t bytes = lx_guest_load_u32(t->bytes + at + PARAM_CB_OFFSET);
        at += PARAM_HEADER;
        if (!bytes || bytes > end - at)
            return FALSE;
        at += bytes;
    }
    return TRUE;
}

static BOOL image_ordinal(const DialogTemplate *t, const uint8 *item)
{
    unsigned offset = ITEM_WORD(item, offText);
    unsigned kind = lx_guest_load_u32(item + offsetof(DLGTITEM, flStyle)) & STATIC_KIND_MASK;
    return !ITEM_WORD(item, cchClassName) && ITEM_WORD(item, offClassName) == PM_CLASS_STATIC &&
           (kind == SS_ICON || kind == SS_BITMAP || kind == SS_SYSICON) &&
           ITEM_WORD(item, cchText) == ORDINAL_TEXT_SIZE && offset <= t->size &&
           t->size - offset >= ORDINAL_TEXT_SIZE && t->bytes[offset] == ORDINAL_TEXT_MARKER;
}

static BOOL validate_tree(DialogTemplate *t, unsigned depth)
{
    if (depth >= TEMPLATE_DEPTH || t->next >= t->count)
        return FALSE;
    const uint8 *item = t->bytes + TEMPLATE_WORD(t->bytes, offadlgti) + t->next++ * TEMPLATE_ITEM;
    unsigned class_length = ITEM_WORD(item, cchClassName),
             class_offset = ITEM_WORD(item, offClassName);
    if (!string_valid(t, class_length, class_offset) ||
        !text_valid(t, ITEM_WORD(item, cchText), ITEM_WORD(item, offText)) ||
        (!class_length &&
         (class_offset < PM_CLASS_FRAME || class_offset > PM_LAST_PREDEFINED_CLASS)) ||
        (SHORT)ITEM_WORD(item, cx) < 0 || (SHORT)ITEM_WORD(item, cy) < 0 ||
        !parameters_valid(t, ITEM_WORD(item, offPresParams)))
        return FALSE;
    /* The root's control data is its FCF_* ULONG; a control's begins with a USHORT size. */
    unsigned control = ITEM_WORD(item, offCtlData);
    if (control != TEMPLATE_NONE && (control > t->size || t->size - control < (depth ? 2u : 4u)))
        return FALSE;
    for (unsigned i = 0; i < ITEM_WORD(item, cChildren); i++)
        if (!validate_tree(t, depth + 1))
            return FALSE;
    return TRUE;
}

static BOOL apply_parameters(HWND window, const DialogTemplate *t, unsigned offset)
{
    if (offset == TEMPLATE_NONE)
        return TRUE;
    size_t end = offset + PRESPARAMS_HEADER + lx_guest_load_u32(t->bytes + offset);
    for (size_t at = offset + PRESPARAMS_HEADER; at < end;)
    {
        ULONG id = lx_guest_load_u32(t->bytes + at),
              size = lx_guest_load_u32(t->bytes + at + PARAM_CB_OFFSET);
        if (!WinSetPresParam(window, id, size, (void *)(t->bytes + at + PARAM_HEADER)))
            return FALSE;
        at += PARAM_HEADER + size;
    }
    return TRUE;
}

static LxPmDialogClassFactory dialog_class_factory;

/* A class outside PM's table can still be provided by the application's API layer. */
void lx_pm_set_dialog_class_factory(LxPmDialogClassFactory factory)
{
    dialog_class_factory = factory;
}

static HWND create_item(DialogTemplate *t, HWND parent, HWND owner, HWND root)
{
    size_t index = t->next++;
    const uint8 *item = t->bytes + TEMPLATE_WORD(t->bytes, offadlgti) + index * TEMPLATE_ITEM;
    unsigned class_offset = ITEM_WORD(item, offClassName), text_offset = ITEM_WORD(item, offText);
    PSZ class_name = ITEM_WORD(item, cchClassName)
                         ? (PSZ)(t->bytes + class_offset)
                         : (PSZ)(uintptr_t)(PM_INTEGER_ATOM_BASE | class_offset);
    PSZ text = ITEM_WORD(item, cchText) ? (PSZ)(t->bytes + text_offset) : "";
    char resource_name[16];
    if (ITEM_WORD(item, cchText) == ORDINAL_TEXT_SIZE &&
        t->bytes[text_offset] == ORDINAL_TEXT_MARKER)
    {
        snprintf(resource_name, sizeof resource_name, "#%u", word(t->bytes, text_offset + 1));
        text = resource_name;
    }
    POINTL position[2] = {{(SHORT)ITEM_WORD(item, x), (SHORT)ITEM_WORD(item, y)},
                          {(SHORT)ITEM_WORD(item, cx), (SHORT)ITEM_WORD(item, cy)}};
    unsigned control = ITEM_WORD(item, offCtlData);
    PVOID control_data = control == TEMPLATE_NONE ? NULL : (PVOID)(t->bytes + control);
    FRAMECDATA frame = {
        sizeof frame, index || control == TEMPLATE_NONE ? 0 : lx_guest_load_u32(t->bytes + control),
        0, ITEM_WORD(item, id)};
    if (index)
    {
        if (!WinMapDlgPoints(root, position, 2, TRUE))
            return 0;
        if (parent == root)
        {
            position[0].x += t->content_origin.x;
            position[0].y += t->content_origin.y;
        }
        AnchorBlock *anchor = getAnchorBlockNoHAB();
        WindowClass *class = findRegisteredClass(anchor, class_name);
        if (control_data && t->guest && class && (uintptr_t)class->window_proc <= UINT32_MAX &&
            lx_execution_memory(GProcessState.execution, (uint32)(uintptr_t)class->window_proc, 1,
                                LX_GUEST_EXECUTE))
            control_data = (PVOID)(uintptr_t)(t->guest + control);
    }
    HWND handle = 0;
    if (index && dialog_class_factory && !findRegisteredClass(getAnchorBlockNoHAB(), class_name))
        handle = dialog_class_factory(
            parent, class_name, text, lx_guest_load_u32(item + offsetof(DLGTITEM, flStyle)),
            position[0].x, position[0].y, position[1].x, position[1].y, ITEM_WORD(item, id));
    else
        handle = WinCreateWindow(
            parent, class_name, text, lx_guest_load_u32(item + offsetof(DLGTITEM, flStyle)),
            position[0].x, position[0].y, position[1].x, position[1].y, owner, HWND_BOTTOM,
            ITEM_WORD(item, id), index ? control_data : &frame, NULL);
    if (!handle)
        return 0;
    t->windows[index] = handle;
    if (image_ordinal(t, item) &&
        !controlResourcePointer(handle, t->module, word(t->bytes + text_offset, 1)))
        goto failed;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = getWindowFromHWND(anchor, handle);
    if (!index && !initializeDialog(window))
        goto failed;
    if (!apply_parameters(handle, t, ITEM_WORD(item, offPresParams)))
        goto failed;
    if (!index)
    {
        root = handle;
        if (!WinMapDlgPoints(root, position, 2, TRUE))
            goto failed;
        RECTL frame_rect = {0, 0, position[1].x, position[1].y};
        if (!WinCalcFrameRect(handle, &frame_rect, FALSE))
            goto failed;
        t->content_origin = (POINTL){-frame_rect.xLeft, -frame_rect.yBottom};
        if (!WinSetWindowPos(handle, HWND_TOP, position[0].x, position[0].y,
                             frame_rect.xRight - frame_rect.xLeft,
                             frame_rect.yTop - frame_rect.yBottom,
                             SWP_MOVE | SWP_SIZE | SWP_NOADJUST | SWP_NOREDRAW))
            goto failed;
        window->window_proc = t->procedure ? t->procedure : WinDefDlgProc;
    }
    for (unsigned i = 0; i < ITEM_WORD(item, cChildren); i++)
        if (!create_item(t, handle, root, root))
            goto failed;
    return handle;
failed:
    WinDestroyWindow(handle);
    return 0;
}

HWND createDialog(HWND parent, HWND owner, PFNWP procedure, const uint8 *data, size_t size,
                  PVOID parameters, HMODULE module)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !data || size < TEMPLATE_HEADER)
        return 0;
    DialogTemplate t = {.bytes = data,
                        .size = TEMPLATE_WORD(data, cbTemplate),
                        .procedure = procedure,
                        .parameters = parameters,
                        .module = module};
    unsigned items = TEMPLATE_WORD(data, offadlgti);
    if (t.size > size || t.size < TEMPLATE_HEADER || items < TEMPLATE_HEADER || items > t.size ||
        TEMPLATE_WORD(data, type) ||
        (TEMPLATE_WORD(data, codepage) != 437 && TEMPLATE_WORD(data, codepage) != 850 &&
         TEMPLATE_WORD(data, codepage) != 0) ||
        TEMPLATE_WORD(data, coffPresParams))
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_RESOURCE_FORMAT, 0);
    t.count = (t.size - items) / TEMPLATE_ITEM;
    /* The root item must be a WC_FRAME. */
    if (!validate_tree(&t, 0) || !t.next || ITEM_WORD(data + items, cchClassName) ||
        ITEM_WORD(data + items, offClassName) != PM_CLASS_FRAME)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_RESOURCE_FORMAT, 0);
    t.count = t.next;
    t.next = 0;
    if (TEMPLATE_WORD(data, iItemFocus) != TEMPLATE_NONE &&
        TEMPLATE_WORD(data, iItemFocus) >= t.count)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_RESOURCE_FORMAT, 0);
    t.windows = calloc(t.count, sizeof *t.windows);
    if (!t.windows)
        return 0;
    /* A callback's control pointer must address guest memory, never a host copy. */
    LxGuestMapping *mapping = NULL;
    if (GProcessState.execution)
    {
        mapping =
            lx_execution_map(GProcessState.execution, 0,
                             (t.size + PM_GUEST_PAGE_SIZE - 1) & ~(size_t)(PM_GUEST_PAGE_SIZE - 1),
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
    while (actual_owner && actual_owner != parent_window && actual_owner->parent != parent_window)
        actual_owner = actual_owner->parent;
    HWND result = create_item(&t, parent, actual_owner ? actual_owner->hwnd : 0, 0);
    if (result)
    {
        HWND focus = 0;
        unsigned desired = TEMPLATE_WORD(data, iItemFocus);
        if (desired != TEMPLATE_NONE)
            focus = t.windows[desired];
        else
            for (size_t i = 1; i < t.count; i++)
                if (WinQueryWindowULong(t.windows[i], QWL_STYLE) & WS_TABSTOP)
                {
                    focus = t.windows[i];
                    break;
                }
        if (!initializeDialogProcedure(result, focus, parameters))
            result = 0;
        if (result)
        {
            Window *window = getWindowFromHWND(anchor, result);
            if (window && (window->style & WS_VISIBLE))
                postExposedMessages(anchor, window, currentSystemTicks());
        }
    }
    if (mapping)
        lx_execution_unmap(GProcessState.execution, mapping);
    free(t.windows);
    return result;
}
