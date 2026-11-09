/* Public domain. No rights reserved. Wholly AI-authored. */
/* Virtual printer queue names match without regard to case. */
#include "os2/include/private/pmdevp.h"
#include <stdio.h>

static int failures;

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    CHECK(lx_printer_index("2INEPDF") == 0);
    CHECK(lx_printer_index("2inepcl") == 1);
    CHECK(lx_printer_index("2InePdf.PDF") == 0);
    CHECK(lx_printer_index("2inepcl;LPT1") == 1);
    CHECK(lx_printer_index("2INEPDFX") == -1);
    CHECK(lx_printer_index("2INEPD") == -1);
    CHECK(lx_printer_index("") == -1);
    CHECK(lx_printer_index(NULL) == -1);
    CHECK(lx_printer_queue_is("2inePdf", 0));
    CHECK(!lx_printer_queue_is("2INEPDF.PDF", 0));
    CHECK(!lx_printer_queue_is("2INEPD", 0));
    CHECK(!lx_printer_queue_is("2INEPDF", 1));
    unsigned char data[LX_PRINTER_DRIVER_DATA_SIZE];
    lx_printer_driver_data(data, 1);
    CHECK(data[0] == LX_PRINTER_DRIVER_DATA_SIZE && data[LX_DRIVDATA_VERSION_OFFSET] == 1);
    CHECK(!strcmp((const char *)data + LX_DRIVDATA_DEVICE_NAME_OFFSET, "2INEPCL"));
    if (!failures)
        puts("Printer queue names and DRIVDATA passed");
    return failures != 0;
}
