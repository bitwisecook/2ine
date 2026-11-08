/* Public domain. No rights reserved. Wholly AI-authored. */
/* Virtual output queues use the native 2ine raster printer devices. */
#ifndef TWOINE_PRINTER_CATALOGUE_H
#define TWOINE_PRINTER_CATALOGUE_H
#include <string.h>
#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
enum
{
    LX_PRINTER_DRIVER_DATA_SIZE = 40
};

static const char *const lx_printer_queues[2] = {"2INEPDF", "2INEPCL"};
static const char *const lx_printer_drivers[2] = {"2INEPDF.PDF", "2INEPCL.PCL"};
static const char *const lx_printer_descriptions[2] = {"Save as PDF", "PCL 5 Printer"};

/* DRIVDATA public prefix. The virtual devices have no private job properties. */
static inline void lx_printer_driver_data(void *output, unsigned printer)
{
    unsigned char *data = output;
    memset(data, 0, LX_PRINTER_DRIVER_DATA_SIZE);
    data[0] = LX_PRINTER_DRIVER_DATA_SIZE;
    data[4] = 1;
    memcpy(data + 8, lx_printer_queues[printer], strlen(lx_printer_queues[printer]) + 1);
}

static int lx_printer_index(const char *name)
{
    if (!name)
        return -1;
    for (unsigned i = 0; i < 2; i++)
    {
        size_t n = strlen(lx_printer_queues[i]);
        if (!strncasecmp(name, lx_printer_queues[i], n) &&
            (!name[n] || name[n] == '.' || name[n] == ';'))
            return (int)i;
    }
    return -1;
}
#endif
