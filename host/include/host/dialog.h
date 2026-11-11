/* Public domain. No rights reserved. Wholly AI-authored. */
/* Desktop dialogs presented by a separate native helper process, so the
   runtime links no desktop toolkit. On Linux this is the GNOME manager's
   2ine-dialog; hosts without a helper report LX_HOST_SAVE_ERROR, and callers
   fall back to their own presenter. */
#ifndef TWOINE_HOST_DIALOG_H
#define TWOINE_HOST_DIALOG_H
#include "host/print.h"

/* Runs the helper with a NULL-terminated argument list (without the program).
   Arguments are passed literally, never through a shell. The helper prints the
   chosen value and exits 0, exits 1 when cancelled and 2 on failure. On success
   *output receives the caller-owned, newline-stripped answer; pass NULL when no
   answer is expected. */
LxHostSaveResult lx_host_dialog_run(const char *const arguments[], char **output);
#endif
