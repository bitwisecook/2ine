/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_PRINT_JOB_H
#define TWOINE_PM_PRINT_JOB_H
#include <stddef.h>
#include <stdint.h>
/* Host print jobs that render PM pages to a PDF file or a PCL 5 printer stream. */
typedef struct LxPrintJob LxPrintJob;

enum
{
    LX_PRINT_PDF = 1,
    LX_PRINT_PCL = 2
};

enum
{
    LX_PRINT_SIMPLEX = 1,
    LX_PRINT_DUPLEX_LONG_EDGE = 2,
    LX_PRINT_DUPLEX_SHORT_EDGE = 3
};

/* Asks the host for a destination; NULL on failure, with *cancelled set if the user declined. */
LxPrintJob *lx_print_begin(unsigned kind, const char *title, int *cancelled);
/* Appends a bottom-up 0xRRGGBB page; returns 0 on failure, after which the job cannot finish. */
int lx_print_page(LxPrintJob *job, const uint32_t *pixels, unsigned width, unsigned height,
                  unsigned dpi);
/* Writes or submits the output and frees the job; returns 0 if nothing was delivered. */
int lx_print_finish(LxPrintJob *job);
/* Copies repeat the job; duplex is an LX_PRINT_ sidedness; draft lowers quality. */
void lx_print_options(LxPrintJob *job, unsigned copies, unsigned duplex, int draft);
/* Device data for the current page. Only the PCL stream can carry it. */
int lx_print_raw(LxPrintJob *job, const void *data, size_t size);
/* Discards the job and frees it. */
void lx_print_abort(LxPrintJob *job);
#endif
