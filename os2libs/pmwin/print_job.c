/* Public domain. No rights reserved. Wholly AI-authored. */
/* Print pages retain actual GPI pixels. PDF stores color image XObjects;
   PCL 5 uses monochrome ordered-dither raster rows at the advertised dpi. */
#include "os2libs/pmwin/print_job.h"
#include "hal/print.h"
#include "hal/host.h"
#include "hal/host_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#define PRINT_PAGE_LIMIT 1000u
#define PRINT_BYTE_LIMIT (UINT64_C(1024) * 1024 * 1024)
struct LxPrintJob
{
    FILE *spool;
    LxHostPrintDestination destination;
    char *title;
    unsigned kind, pages;
    long offsets[3 * PRINT_PAGE_LIMIT + 3];
    int failed;
};
void lx_print_abort(LxPrintJob *job)
{
    if (!job)
        return;
    if (job->spool)
        fclose(job->spool);
    lx_host_print_destination_clear(&job->destination);
    free(job->title);
    free(job);
}
LxPrintJob *lx_print_begin(unsigned kind, const char *title, int *cancelled)
{
    if (cancelled)
        *cancelled = 0;
    if (kind != LX_PRINT_PDF && kind != LX_PRINT_PCL)
        return NULL;
    LxHostPrintDestination destination = {0};
    destination.copies = 1;
    LxHostSaveResult choice = kind == LX_PRINT_PDF
                                  ? lx_host_print_save_path(title, "pdf", &destination.path)
                                  : lx_host_print_choose_pcl(title, &destination);
    if (choice != LX_HOST_SAVE_OK)
    {
        if (choice == LX_HOST_SAVE_ERROR)
            fprintf(stderr, "2ine: Cannot choose a print output destination.\n");
        if (cancelled)
            *cancelled = choice == LX_HOST_SAVE_CANCEL;
        return NULL;
    }
    LxPrintJob *job = calloc(1, sizeof *job);
    if (!job)
    {
        lx_host_print_destination_clear(&destination);
        return NULL;
    }
    job->destination = destination;
    job->title = strdup(title && *title ? title : "2ine Print Job");
    job->kind = kind;
    job->spool = tmpfile();
    if (!job->spool || !job->title)
    {
        lx_print_abort(job);
        return NULL;
    }
    if (kind == LX_PRINT_PDF)
        fputs("%PDF-1.4\n%\342\343\317\323\n", job->spool);
    else
        fputs("\033%-12345X@PJL ENTER LANGUAGE=PCL\r\n\033E", job->spool);
    return job;
}
static void pdf_object(LxPrintJob *job, unsigned number)
{
    job->offsets[number] = ftell(job->spool);
    fprintf(job->spool, "%u 0 obj\n", number);
}
static int pdf_page(LxPrintJob *job, const uint32_t *pixels, unsigned width, unsigned height,
                    unsigned dpi)
{
    size_t length = (size_t)width * height * 3;
    unsigned char *rgb = malloc(length);
    if (!rgb)
        return 0;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
        {
            uint32_t color = pixels[(size_t)(height - y - 1) * width + x];
            unsigned char *p = rgb + ((size_t)y * width + x) * 3;
            p[0] = (unsigned char)(color >> 16);
            p[1] = (unsigned char)(color >> 8);
            p[2] = (unsigned char)color;
        }
    uLongf packed_size = compressBound((uLong)length);
    unsigned char *packed = malloc(packed_size);
    int ok = packed && compress2(packed, &packed_size, rgb, (uLong)length, Z_BEST_SPEED) == Z_OK;
    free(rgb);
    if (!ok)
    {
        free(packed);
        return 0;
    }
    unsigned image = 3 + job->pages * 3, content = image + 1, page = image + 2;
    pdf_object(job, image);
    fprintf(job->spool,
            "<< /Type /XObject /Subtype /Image /Width %u /Height %u /ColorSpace /DeviceRGB "
            "/BitsPerComponent 8 /Filter /FlateDecode /Length %lu >>\nstream\n",
            width, height, (unsigned long)packed_size);
    ok = fwrite(packed, 1, packed_size, job->spool) == packed_size;
    free(packed);
    fputs("\nendstream\nendobj\n", job->spool);
    char commands[160];
    /* Integer thousandths avoid locale-dependent decimal formatting. */
    unsigned page_width = (unsigned)(((uint64_t)width * 72000 + dpi / 2) / dpi),
             page_height = (unsigned)(((uint64_t)height * 72000 + dpi / 2) / dpi);
    int bytes =
        snprintf(commands, sizeof commands, "q\n%u.%03u 0 0 %u.%03u 0 0 cm\n/Im0 Do\nQ\n",
                 page_width / 1000, page_width % 1000, page_height / 1000, page_height % 1000);
    pdf_object(job, content);
    fprintf(job->spool, "<< /Length %d >>\nstream\n%sendstream\nendobj\n", bytes, commands);
    pdf_object(job, page);
    fprintf(job->spool,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %u.%03u %u.%03u] "
            "/Resources << /XObject << /Im0 %u 0 R >> >> /Contents %u 0 R >>\nendobj\n",
            page_width / 1000, page_width % 1000, page_height / 1000, page_height % 1000, image,
            content);
    return ok && !ferror(job->spool);
}
static int pcl_page(LxPrintJob *job, const uint32_t *pixels, unsigned width, unsigned height,
                    unsigned dpi)
{
    static const unsigned threshold[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    size_t bytes = (width + 7u) / 8u;
    unsigned char *row = malloc(bytes);
    if (!row)
        return 0;
    fprintf(job->spool,
            "\033&l26A\033&l0O\033&l0E\033*p0x0Y\033*t%uR\033*r%uS\033*r%uT\033*b0M\033*r1A", dpi,
            width, height);
    for (unsigned y = 0; y < height; y++)
    {
        memset(row, 0, bytes);
        for (unsigned x = 0; x < width; x++)
        {
            uint32_t pixel = pixels[(size_t)(height - y - 1) * width + x];
            unsigned luminance =
                (77 * ((pixel >> 16) & 255) + 150 * ((pixel >> 8) & 255) + 29 * (pixel & 255)) >> 8;
            if (luminance < threshold[(y & 3) * 4 + (x & 3)] * 16 + 8)
                row[x / 8] |= (unsigned char)(0x80u >> (x & 7));
        }
        fprintf(job->spool, "\033*b%zuW", bytes);
        if (fwrite(row, 1, bytes, job->spool) != bytes)
        {
            free(row);
            return 0;
        }
    }
    free(row);
    fputs("\033*rB\f", job->spool);
    return !ferror(job->spool);
}
int lx_print_page(LxPrintJob *job, const uint32_t *pixels, unsigned width, unsigned height,
                  unsigned dpi)
{
    if (!job || job->failed || !pixels || !width || !height || width > 10000 || height > 10000 ||
        (uint64_t)width * height > 16000000 || !dpi || dpi > 2400 || job->pages >= PRINT_PAGE_LIMIT)
        return 0;
    int ok = job->kind == LX_PRINT_PDF ? pdf_page(job, pixels, width, height, dpi)
                                       : pcl_page(job, pixels, width, height, dpi);
    long size = ftell(job->spool);
    if (!ok || size < 0 || (uint64_t)size > PRINT_BYTE_LIMIT)
    {
        job->failed = 1;
        return 0;
    }
    job->pages++;
    return 1;
}
static int commit_output(LxPrintJob *job)
{
    uint32_t pid, parent;
    lx_host_process_ids(&pid, &parent);
    size_t length = strlen(job->destination.path) + 80;
    char *temporary = malloc(length);
    if (!temporary)
        return 0;
    LxHostFile *file = NULL;
    LxHostFsError error = LX_HOST_FS_IO;
    for (unsigned attempt = 0; attempt < 16 && !file; attempt++)
    {
        snprintf(temporary, length, "%s.2ine-%u-%llu-%u.tmp", job->destination.path, pid,
                 (unsigned long long)lx_host_monotonic_us(), attempt);
        file = lx_host_file_open(temporary,
                                 LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE |
                                     LX_HOST_FILE_NOINHERIT,
                                 0, &error);
        if (!file && error != LX_HOST_FS_EXISTS)
            break;
    }
    int ok = file && !fflush(job->spool) && !fseek(job->spool, 0, SEEK_SET);
    const size_t capacity = 65536;
    unsigned char *buffer = malloc(capacity);
    ok = ok && buffer;
    size_t count;
    while (ok && (count = fread(buffer, 1, capacity, job->spool)))
    {
        size_t written = 0;
        ok = lx_host_file_write(file, buffer, count, &written) == LX_HOST_FS_OK && written == count;
    }
    free(buffer);
    ok = ok && !ferror(job->spool) && lx_host_file_flush(file) == LX_HOST_FS_OK;
    lx_host_file_close(file);
    if (ok)
        ok = lx_host_path_move(temporary, job->destination.path) == LX_HOST_FS_OK;
    if (!ok && file)
        lx_host_path_remove(temporary, 0);
    free(temporary);
    return ok;
}
int lx_print_finish(LxPrintJob *job)
{
    if (!job)
        return 0;
    int ok = !job->failed && job->pages;
    if (ok && job->kind == LX_PRINT_PDF)
    {
        pdf_object(job, 1);
        fputs("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", job->spool);
        pdf_object(job, 2);
        fprintf(job->spool, "<< /Type /Pages /Count %u /Kids [", job->pages);
        for (unsigned page = 0; page < job->pages; page++)
            fprintf(job->spool, "%u 0 R ", 5 + page * 3);
        fputs("] >>\nendobj\n", job->spool);
        long start = ftell(job->spool);
        unsigned objects = 3 + job->pages * 3;
        fprintf(job->spool, "xref\n0 %u\n0000000000 65535 f \n", objects);
        for (unsigned i = 1; i < objects; i++)
            fprintf(job->spool, "%010ld 00000 n \n", job->offsets[i]);
        fprintf(job->spool, "trailer\n<< /Size %u /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",
                objects, start);
    }
    else if (ok)
        fputs("\033E\033%-12345X", job->spool);
    ok = ok && !ferror(job->spool) &&
         (job->destination.printer ? lx_host_print_submit_pcl(job->destination.printer, job->title,
                                                              job->destination.copies, job->spool)
                                   : commit_output(job));
    lx_print_abort(job);
    return ok;
}
