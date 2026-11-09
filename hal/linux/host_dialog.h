/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native GNOME dialogs are provided by the 2ine-dialog helper. Arguments are
   passed literally; the helper prints a chosen value and exits 0, exits 1
   when cancelled and 2 on failure. */
#ifndef TWOINE_LINUX_HOST_DIALOG_H
#define TWOINE_LINUX_HOST_DIALOG_H
#include "hal/print.h"
/* Runs the helper with a NULL-terminated argument list (without the program).
   On success *output receives the owned, newline-stripped answer; pass NULL
   when no answer is expected. */
LxHostSaveResult lx_host_dialog_run(const char *const arguments[], char **output);
#endif
