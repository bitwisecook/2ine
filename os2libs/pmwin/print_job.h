/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_PRINT_JOB_H
#define TWOINE_PM_PRINT_JOB_H
#include <stddef.h>
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
/* Copies repeat the job; duplex is 1 simplex, 2 long edge, 3 short edge; draft lowers quality. */
void lx_print_options(LxPrintJob *job, unsigned copies, unsigned duplex, int draft);
/* Device data for the current page. Only the PCL stream can carry it. */
int lx_print_raw(LxPrintJob *job, const void *data, size_t size);
void lx_print_abort(LxPrintJob *job);
#endif
