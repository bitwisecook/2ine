/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private atom ownership, locale comparisons, bitmap scan uploads and scroll damage. */
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmp.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* The 12-byte OS/2 1.x BITMAPINFOHEADER. */
enum
{
    BMIH_SIZE = 12,
    BMIH_CX_OFFSET = 4,
    BMIH_CY_OFFSET = 6,
    BMIH_CPLANES_OFFSET = 8,
    BMIH_CBITCOUNT_OFFSET = 10
};

int main(void)
{
    HAB hab = WinInitialize(0);
    HMQ queue = WinCreateMsgQueue(hab, 0);
    CHECK(hab && queue && WinQueryCpType(queue) == 1 && WinQueryCpType(0) == 1);
    CHECK(WinCompareStrings(hab, 437, 1, "Alpha", "ALPHA", 0) == WCS_EQ);
    CHECK(WinCompareStrings(hab, 437, 1, "Alpha", "Beta", 0) == WCS_LT);
    CHECK(WinCompareStrings(hab, 437, 1, "Beta", "Alpha", 0) == WCS_GT);
    CHECK(WinCompareStrings(hab, 99999, 1, "Alpha", "Beta", 0) == WCS_ERROR);
    ULONG a = WinCreateAtomTable(0, 17), b = WinCreateAtomTable(4096, 31);
    CHECK(a && b && a != b && a != WinQuerySystemAtomTable());
    ULONG first = WinAddAtom(a, "Help table"), second = WinAddAtom(b, "Another table");
    CHECK(first && second && WinFindAtom(a, "HELP TABLE") == first);
    CHECK(WinFindAtom(b, "Help table") == 0 && WinFindAtom(a, "Another table") == 0);
    CHECK(WinAddAtom(a, "Help table") == first && WinDeleteAtom(a, first) == 0);
    CHECK(WinFindAtom(a, "Help table") == first && WinDestroyAtomTable(a) == 0);
    CHECK(WinFindAtom(a, "Help table") == 0 && WinAddAtom(a, (PSZ)(uintptr_t)5) == 0);
    CHECK(WinFindAtom(b, "Another table") == second && WinDestroyAtomTable(b) == 0);
    CHECK(WinDestroyAtomTable(WinQuerySystemAtomTable()) == WinQuerySystemAtomTable());
    CHECK(WinQueryDesktopWindow(hab, 0) == HWND_DESKTOP &&
          WinQueryDesktopWindow(hab, 0xfffffffe) == 0);
    HDC dc = DevOpenDC(hab, OD_MEMORY, "*", 0, NULL, 0);
    SIZEL size = {2, 3};
    HPS ps = GpiCreatePS(hab, dc, &size, PU_PELS | GPIT_MICRO | GPIA_ASSOC);
    uint8_t header[BMIH_SIZE] = {0};
    lx_guest_store_u32(header, BMIH_SIZE);
    lx_guest_store_u16(header + BMIH_CX_OFFSET, 2);
    lx_guest_store_u16(header + BMIH_CY_OFFSET, 3);
    lx_guest_store_u16(header + BMIH_CPLANES_OFFSET, 1);
    lx_guest_store_u16(header + BMIH_CBITCOUNT_OFFSET, 24);
    ULONG bitmap = GpiCreateBitmap(ps, header, 0, NULL, NULL);
    CHECK(dc && ps && bitmap && GpiSetBitmap(ps, bitmap) == 0);
    uint8_t row[8] = {0xff, 0, 0, 0, 0, 0xff, 0, 0};
    CHECK(GpiSetBitmapBits(ps, 1, 1, row, header) == 1);
    uint32_t pixels[6];
    CHECK(lx_pm_raster_read(ps, 0, 0, 2, 3, pixels));
    CHECK(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0xff && pixels[3] == 0xff0000 &&
          pixels[4] == 0 && pixels[5] == 0);
    CHECK(GpiSetBitmapBits(ps, -1, 1, row, header) == GPI_ALTERROR);
    CHECK(GpiSetBitmapBits(ps, 3, 1, row, header) == 0);
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteBitmap(bitmap));
    CHECK(GpiDestroyPS(ps) && DevCloseDC(dc) == 0);
    HWND window = WinCreateWindow(HWND_DESKTOP, (PSZ)(uintptr_t)WC_FRAME, "scroll", WS_VISIBLE, 0,
                                  0, 20, 20, 0, HWND_TOP, 1, NULL, NULL);
    CHECK(window);
    ps = WinGetPS(window);
    RECTL all = {0, 0, 20, 20}, red = {0, 0, 10, 10}, scroll = {0, 0, 20, 10}, update;
    CHECK(ps && WinFillRect(ps, &all, CLR_BLUE) && WinFillRect(ps, &red, CLR_RED));
    ULONG damage = GpiCreateRegion(ps, 0, NULL);
    CHECK(damage && WinScrollWindow(window, 5, 0, &scroll, NULL, damage, &update,
                                    SW_INVALIDATERGN) == RGN_RECT);
    CHECK(update.xLeft == 0 && update.yBottom == 0 && update.xRight == 5 && update.yTop == 10);
    CHECK(lx_pm_raster_read(ps, 5, 0, 1, 1, pixels) && pixels[0] == 0xff0000);
    CHECK(lx_pm_raster_read(ps, 15, 0, 1, 1, pixels) && pixels[0] == 0xff);
    CHECK(GpiQueryRegionBox(ps, damage, &update) == RGN_RECT && update.xRight == 5);
    CHECK(GpiDestroyRegion(ps, damage) && WinReleasePS(ps));
    CHECK(WinDestroyWindow(window) && WinDestroyMsgQueue(queue) && WinTerminate(hab));
    puts(
        "Help prerequisite APIs: private atoms, locale comparison, bitmap scan upload and scroll damage passed");
    return 0;
}
