/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_WIN_PRINT_INTERNAL_H
#define TWOINE_HOST_WIN_PRINT_INTERNAL_H
#include <windows.h>
/* The most copies of one job the dialogs offer and the spooler submits. */
#define LX_WIN_PRINT_MAX_COPIES 999
/* A caller-owned UTF-8 copy of a printer name, or NULL. */
char *lx_win_print_utf8(const WCHAR *name);
#endif
