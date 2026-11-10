/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared between the PMGPI units. */
#ifndef TWOINE_PMGPI_INTERNAL_H
#define TWOINE_PMGPI_INTERNAL_H
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmwinp.h"

enum
{
    /* MAKEFIXED(1, 0): FIXED values are signed 16.16. */
    GPI_FIXED_ONE = 65536,
    /* The default font is 16 pels high at the 96 dpi reference resolution. */
    GPI_DEFAULT_FONT_PIXELS = 16,
    GPI_REFERENCE_DPI = 96,
    /* Local identifiers for logical fonts and bitmap tags run from 1 to 254. */
    GPI_LCID_MAX = 254,
    /* MATRIXLF has nine elements: fxM11, fxM12, lM13, fxM21, fxM22, lM23, lM31, lM32, lM33. */
    GPI_MATRIX_ELEMENTS = 9,
    /* The bits that PU_ARBITRARY to PU_TWIPS occupy. */
    GPI_PAGE_UNITS = 0x1c,
    /* Guest BITMAPINFOHEADER (12 bytes, USHORT dimensions, RGB entries) and BITMAPINFOHEADER2
       (64 bytes, ULONG dimensions, RGB2 entries) layouts. */
    BMIH_SIZE = 12,
    BMIH_CX_OFFSET = 4,
    BMIH_CY_OFFSET = 6,
    BMIH_CPLANES_OFFSET = 8,
    BMIH_CBITCOUNT_OFFSET = 10,
    RGB_SIZE = 3,
    BMIH2_SIZE = 64,
    BMIH2_CX_OFFSET = 4,
    BMIH2_CY_OFFSET = 8,
    BMIH2_CPLANES_OFFSET = 12,
    BMIH2_CBITCOUNT_OFFSET = 14,
    BMIH2_CBIMAGE_OFFSET = 20,
    BMIH2_CCLRUSED_OFFSET = 32,
    RGB2_SIZE = 4,
    /* An indexed bitmap has at most 8 bits per pel. */
    GPI_MAX_PALETTE_COLORS = 256,
    /* The host canvas is 24-bit RGB unless a display depth is configured. */
    GPI_DEFAULT_DISPLAY_DEPTH = 24,
    GPI_CAPS_COUNT = CAPS_DEVICE_POLYSET_POINTS + 1,
    /* The longest string GpiQueryTextBox measures. */
    GPI_TEXTBOX_MAX_CHARS = 512,
    /* DEVOPENSTRUC: nine fields, all PSZ except pdriv (DRIVDATA). */
    GPI_DEVOPEN_LOGADDRESS = 0,
    GPI_DEVOPEN_DRIVERNAME = 1,
    GPI_DEVOPEN_DRIVDATA = 2,
    GPI_DEVOPEN_FIELDS = 9,
    /* Guest attribute bundle sizes. */
    LINEBUNDLE_SIZE = 28,
    CHARBUNDLE_SIZE = 52,
    MARKERBUNDLE_SIZE = 24,
    AREABUNDLE_SIZE = 24,
    IMAGEBUNDLE_SIZE = 12
};

/* The index of a PRIM_* attribute bundle in PresentationSpace's primitive_* arrays. */
#define GPI_PRIM_INDEX(primitive) ((primitive) - 1)

/* The identity transform in the Toolkit MATRIXLF layout: FIXED 16.16
   coefficients, integral translation and homogeneous term. */
extern LX_PRIVATE const LONG gpi_identity[GPI_MATRIX_ELEMENTS];

/* A PS with its GPI state initialised on first use, or NULL. */
LX_PRIVATE PresentationSpace *gpi_space(HPS h);

/* Sets the pick aperture to its default: a square of the default character cell height. */
LX_PRIVATE void lx_gpi_default_pick(HPS h);

/* GpiSetDefaultViewMatrix: sets the default view matrix from count leading MATRIXLF elements. */
LX_PRIVATE BOOL lx_gpi_default_matrix(HPS h, LONG count, PVOID in, LONG options);

/* Fails an entry that 2ine does not implement with PMERR_FUNCTION_NOT_SUPPORTED. */
static inline BOOL gpi_unsupported(void)
{
    WinSetErrorInfo(PMERR_FUNCTION_NOT_SUPPORTED, 0, NULL);
    return FALSE;
}
#endif
