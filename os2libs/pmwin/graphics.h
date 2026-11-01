/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared presentation-space access for PMWIN and PMGPI. No host renderer types. */
#ifndef TWOINE_PM_GRAPHICS_H
#define TWOINE_PM_GRAPHICS_H
#include "os2libs/pmwin.h"
#include "hal/font.h"
LxHostFont *lx_pm_font(HPS hps);
unsigned lx_pm_font_index(HPS hps);
HPS lx_pm_create_ps(HAB hab, HDC dc);
BOOL lx_pm_erase(HPS hps);
BOOL lx_pm_destroy_ps(HPS hps);
LONG lx_pm_color(HPS hps, LONG color);
BOOL lx_pm_move(HPS hps, const POINTL *point);
LONG lx_pm_line(HPS hps, const POINTL *point, int pel);
LONG lx_pm_rgb(LONG index);
BOOL lx_pm_valid_ps(HPS hps);
#endif
