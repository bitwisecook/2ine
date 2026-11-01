/* Public domain. No rights reserved. Wholly AI-authored. */
/* System cursor handles retain the host cursor and distinguish shared resources from copies. */
#include "os2libs/pmwin/internal.h"
typedef struct PmPointer
{
    struct PmPointer *next;
    AnchorBlock *anchor;
    HPOINTER handle;
    LONG id;
    BOOL copy;
    LxHostPointer *host;
} PmPointer;
static void release_pointer(void *resource)
{
    PmPointer *pointer = resource;
    PmPointer **link = &pointer->anchor->pointers;
    while (*link && *link != pointer)
        link = &(*link)->next;
    if (*link)
        *link = pointer->next;
    lx_host_pointer_destroy(pointer->host);
    free(pointer);
}
void destroyPointers(AnchorBlock *anchor)
{
    while (anchor->pointers)
        lx_handle_close(anchor->pointers->handle, LX_HANDLE_PM_POINTER);
}
HPOINTER WinLoadPointer(HWND desktop, HMODULE module, ULONG id)
{
    TRACE_NATIVE("WinLoadPointer(%u, %u, %u)", (uint)desktop, (uint)module, (uint)id);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    /* Resource cursors are unavailable; callers may retain a system pointer. */
    SET_WIN_ERROR_AND_RETURN(
        anchor, desktop == HWND_DESKTOP ? PMERR_RESOURCE_NOT_FOUND : PMERR_INVALID_HWND, 0);
}
HPOINTER WinQuerySysPointer(HWND desktop, LONG id, BOOL copy)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || !((id >= 1 && id <= 9) || id == 18))
        return 0;
    for (PmPointer *p = anchor->pointers; p && !copy; p = p->next)
        if (!p->copy && p->id == id)
            return p->handle;
    PmPointer *p = calloc(1, sizeof *p);
    if (!p)
        return 0;
    p->host = lx_host_pointer_create(id == 18 ? LX_HOST_POINTER_ILLEGAL : id - 1);
    if (!p->host)
    {
        free(p);
        return 0;
    }
    p->anchor = anchor;
    p->id = id;
    p->copy = copy;
    p->handle = lx_handle_create(LX_HANDLE_PM_POINTER, p, release_pointer);
    if (!p->handle)
    {
        lx_host_pointer_destroy(p->host);
        free(p);
        return 0;
    }
    p->next = anchor->pointers;
    anchor->pointers = p;
    return p->handle;
}
BOOL WinSetPointer(HWND desktop, HPOINTER handle)
{
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB())
        return FALSE;
    if (!handle)
        return lx_host_pointer_set(NULL);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    BOOL result = lx_host_pointer_set(((PmPointer *)borrow.resource)->host);
    lx_handle_release(&borrow);
    return result;
}
BOOL WinDestroyPointer(HPOINTER handle)
{
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    BOOL copy = ((PmPointer *)borrow.resource)->copy;
    lx_handle_release(&borrow);
    return copy && lx_handle_close(handle, LX_HANDLE_PM_POINTER);
}

BOOL WinSetPointerPos(HWND desktop, LONG x, LONG y)
{
    return desktop == HWND_DESKTOP && getAnchorBlockNoHAB() &&
           lx_host_mouse_warp(x, desktop_window.h - 1 - y);
}
BOOL WinShowPointer(HWND desktop, BOOL show)
{
    return desktop == HWND_DESKTOP && getAnchorBlockNoHAB() && lx_host_pointer_show(show != 0);
}
BOOL WinShowCursor(HWND handle, BOOL show)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    /* Text carets are painted by their control; absent carets cannot be shown. */
    return window && !show;
}
