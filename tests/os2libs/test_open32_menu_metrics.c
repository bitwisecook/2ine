/* Public domain. No rights reserved. Wholly AI-authored. */
/* Menu metric queries use live bitmap dimensions and release their transient handles. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
#include "guest/abi.h"
#include <stdio.h>
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
extern LONG OS2API GetMenuCheckMarkDimensions(void);
LxProcessState GProcessState;
static ULONG presentation = 1, current;
static unsigned made, deleted;
static BOOL load_result = TRUE, query_result = TRUE;
static USHORT width, height;

uint64_t lx_host_thread_id(void)
{
    return 0;
}

ULONG open32Presentation(void)
{
    return presentation;
}

ULONG WinGetSysBitmap(HWND desktop, ULONG id)
{
    CHECK(desktop == HWND_DESKTOP && id == 6 && current == 0);
    if (!load_result)
        return 0;
    current = ++made;
    return current;
}

BOOL GpiQueryBitmapParameters(ULONG bitmap, PVOID output)
{
    CHECK(bitmap && bitmap == current && output);
    if (!query_result)
        return FALSE;
    lx_guest_store_u32(output, 12);
    lx_guest_store_u16((UCHAR *)output + 4, width);
    lx_guest_store_u16((UCHAR *)output + 6, height);
    lx_guest_store_u16((UCHAR *)output + 8, 1);
    lx_guest_store_u16((UCHAR *)output + 10, 1);
    return TRUE;
}

BOOL GpiDeleteBitmap(ULONG bitmap)
{
    CHECK(bitmap && bitmap == current);
    current = 0;
    deleted++;
    return TRUE;
}

/* Unrelated metrics must not replace the queried system bitmap's dimensions. */
LONG WinQuerySysValue(HWND desktop, LONG index)
{
    (void)desktop;
    (void)index;
    CHECK(0);
    return 0;
}

LONG WinQuerySysColor(HWND desktop, LONG index, LONG reserved)
{
    (void)desktop;
    (void)index;
    (void)reserved;
    CHECK(0);
    return 0;
}

void open32SetError(ULONG error)
{
    (void)error;
    CHECK(0);
}

Open32DC *open32FindDC(ULONG handle)
{
    (void)handle;
    CHECK(0);
    return NULL;
}

HDC GpiQueryDevice(HPS ps)
{
    (void)ps;
    CHECK(0);
    return 0;
}

unsigned lx_pm_printer_kind(HDC dc)
{
    (void)dc;
    CHECK(0);
    return 0;
}

BOOL lx_pm_printer_size(HDC dc, unsigned *width, unsigned *height);

BOOL lx_pm_printer_size(HDC dc, unsigned *width, unsigned *height)
{
    (void)dc;
    (void)width;
    (void)height;
    CHECK(0);
    return FALSE;
}

unsigned lx_pm_dc_dpi(HDC dc)
{
    (void)dc;
    CHECK(0);
    return 0;
}

int main(void)
{
    presentation = 0;
    CHECK(GetMenuCheckMarkDimensions() == 0 && made == 0);
    presentation = 1;
    static const USHORT sizes[][2] = {{13, 7}, {9, 9}, {65535, 2}, {2, 65535}, {0, 0}};
    for (unsigned repeat = 0; repeat < 20; repeat++)
        for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++)
        {
            width = sizes[i][0];
            height = sizes[i][1];
            CHECK((ULONG)GetMenuCheckMarkDimensions() == ((ULONG)width | (ULONG)height << 16));
            CHECK(current == 0 && made == deleted);
        }
    load_result = FALSE;
    CHECK(GetMenuCheckMarkDimensions() == 0 && current == 0 && made == deleted);
    load_result = TRUE;
    query_result = FALSE;
    CHECK(GetMenuCheckMarkDimensions() == 0 && current == 0 && made == deleted);
    puts(
        "Menu check dimensions follow queried system bitmaps and release successful/failed queries");
    return 0;
}
