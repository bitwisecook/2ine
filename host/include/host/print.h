/* Public domain. No rights reserved. Wholly AI-authored. */
/* Printer output paths are host paths selected by the user, never guest paths. */
#ifndef TWOINE_HOST_PRINT_H
#define TWOINE_HOST_PRINT_H
#include <stddef.h>
#include <stdio.h>

typedef enum LxHostSaveResult
{
    LX_HOST_SAVE_ERROR = -1,
    LX_HOST_SAVE_CANCEL = 0,
    LX_HOST_SAVE_OK = 1
} LxHostSaveResult;

/* Returns caller-owned UTF-8 storage on success. The explicit host-only
   TWOINE_PRINT_OUTPUT override provides a destination for unattended jobs. */
LxHostSaveResult lx_host_print_save_path(const char *title, const char *extension, char **path);

typedef struct LxHostPrintDestination
{
    char *path;
    char *printer;
    unsigned copies;
} LxHostPrintDestination;

/* The destination contains exactly one owned path or system-printer name. */
LxHostSaveResult lx_host_print_choose_pcl(const char *title, LxHostPrintDestination *destination);
/* Selects the native PDF (1) or PCL (2) OS/2 device for a common print dialog. */
LxHostSaveResult lx_host_print_choose_device(unsigned *kind);
void lx_host_print_destination_clear(LxHostPrintDestination *destination);
/* Lists owned local printer names; the caller frees each name and the array. */
char **lx_host_print_printers(size_t *count);
/* Submits actual raw PCL bytes to the named system spooler and reports its
   result. The stream is positioned at the beginning before submission. */
int lx_host_print_submit_pcl(const char *printer, const char *title, unsigned copies, FILE *source);
/* Backend test primitive: the caller supplies a trusted helper executable.
   Guest settings never select that executable; ordinary jobs use the OS spooler. */
int lx_host_print_submit_helper(const char *program, const char *printer, const char *title,
                                unsigned copies, FILE *source);
#endif
