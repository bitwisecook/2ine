/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_PRINT_JOB_H
#define TWOINE_PM_PRINT_JOB_H
#include <stdint.h>
typedef struct LxPrintJob LxPrintJob;
enum
{
    LX_PRINT_PDF = 1,
    LX_PRINT_PCL = 2
};
LxPrintJob *lx_print_begin(unsigned kind, const char *title, int *cancelled);
int lx_print_page(LxPrintJob *job, const uint32_t *pixels, unsigned width, unsigned height,
                  unsigned dpi);
int lx_print_finish(LxPrintJob *job);
void lx_print_abort(LxPrintJob *job);
#endif
