/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared between the PMGPI units. */
#ifndef TWOINE_PMGPI_INTERNAL_H
#define TWOINE_PMGPI_INTERNAL_H
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/private/pmwinp.h"

/* The identity transform in the Toolkit MATRIXLF layout: FIXED 16.16
   coefficients, integral translation and homogeneous term. */
extern LX_PRIVATE const LONG gpi_identity[9];

/* A PS with its GPI state initialised on first use, or NULL. */
LX_PRIVATE PresentationSpace *gpi_space(HPS h);

/* Sets the pick aperture to its default: a square of the default character cell height. */
LX_PRIVATE void lx_gpi_default_pick(HPS h);

LX_PRIVATE BOOL lx_gpi_default_matrix(HPS h, LONG count, PVOID in, LONG options);

/* Fails an entry that 2ine does not implement with PMERR_FUNCTION_NOT_SUPPORTED. */
static inline BOOL gpi_unsupported(void)
{
    WinSetErrorInfo(PMERR_FUNCTION_NOT_SUPPORTED, 0, NULL);
    return FALSE;
}
#endif
