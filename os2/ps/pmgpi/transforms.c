/* Public domain. No rights reserved. Wholly AI-authored. */
/* Transforms and viewing: model, default view and viewing matrices kept in the
   Toolkit's mixed FIXED/LONG MATRIXLF form, graphics field and viewing limits. */
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmgpi.h"
#include "emulator/abi.h"
#include <math.h>
#include "os2/ps/pmgpi/internal.h"

static BOOL matrix_set(LONG *out, const LONG *in, LONG options)
{
    if (!in || options < 0 || options > 2 || in[2] || in[5] || in[8] != 1 ||
        (options && (out[2] || out[5] || out[8] != 1)))
        return FALSE;
    LONG result[9];
    if (options == 0)
        memcpy(result, in, sizeof result);
    else
    {
        /* Row vectors apply a first: TRANSFORM_ADD runs the existing matrix first. */
        const LONG *a = options == 1 ? out : in, *b = options == 1 ? in : out;
        double aa = a[0] / 65536.0, ab = a[1] / 65536.0, ac = a[3] / 65536.0, ad = a[4] / 65536.0;
        double ba = b[0] / 65536.0, bb = b[1] / 65536.0, bc = b[3] / 65536.0, bd = b[4] / 65536.0;
        double v[9] = {(aa * ba + ab * bc) * 65536,  (aa * bb + ab * bd) * 65536,  0,
                       (ac * ba + ad * bc) * 65536,  (ac * bb + ad * bd) * 65536,  0,
                       a[6] * ba + a[7] * bc + b[6], a[6] * bb + a[7] * bd + b[7], 1};
        for (unsigned i = 0; i < 9; i++)
        {
            if (!isfinite(v[i]) || v[i] < INT32_MIN || v[i] > INT32_MAX)
                return FALSE;
            result[i] = (LONG)llround(v[i]);
        }
    }
    memcpy(out, result, sizeof result);
    return TRUE;
}

BOOL GpiQueryDefaultViewMatrix(HPS h, LONG count, PVOID out)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !out || count < 0 || count > 9)
        return FALSE;
    if (count)
        memcpy(out, p->gpi.view, count * 4);
    return TRUE;
}

BOOL GpiQueryModelTransformMatrix(HPS h, LONG count, PVOID out)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !out || count < 0 || count > 9)
        return FALSE;
    if (count)
        memcpy(out, p->gpi.model, count * 4);
    return TRUE;
}

BOOL GpiSetModelTransformMatrix(HPS h, LONG count, PVOID in, LONG options)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || count < 0 || count > 9 || (count && !in))
        return FALSE;
    LONG m[9];
    memcpy(m, gpi_identity, sizeof m);
    if (count)
        memcpy(m, in, count * 4);
    return matrix_set(p->gpi.model, m, options);
}

BOOL lx_gpi_default_matrix(HPS h, LONG count, PVOID in, LONG options)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || count < 0 || count > 9 || (count && !in))
        return FALSE;
    LONG m[9];
    memcpy(m, gpi_identity, sizeof m);
    if (count)
        memcpy(m, in, count * 4);
    return matrix_set(p->gpi.view, m, options);
}

BOOL GpiTranslate(HPS h, PVOID matrix, LONG options, PPOINTL offset)
{
    /* The matrix helpers accept only TRANSFORM_REPLACE and TRANSFORM_ADD. */
    if (!gpi_space(h) || !matrix || !offset || options < 0 || options > 1)
        return FALSE;
    LONG m[9];
    memcpy(m, gpi_identity, sizeof m);
    m[6] = offset->x;
    m[7] = offset->y;
    return matrix_set(matrix, m, options);
}

BOOL GpiScale(HPS h, PVOID matrix, LONG options, PLONG factors, PPOINTL center)
{
    if (!gpi_space(h) || !matrix || !factors || !center || options < 0 || options > 1)
        return FALSE;
    LONG m[9];
    memcpy(m, gpi_identity, sizeof m);
    m[0] = factors[0];
    m[4] = factors[1];
    double x = center->x * (1 - factors[0] / 65536.0), y = center->y * (1 - factors[1] / 65536.0);
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return FALSE;
    m[6] = (LONG)llround(x);
    m[7] = (LONG)llround(y);
    return matrix_set(matrix, m, options);
}

BOOL GpiRotate(HPS h, PVOID matrix, LONG options, LONG angle, PPOINTL center)
{
    if (!gpi_space(h) || !matrix || !center || options < 0 || options > 1)
        return FALSE;
    double r = angle / 65536.0 * 3.14159265358979323846 / 180.0, c = cos(r), s = sin(r);
    LONG m[9];
    memcpy(m, gpi_identity, sizeof m);
    m[0] = m[4] = (LONG)llround(c * 65536);
    m[1] = (LONG)llround(s * 65536);
    m[3] = -m[1];
    double x = center->x * (1 - c) + center->y * s, y = center->y * (1 - c) - center->x * s;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return FALSE;
    m[6] = (LONG)llround(x);
    m[7] = (LONG)llround(y);
    return matrix_set(matrix, m, options);
}

static BOOL rect_valid(PRECTL r)
{
    return r && r->xLeft <= r->xRight && r->yBottom <= r->yTop;
}

/* GRE resets viewing limits to this rectangle, which clips nothing. */
static const RECTL no_limits = {(LONG)0xf8000000, (LONG)0xf8000000, 0x7ffffff, 0x7ffffff};

BOOL GpiSetGraphicsField(HPS h, PRECTL r)
{
    PresentationSpace *p = gpi_space(h);
    if (!p)
        return FALSE;
    if (!rect_valid(r))
    {
        WinSetErrorInfo(0x207A, 0); /* PMERR_INV_GRAPHICS_FIELD */
        return FALSE;
    }
    p->gpi.graphics_field = *r;
    p->gpi.graphics_set = TRUE;
    return TRUE;
}

BOOL GpiQueryGraphicsField(HPS h, PRECTL r)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !r)
        return FALSE;
    *r = p->gpi.graphics_set ? p->gpi.graphics_field : (RECTL){0, 0, 0, 0};
    return TRUE;
}

BOOL GpiSetViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !rect_valid(r))
        return FALSE;
    p->gpi.viewing_limits = *r;
    p->gpi.viewing_set = TRUE;
    p->gpi.viewing_custom = TRUE;
    return TRUE;
}

BOOL GpiQueryViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !r)
        return FALSE;
    *r = p->gpi.viewing_set ? p->gpi.viewing_limits : no_limits;
    return TRUE;
}

BOOL GpiSetDefViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !rect_valid(r))
        return FALSE;
    p->gpi.default_limits = *r;
    p->gpi.default_set = TRUE;
    /* A new default applies at once to limits still at their default value. */
    if (!p->gpi.viewing_custom)
    {
        p->gpi.viewing_limits = *r;
        p->gpi.viewing_set = TRUE;
    }
    return TRUE;
}

BOOL GpiQueryDefViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !r)
        return FALSE;
    *r = p->gpi.default_set ? p->gpi.default_limits : no_limits;
    return TRUE;
}

BOOL GpiSetDefaultViewMatrix(HPS ps, LONG count, PVOID matrix, LONG options)
{
    return lx_gpi_default_matrix(ps, count, matrix, options);
}
