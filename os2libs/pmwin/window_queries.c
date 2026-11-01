/* Public domain. No rights reserved. Wholly AI-authored. */
/* Enumerations retain a snapshot of immediate child handles in snapshotted z order. */
#include "os2libs/pmwin/internal.h"
#include "guest/handles.h"

typedef struct PmWindowEnumeration
{
    AnchorBlock *anchor;
    HENUM handle;
    HWND *windows;
    size_t count, position;
    struct PmWindowEnumeration *next;
} PmWindowEnumeration;

static void enumeration_destroy(void *value)
{
    PmWindowEnumeration *e = value;
    PmWindowEnumeration **slot = &e->anchor->enumerations;
    while (*slot && *slot != e)
        slot = &(*slot)->next;
    if (*slot)
        *slot = e->next;
    free(e->windows);
    free(e);
}
HENUM WinBeginEnumWindows(HWND parent)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, parent) : NULL;
    if (!w)
        return 0;
    PmWindowEnumeration *e = calloc(1, sizeof *e);
    if (!e)
        return 0;
    for (Window *child = w->children; child; child = child->sibling)
        e->count++;
    if (e->count)
    {
        e->windows = malloc(e->count * sizeof *e->windows);
        if (!e->windows)
        {
            free(e);
            return 0;
        }
        size_t i = 0;
        for (Window *child = w->children; child; child = child->sibling)
            e->windows[i++] = child->hwnd;
    }
    e->anchor = anchor;
    e->handle = lx_handle_create(LX_HANDLE_PM_ENUM, e, enumeration_destroy);
    if (!e->handle)
    {
        free(e->windows);
        free(e);
        return 0;
    }
    e->next = anchor->enumerations;
    anchor->enumerations = e;
    return e->handle;
}
HWND WinGetNextWindow(HENUM handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    LxHandleBorrow borrow = {0};
    HWND result = 0;
    if (anchor && lx_handle_borrow(handle, LX_HANDLE_PM_ENUM, &borrow))
    {
        PmWindowEnumeration *e = borrow.resource;
        if (e->anchor == anchor)
        {
            if (e->position < e->count)
                result = e->windows[e->position++];
            else
                e->position = 0; /* The call after end-of-list starts over. */
        }
        else
            anchor->last_error = PMERR_INVALID_HENUM;
    }
    else if (anchor)
        anchor->last_error = PMERR_INVALID_HENUM;
    lx_handle_release(&borrow);
    return result;
}
BOOL WinEndEnumWindows(HENUM handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    LxHandleBorrow borrow = {0};
    BOOL result = FALSE;
    if (anchor && lx_handle_borrow(handle, LX_HANDLE_PM_ENUM, &borrow))
    {
        PmWindowEnumeration *e = borrow.resource;
        if (e->anchor == anchor)
            result = lx_handle_close(handle, LX_HANDLE_PM_ENUM);
    }
    lx_handle_release(&borrow);
    if (!result && anchor)
        anchor->last_error = PMERR_INVALID_HENUM;
    return result;
}
void destroyWindowEnumerations(AnchorBlock *anchor)
{
    while (anchor->enumerations)
        lx_handle_close(anchor->enumerations->handle, LX_HANDLE_PM_ENUM);
}
HAB WinQueryAnchorBlock(HWND window)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && getWindowFromHWND(anchor, window) ? anchor->hab : 0;
}
LONG WinQueryClassName(HWND window, LONG capacity, PCHAR output)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w || capacity <= 0 || !output)
        return 0;
    const char *name = w->window_class_name ? w->window_class_name : "";
    size_t count = strlen(name);
    if (count >= (size_t)capacity)
        count = (size_t)capacity - 1;
    memcpy(output, name, count);
    output[count] = 0;
    return (LONG)count;
}
HWND WinQueryWindow(HWND window, LONG code)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w)
        return 0;
    Window *result = NULL;
    switch (code)
    {
        case 0:
            result = w->sibling;
            break; /* QW_NEXT */
        case 1:    /* QW_PREV */
            for (Window *i = w->parent ? w->parent->children : NULL; i && i != w; i = i->sibling)
                result = i;
            break;
        case 2:
            result = w->children;
            break; /* QW_TOP */
        case 3:    /* QW_BOTTOM */
            for (Window *i = w->children; i; i = i->sibling)
                result = i;
            break;
        case 4:
            result = w->owner;
            break;
        case 5:
            result = w->parent;
            break;
        default:
            anchor->last_error = PMERR_PARAMETER_OUT_OF_RANGE;
            break;
    }
    return result ? result->hwnd : 0;
}

BOOL WinSetOwner(HWND handle, HWND owner_handle)
{
    TRACE_NATIVE("WinSetOwner(%u, %u)", handle, owner_handle);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    Window *owner = anchor && owner_handle ? getWindowFromHWND(anchor, owner_handle) : NULL;
    if (!window || window == &desktop_window || window->destroying ||
        (owner_handle && (!owner || owner->destroying)))
    {
        if (anchor)
            anchor->last_error = PMERR_INVALID_HWND;
        return FALSE;
    }
    for (Window *cursor = owner; cursor; cursor = cursor->owner)
        if (cursor == window)
        {
            anchor->last_error = PMERR_INVALID_HWND;
            return FALSE;
        }
    window->owner = owner;
    return TRUE;
}
