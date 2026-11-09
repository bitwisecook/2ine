/* Public domain. No rights reserved. Wholly AI-authored. */
/* Correlation and drawing control: the pick aperture, boundary data and the
   stop-draw and draw-control states. */
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"
#include <math.h>
#include "os2/ps/pmgpi/internal.h"

LONG GpiQueryStopDraw(HPS h)
{
    PresentationSpace *p = gpi_space(h);
    return p ? p->gpi.stop_draw : -1;
}

BOOL GpiSetStopDraw(HPS h, LONG v)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || v < 0 || v > 1)
        return FALSE;
    p->gpi.stop_draw = v;
    return TRUE;
}

BOOL GpiSetPickAperturePosition(HPS h, PPOINTL v)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !v)
        return FALSE;
    p->gpi.pick_position = *v;
    return TRUE;
}

BOOL GpiQueryPickAperturePosition(HPS h, PPOINTL v)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !v)
        return FALSE;
    *v = p->gpi.pick_position;
    return TRUE;
}

BOOL GpiSetPickApertureSize(HPS h, LONG options, PSIZEL v)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || (options != 0 && options != 2))
        return FALSE;
    if (!options)
    {
        lx_gpi_default_pick(h);
        return TRUE;
    }
    if (!v || v->cx < 0 || v->cy < 0)
        return FALSE;
    p->gpi.pick_size = *v;
    return TRUE;
}

BOOL GpiQueryPickApertureSize(HPS h, PSIZEL v)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !v)
        return FALSE;
    *v = p->gpi.pick_size;
    return TRUE;
}

BOOL GpiResetBoundaryData(HPS h)
{
    PresentationSpace *p = gpi_space(h);
    if (!p)
        return FALSE;
    p->gpi.boundary_set = FALSE;
    return TRUE;
}

BOOL GpiQueryBoundaryData(HPS handle, PRECTL bounds)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !bounds)
        return FALSE;
    *bounds = ps->gpi.boundary_set ? ps->gpi.boundary : (RECTL){0, 0, 0, 0};
    return TRUE;
}

BOOL GpiSetDrawControl(HPS handle, LONG control, LONG value)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || control < 1 || control > 5 || (value != 0 && value != 1))
        return FALSE;
    LONG matrix[9];
    if (!GpiQueryDefaultViewMatrix(handle, 9, matrix))
        return FALSE;
    /* DCTL_ERASE and DCTL_DYNAMIC concern retained drawing, which does not exist here. */
    if (control == 2)
        ps->gpi.display_enabled = value;
    else if (control == 3)
        ps->gpi.boundary_enabled = value;
    else if (control == 5)
    {
        ps->gpi.correlate = value;
        ps->gpi.pick_hit = FALSE;
    }
    else if (value && control == 4)
        return gpi_unsupported();
    return TRUE;
}
