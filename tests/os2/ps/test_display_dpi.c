/* Public domain. No rights reserved. Wholly AI-authored. */
/* Display DPI affects physical units and layout while printer DPI remains fixed. */
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

enum
{
    CAPS_COUNT = CAPS_DEVICE_POLYSET_POINTS + 1,
    REFERENCE_DPI = 96,
    PRINTER_DPI = 300,
    /* 96-DPI title bar height and vertical scroll bar width */
    TITLE_BAR_HEIGHT = 20,
    SCROLL_BAR_WIDTH = 16,
    /* One character cell in dialog units */
    DIALOG_CELL_X = 4,
    DIALOG_CELL_Y = 8,
    /* Tenths of a millimetre per inch, rounded */
    LOMETRIC_INCH = 254,
    /* The Welcome preview control */
    PREVIEW_ID = 25700,
    PREVIEW_CX = 165,
    PREVIEW_CY = 126,
    PREVIEW_MIN_CX = 188,
    PREVIEW_MIN_CY = 243
};

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    unsigned dpi = (unsigned)strtoul(argv[1], NULL, 10);
    CHECK(GProcessState.display_dpi == dpi);
    HAB anchor = WinInitialize(0);
    CHECK(anchor);
    HMQ queue = WinCreateMsgQueue(anchor, 0);
    CHECK(queue);
    HDC dc = DevOpenDC(anchor, OD_SCREEN, "*", 0, NULL, 0);
    CHECK(dc);
    LONG caps[CAPS_COUNT];
    CHECK(DevQueryCaps(dc, 0, CAPS_COUNT, caps));
    CHECK(caps[CAPS_HORIZONTAL_FONT_RES] == (LONG)dpi && caps[CAPS_VERTICAL_FONT_RES] == (LONG)dpi);
    const char *depth_text = getenv("TWOINE_DISPLAY_DEPTH");
    const LONG depth = depth_text ? strtol(depth_text, NULL, 10) : 24;
    CHECK(caps[CAPS_COLOR_BITCOUNT] == depth && caps[CAPS_COLORS] == 1L << depth &&
          caps[CAPS_PHYS_COLORS] == 1L << depth);
    CHECK(WinQuerySysValue(HWND_DESKTOP, SV_CYTITLEBAR) ==
          (LONG)((TITLE_BAR_HEIGHT * dpi + REFERENCE_DPI / 2) / REFERENCE_DPI));
    CHECK(WinQuerySysValue(HWND_DESKTOP, SV_CXVSCROLL) ==
          (LONG)((SCROLL_BAR_WIDTH * dpi + REFERENCE_DPI / 2) / REFERENCE_DPI));
    SIZEL size = {0, 0};
    HPS space = GpiCreatePS(anchor, dc, &size, PU_LOMETRIC | GPIA_ASSOC);
    CHECK(space);
    LONG formats[2];
    CHECK(GpiQueryDeviceBitmapFormats(space, 2, formats) && formats[0] == 1 && formats[1] == depth);
    POINTL inch = {LOMETRIC_INCH, LOMETRIC_INCH};
    CHECK(GpiConvert(space, CVTC_WORLD, CVTC_DEVICE, 1, &inch));
    CHECK(inch.x == (LONG)dpi && inch.y == (LONG)dpi);
    FONTMETRICS metrics;
    CHECK(GpiQueryFontMetrics(space, sizeof metrics, &metrics));
    CHECK(metrics.sXDeviceRes == dpi && metrics.sYDeviceRes == dpi);
    CHECK(metrics.lMaxBaselineExt >= (LONG)(12 * dpi / REFERENCE_DPI));

    const struct
    {
        unsigned dpi;
        POINTL cell, one, three, preview, inverse_one, inverse_three;
    } cases[] = {{96, {6, 16}, {2, 2}, {5, 6}, {248, 252}, {1, 1}, {2, 2}},
                 {120, {8, 20}, {2, 3}, {6, 8}, {330, 315}, {1, 0}, {2, 1}},
                 {144, {9, 24}, {2, 3}, {7, 9}, {371, 378}, {0, 0}, {1, 1}},
                 {192, {12, 32}, {3, 4}, {9, 12}, {495, 504}, {0, 0}, {1, 1}}};

    unsigned selected = 0;
    while (selected < sizeof cases / sizeof cases[0] && cases[selected].dpi != dpi)
        selected++;
    CHECK(selected < sizeof cases / sizeof cases[0]);
    POINTL dialog = {DIALOG_CELL_X, DIALOG_CELL_Y};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &dialog, 1, TRUE));
    CHECK(dialog.x == cases[selected].cell.x && dialog.y == cases[selected].cell.y);
    const char *fonts[] = {"8.Helv", "24.Courier", "10.System Proportional"};
    for (unsigned font = 0; font < sizeof fonts / sizeof fonts[0]; font++)
    {
        HWND window = WinCreateWindow(HWND_DESKTOP, (PSZ)(uintptr_t)WC_STATIC, "Dialog units",
                                      SS_TEXT, 0, 0, 500, 500, 0, HWND_TOP, 100 + font, NULL, NULL);
        CHECK(window && WinSetPresParam(window, PP_FONTNAMESIZE, (ULONG)strlen(fonts[font]) + 1,
                                        (void *)fonts[font]));
        HPS window_ps = WinGetPS(window);
        FONTMETRICS before, after;
        CHECK(window_ps && GpiQueryFontMetrics(window_ps, sizeof before, &before));
        CHECK(WinReleasePS(window_ps));
        if (dpi == 96 && !font)
            CHECK(before.lMaxBaselineExt == 13);
        POINTL mapped[] = {{1, 1},
                           {-1, -1},
                           {3, 3},
                           {-3, -3},
                           {PREVIEW_CX, PREVIEW_CY},
                           {-PREVIEW_CX, -PREVIEW_CY}};
        CHECK(WinMapDlgPoints(window, mapped, 6, TRUE));
        POINTL expected[] = {cases[selected].one, cases[selected].three, cases[selected].preview};
        for (unsigned i = 0; i < 3; i++)
        {
            CHECK(mapped[2 * i].x == expected[i].x && mapped[2 * i].y == expected[i].y);
            CHECK(mapped[2 * i + 1].x == -expected[i].x && mapped[2 * i + 1].y == -expected[i].y);
        }
        POINTL inverse[] = {{1, 1}, {-1, -1}, {3, 3}, {-3, -3}, cases[selected].preview};
        CHECK(WinMapDlgPoints(window, inverse, 5, FALSE));
        CHECK(inverse[0].x == cases[selected].inverse_one.x &&
              inverse[0].y == cases[selected].inverse_one.y);
        CHECK(inverse[1].x == -inverse[0].x && inverse[1].y == -inverse[0].y);
        CHECK(inverse[2].x == cases[selected].inverse_three.x &&
              inverse[2].y == cases[selected].inverse_three.y);
        CHECK(inverse[3].x == -inverse[2].x && inverse[3].y == -inverse[2].y);
        CHECK(inverse[4].x == PREVIEW_CX && inverse[4].y == PREVIEW_CY);
        window_ps = WinGetPS(window);
        CHECK(window_ps && GpiQueryFontMetrics(window_ps, sizeof after, &after));
        CHECK(WinReleasePS(window_ps) && before.lMaxBaselineExt == after.lMaxBaselineExt &&
              before.lAveCharWidth == after.lAveCharWidth);
        CHECK(WinDestroyWindow(window));
    }
    /* The original Welcome preview control is165x126 DLU and draws188x243 pixels. */
    unsigned char template_bytes[128] = {0};
    DLGTEMPLATE *template = (void *)template_bytes;
    template->cbTemplate = sizeof template_bytes;
    template->codepage = 437;
    template->offadlgti = offsetof(DLGTEMPLATE, adlgti);
    DLGTITEM *items = (void *)(template_bytes + offsetof(DLGTEMPLATE, adlgti));
    for (unsigned i = 0; i < 2; i++)
        items[i].offPresParams = items[i].offCtlData = 0xffff; /* none */
    items[0].cChildren = 1;
    items[0].offClassName = (USHORT)WC_FRAME;
    items[0].cx = 200;
    items[0].cy = 160;
    items[1].offClassName = (USHORT)WC_STATIC;
    items[1].flStyle = SS_TEXT;
    items[1].id = PREVIEW_ID;
    items[1].cx = PREVIEW_CX;
    items[1].cy = PREVIEW_CY;
    HWND preview_dialog = WinCreateDlg(HWND_DESKTOP, 0, NULL, template, NULL);
    CHECK(preview_dialog);
    SWP preview;
    CHECK(WinQueryWindowPos(WinWindowFromID(preview_dialog, PREVIEW_ID), &preview));
    CHECK(preview.cx == cases[selected].preview.x && preview.cy == cases[selected].preview.y);
    CHECK(preview.cx >= PREVIEW_MIN_CX && preview.cy >= PREVIEW_MIN_CY);
    CHECK(WinDestroyWindow(preview_dialog));
    POINTL unchanged[] = {{1, 1}, {INT32_MAX, INT32_MAX}};
    CHECK(!WinMapDlgPoints(HWND_DESKTOP, unchanged, 2, TRUE));
    CHECK(unchanged[0].x == 1 && unchanged[0].y == 1 && unchanged[1].x == INT32_MAX);
    CHECK(!WinMapDlgPoints(0x7fffffffu, &dialog, 1, TRUE));
    CHECK(WinMapDlgPoints(HWND_DESKTOP, NULL, 0, TRUE));
    CHECK(!WinMapDlgPoints(HWND_DESKTOP, NULL, 1, TRUE));
    GProcessState.display_dpi = 96;
    POINTL cached = {DIALOG_CELL_X, DIALOG_CELL_Y};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &cached, 1, TRUE) && cached.x == 6 && cached.y == 16);
    GProcessState.display_dpi = 120;
    cached = (POINTL){DIALOG_CELL_X, DIALOG_CELL_Y};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &cached, 1, TRUE) && cached.x == 8 && cached.y == 20);
    GProcessState.display_dpi = dpi;
    cached = (POINTL){DIALOG_CELL_X, DIALOG_CELL_Y};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &cached, 1, TRUE) && cached.x == cases[selected].cell.x &&
          cached.y == cases[selected].cell.y);
    PSZ printer_data[2] = {"2INEPDF", "2INEPDF"};
    HDC printer = DevOpenDC(anchor, OD_INFO, "*", 2, printer_data, 0);
    CHECK(printer);
    CHECK(DevQueryCaps(printer, 0, CAPS_COUNT, caps) &&
          caps[CAPS_HORIZONTAL_FONT_RES] == PRINTER_DPI &&
          caps[CAPS_VERTICAL_FONT_RES] == PRINTER_DPI);
    CHECK(DevCloseDC(printer) != (ULONG)-1);
    CHECK(GpiDestroyPS(space));
    CHECK(DevCloseDC(dc) != (ULONG)-1);
    CHECK(WinDestroyMsgQueue(queue));
    CHECK(WinTerminate(anchor));
    return 0;
}
