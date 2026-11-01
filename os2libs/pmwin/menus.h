/* Public domain. No rights reserved. Wholly AI-authored. */
/* Menu entries retain command IDs, attributes and owned submenu windows. */
#ifndef TWOINE_PM_MENUS_H
#define TWOINE_PM_MENUS_H
#include "os2libs/pmwin/internal.h"
void dismissMenusForClick(AnchorBlock *anchor, Window *target);
typedef struct PmMenuItem
{
    MENUITEM item;
    char *text;
    RECTL rect;
} PmMenuItem;
typedef struct PmMenu
{
    PmMenuItem *items;
    size_t count;
    int selected;
    HWND focus;
} PmMenu;
Window *lx_pm_menu_window(HWND handle);
BOOL lx_pm_menu_attach(HWND menu, HWND parent);
BOOL lx_pm_menu_owner(HWND menu, HWND owner);
BOOL lx_pm_menu_remove(HWND handle, size_t index, BOOL destroy_submenu);
#endif
