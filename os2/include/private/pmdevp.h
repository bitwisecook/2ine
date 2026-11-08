/* Public domain. No rights reserved. Wholly AI-authored. */
/* Virtual output queues use the native 2ine raster printer devices. */
#ifndef _INCL_PMDEVP_H_
#define _INCL_PMDEVP_H_
#include <stddef.h>
#include <string.h>

enum
{
    LX_PRINTER_COUNT = 2,
    /* DRIVDATA: LONG cb, LONG lVersion, CHAR szDeviceName[32]. */
    LX_PRINTER_DRIVER_DATA_SIZE = 40,
    LX_DRIVDATA_VERSION_OFFSET = 4,
    LX_DRIVDATA_DEVICE_NAME_OFFSET = 8
};

static const char *const lx_printer_queues[LX_PRINTER_COUNT] = {"2INEPDF", "2INEPCL"};
static const char *const lx_printer_drivers[LX_PRINTER_COUNT] = {"2INEPDF.PDF", "2INEPCL.PCL"};
static const char *const lx_printer_descriptions[LX_PRINTER_COUNT] = {"Save as PDF",
                                                                      "PCL 5 Printer"};

/* DRIVDATA public prefix. The virtual devices have no private job properties. */
static inline void lx_printer_driver_data(void *output, unsigned printer)
{
    unsigned char *data = output;
    memset(data, 0, LX_PRINTER_DRIVER_DATA_SIZE);
    data[0] = LX_PRINTER_DRIVER_DATA_SIZE;
    data[LX_DRIVDATA_VERSION_OFFSET] = 1;
    memcpy(data + LX_DRIVDATA_DEVICE_NAME_OFFSET, lx_printer_queues[printer],
           strlen(lx_printer_queues[printer]) + 1);
}

/* Queue names are ASCII. Compares the first length bytes of name and queue
   without regard to case, stopping after a shared terminator. */
static inline int lx_printer_name_prefix(const char *name, const char *queue, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        unsigned char a = (unsigned char)name[i], b = (unsigned char)queue[i];
        if (a >= 'a' && a <= 'z')
            a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z')
            b -= 'a' - 'A';
        if (a != b)
            return 0;
        if (!a)
            return 1;
    }
    return 1;
}

/* Whether name is the queue of the printer, ignoring case. */
static inline int lx_printer_queue_is(const char *name, unsigned printer)
{
    return lx_printer_name_prefix(name, lx_printer_queues[printer],
                                  strlen(lx_printer_queues[printer]) + 1);
}

static int lx_printer_index(const char *name)
{
    if (!name)
        return -1;
    for (unsigned i = 0; i < LX_PRINTER_COUNT; i++)
    {
        size_t n = strlen(lx_printer_queues[i]);
        if (lx_printer_name_prefix(name, lx_printer_queues[i], n) &&
            (!name[n] || name[n] == '.' || name[n] == ';'))
            return (int)i;
    }
    return -1;
}
#endif
