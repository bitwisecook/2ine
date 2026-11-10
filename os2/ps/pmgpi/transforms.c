/* Public domain. No rights reserved. Wholly AI-authored. */
/* Transforms and viewing: model, default view and viewing matrices kept in the
   Toolkit's mixed FIXED/LONG MATRIXLF form, graphics field and viewing limits. */
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmgpi.h"
#include "emulator/abi.h"
#include <math.h>
#include "os2/ps/pmgpi/internal.h"

/* MATRIXLF element indexes. */
enum
{
    M11,
    M12,
    M13,
    M21,
    M22,
    M23,
    M31,
    M32,
    M33
};

static BOOL matrix_set(LONG *out, const LONG *in, LONG options)
{
    /* Only affine matrices: lM13 and lM23 are 0 and lM33 is 1. */
    if (!in || options < TRANSFORM_REPLACE || options > TRANSFORM_PREEMPT || in[M13] || in[M23] ||
        in[M33] != 1 || (options && (out[M13] || out[M23] || out[M33] != 1)))
        return FALSE;
    LONG result[GPI_MATRIX_ELEMENTS];
    if (options == TRANSFORM_REPLACE)
        memcpy(result, in, sizeof result);
    else
    {
        /* Row vectors apply a first: TRANSFORM_ADD runs the existing matrix first. */
        const LONG *a = options == TRANSFORM_ADD ? out : in,
                   *b = options == TRANSFORM_ADD ? in : out;
        const double one = GPI_FIXED_ONE;
        double aa = a[M11] / one, ab = a[M12] / one, ac = a[M21] / one, ad = a[M22] / one;
        double ba = b[M11] / one, bb = b[M12] / one, bc = b[M21] / one, bd = b[M22] / one;
        double v[GPI_MATRIX_ELEMENTS] = {
            (aa * ba + ab * bc) * GPI_FIXED_ONE, (aa * bb + ab * bd) * GPI_FIXED_ONE, 0,
            (ac * ba + ad * bc) * GPI_FIXED_ONE, (ac * bb + ad * bd) * GPI_FIXED_ONE, 0,
            a[M31] * ba + a[M32] * bc + b[M31],  a[M31] * bb + a[M32] * bd + b[M32],  1};
        for (unsigned i = 0; i < GPI_MATRIX_ELEMENTS; i++)
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
    if (!p || !out || count < 0 || count > GPI_MATRIX_ELEMENTS)
        return FALSE;
    if (count)
        memcpy(out, p->gpi.view, count * (LONG)sizeof *p->gpi.view);
    return TRUE;
}

BOOL GpiQueryModelTransformMatrix(HPS h, LONG count, PVOID out)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !out || count < 0 || count > GPI_MATRIX_ELEMENTS)
        return FALSE;
    if (count)
        memcpy(out, p->gpi.model, count * (LONG)sizeof *p->gpi.model);
    return TRUE;
}

BOOL GpiSetModelTransformMatrix(HPS h, LONG count, PVOID in, LONG options)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || count < 0 || count > GPI_MATRIX_ELEMENTS || (count && !in))
        return FALSE;
    LONG m[GPI_MATRIX_ELEMENTS];
    memcpy(m, gpi_identity, sizeof m);
    if (count)
        memcpy(m, in, count * (LONG)sizeof *m);
    return matrix_set(p->gpi.model, m, options);
}

BOOL lx_gpi_default_matrix(HPS h, LONG count, PVOID in, LONG options)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || count < 0 || count > GPI_MATRIX_ELEMENTS || (count && !in))
        return FALSE;
    LONG m[GPI_MATRIX_ELEMENTS];
    memcpy(m, gpi_identity, sizeof m);
    if (count)
        memcpy(m, in, count * (LONG)sizeof *m);
    return matrix_set(p->gpi.view, m, options);
}

BOOL GpiTranslate(HPS h, PVOID matrix, LONG options, PPOINTL offset)
{
    /* The matrix helpers accept only TRANSFORM_REPLACE and TRANSFORM_ADD. */
    if (!gpi_space(h) || !matrix || !offset || options < TRANSFORM_REPLACE ||
        options > TRANSFORM_ADD)
        return FALSE;
    LONG m[GPI_MATRIX_ELEMENTS];
    memcpy(m, gpi_identity, sizeof m);
    m[M31] = offset->x;
    m[M32] = offset->y;
    return matrix_set(matrix, m, options);
}

BOOL GpiScale(HPS h, PVOID matrix, LONG options, PLONG factors, PPOINTL center)
{
    if (!gpi_space(h) || !matrix || !factors || !center || options < TRANSFORM_REPLACE ||
        options > TRANSFORM_ADD)
        return FALSE;
    LONG m[GPI_MATRIX_ELEMENTS];
    memcpy(m, gpi_identity, sizeof m);
    m[M11] = factors[0];
    m[M22] = factors[1];
    double x = center->x * (1 - factors[0] / (double)GPI_FIXED_ONE),
           y = center->y * (1 - factors[1] / (double)GPI_FIXED_ONE);
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return FALSE;
    m[M31] = (LONG)llround(x);
    m[M32] = (LONG)llround(y);
    return matrix_set(matrix, m, options);
}

BOOL GpiRotate(HPS h, PVOID matrix, LONG options, LONG angle, PPOINTL center)
{
    if (!gpi_space(h) || !matrix || !center || options < TRANSFORM_REPLACE ||
        options > TRANSFORM_ADD)
        return FALSE;
    /* The angle is FIXED degrees. */
    double r = angle / (double)GPI_FIXED_ONE * 3.14159265358979323846 / 180.0, c = cos(r),
           s = sin(r);
    LONG m[GPI_MATRIX_ELEMENTS];
    memcpy(m, gpi_identity, sizeof m);
    m[M11] = m[M22] = (LONG)llround(c * GPI_FIXED_ONE);
    m[M12] = (LONG)llround(s * GPI_FIXED_ONE);
    m[M21] = -m[M12];
    double x = center->x * (1 - c) + center->y * s, y = center->y * (1 - c) - center->x * s;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return FALSE;
    m[M31] = (LONG)llround(x);
    m[M32] = (LONG)llround(y);
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
        WinSetErrorInfo(PMERR_INV_GRAPHICS_FIELD, 0);
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
