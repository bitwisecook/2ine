/* Public domain. No rights reserved. Wholly AI-authored. */
/* Display-compatible contexts have typed identities and cannot close while associated. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
static void destroy_dc(void *resource)
{
    PmDeviceContext *dc = resource;
    AnchorBlock *anchor = dc->anchor;
    PmDeviceContext **slot = &anchor->devices;
    while (*slot && *slot != dc)
        slot = &(*slot)->next;
    if (*slot)
        *slot = dc->next;
    free(dc);
}
HDC lx_pm_open_dc(HAB hab, LONG type, HDC compatible)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || (type != 0 && type != 6 && type != 8) ||
        (compatible && !lx_pm_dc_valid(compatible)))
        return 0;
    PmDeviceContext *dc = calloc(1, sizeof *dc);
    if (!dc)
        return 0;
    dc->anchor = anchor;
    dc->type = type;
    dc->handle = lx_handle_create(LX_HANDLE_PM_DC, dc, destroy_dc);
    if (!dc->handle)
    {
        free(dc);
        return 0;
    }
    dc->next = anchor->devices;
    anchor->devices = dc;
    return dc->handle;
}
BOOL lx_pm_dc_valid(HDC handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return FALSE;
    if (getWindowFromHWND(anchor, handle))
        return TRUE;
    LxHandleBorrow borrow = {0};
    BOOL valid = FALSE;
    if (lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
        valid = ((PmDeviceContext *)borrow.resource)->anchor == anchor;
    lx_handle_release(&borrow);
    return valid;
}
static int unused_dc(void *resource)
{
    return ((PmDeviceContext *)resource)->spaces == 0;
}
LONG lx_pm_close_dc(HDC handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    LxHandleBorrow borrow = {0};
    LONG result = -1;
    if (anchor && lx_handle_borrow(handle, LX_HANDLE_PM_DC, &borrow))
    {
        PmDeviceContext *dc = borrow.resource;
        if (dc->anchor == anchor &&
            lx_handle_close_guarded(handle, LX_HANDLE_PM_DC, 1, unused_dc) == 1)
            result = 0;
    }
    lx_handle_release(&borrow);
    return result;
}
void destroyDeviceContexts(AnchorBlock *anchor)
{
    while (anchor->devices)
        lx_handle_close(anchor->devices->handle, LX_HANDLE_PM_DC);
}
HDC lx_pm_query_dc(HPS handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
    return ps ? ps->dc : (HDC)-1;
}
BOOL lx_pm_associate_dc(HPS handle, HDC dc)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
    if (!ps || (dc && !lx_pm_dc_valid(dc)))
        return FALSE;
    LxHandleBorrow next = {0};
    Window *window = dc ? getWindowFromHWND(anchor, dc) : NULL;
    if (dc && !window)
    {
        if (!lx_handle_borrow(dc, LX_HANDLE_PM_DC, &next))
            return FALSE;
        ((PmDeviceContext *)next.resource)->spaces++;
    }
    if (ps->dc_lease.resource)
        ((PmDeviceContext *)ps->dc_lease.resource)->spaces--;
    lx_handle_release(&ps->dc_lease);
    ps->dc_lease = next;
    ps->dc = dc;
    ps->window = window;
    return TRUE;
}
