/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_CORE_FATAL_DIALOG_HOOKS_H
#define TWOINE_HOST_CORE_FATAL_DIALOG_HOOKS_H
/* Shows the failure on the host desktop. An empty report path offers no report. */
int lx_host_fatal_present(const char *title, const char *message, const char *report);
#endif
