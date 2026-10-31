/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared presentation-space access for PMWIN and PMGPI. No host renderer types. */
#ifndef TWOINE_PM_GRAPHICS_H
#define TWOINE_PM_GRAPHICS_H
#include "os2libs/pmwin.h"
HPS lx_pm_create_ps(HAB hab, HDC dc);
BOOL lx_pm_erase(HPS hps);
BOOL lx_pm_destroy_ps(HPS hps);
LONG lx_pm_color(HPS hps, LONG color);
BOOL lx_pm_move(HPS hps, const POINTL *point);
LONG lx_pm_line(HPS hps, const POINTL *point, int pel);
BOOL lx_pm_valid_ps(HPS hps);
#endif
