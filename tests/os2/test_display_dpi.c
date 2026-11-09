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

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    unsigned dpi = (unsigned)strtoul(argv[1], NULL, 10);
    CHECK(GProcessState.display_dpi == dpi);
    HAB anchor = WinInitialize(0);
    CHECK(anchor);
    HMQ queue = WinCreateMsgQueue(anchor, 0);
    CHECK(queue);
    HDC dc = DevOpenDC(anchor, 0, "*", 0, NULL, 0);
    CHECK(dc);
    LONG caps[42];
    CHECK(DevQueryCaps(dc, 0, 42, caps));
    CHECK(caps[37] == (LONG)dpi && caps[38] == (LONG)dpi);
    const char *depth_text = getenv("TWOINE_DISPLAY_DEPTH");
    const LONG depth = depth_text ? strtol(depth_text, NULL, 10) : 24;
    CHECK(caps[16] == depth && caps[14] == 1L << depth && caps[33] == 1L << depth);
    CHECK(WinQuerySysValue(HWND_DESKTOP, 30) == (LONG)((20 * dpi + 48) / 96));
    CHECK(WinQuerySysValue(HWND_DESKTOP, 22) == (LONG)((16 * dpi + 48) / 96));
    SIZEL size = {0, 0};
    HPS space = GpiCreatePS(anchor, dc, &size, 0x0c | GPIA_ASSOC);
    CHECK(space);
    LONG formats[2];
    CHECK(GpiQueryDeviceBitmapFormats(space, 2, formats) && formats[0] == 1 && formats[1] == depth);
    POINTL inch = {254, 254};
    CHECK(GpiConvert(space, 1, 5, 1, &inch));
    CHECK(inch.x == (LONG)dpi && inch.y == (LONG)dpi);
    FONTMETRICS metrics;
    CHECK(GpiQueryFontMetrics(space, sizeof metrics, &metrics));
    CHECK(metrics.sXDeviceRes == dpi && metrics.sYDeviceRes == dpi);
    CHECK(metrics.lMaxBaselineExt >= (LONG)(12 * dpi / 96));

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
    POINTL dialog = {4, 8};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &dialog, 1, TRUE));
    CHECK(dialog.x == cases[selected].cell.x && dialog.y == cases[selected].cell.y);
    const char *fonts[] = {"8.Helv", "24.Courier", "10.System Proportional"};
    for (unsigned font = 0; font < sizeof fonts / sizeof fonts[0]; font++)
    {
        HWND window = WinCreateWindow(HWND_DESKTOP, (PSZ)(uintptr_t)WC_STATIC, "Dialog units", 1, 0,
                                      0, 500, 500, 0, HWND_TOP, 100 + font, NULL, NULL);
        CHECK(window &&
              WinSetPresParam(window, 15, (ULONG)strlen(fonts[font]) + 1, (void *)fonts[font]));
        HPS window_ps = WinGetPS(window);
        FONTMETRICS before, after;
        CHECK(window_ps && GpiQueryFontMetrics(window_ps, sizeof before, &before));
        CHECK(WinReleasePS(window_ps));
        if (dpi == 96 && !font)
            CHECK(before.lMaxBaselineExt == 13);
        POINTL mapped[] = {{1, 1}, {-1, -1}, {3, 3}, {-3, -3}, {165, 126}, {-165, -126}};
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
        CHECK(inverse[4].x == 165 && inverse[4].y == 126);
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
    template->offadlgti = 14;
    DLGTITEM *items = (void *)(template_bytes + 14);
    for (unsigned i = 0; i < 2; i++)
        items[i].offPresParams = items[i].offCtlData = 0xffff;
    items[0].cChildren = 1;
    items[0].offClassName = 1;
    items[0].cx = 200;
    items[0].cy = 160;
    items[1].offClassName = 5;
    items[1].flStyle = 1;
    items[1].id = 25700;
    items[1].cx = 165;
    items[1].cy = 126;
    HWND preview_dialog = WinCreateDlg(HWND_DESKTOP, 0, NULL, template, NULL);
    CHECK(preview_dialog);
    SWP preview;
    CHECK(WinQueryWindowPos(WinWindowFromID(preview_dialog, 25700), &preview));
    CHECK(preview.cx == cases[selected].preview.x && preview.cy == cases[selected].preview.y);
    CHECK(preview.cx >= 188 && preview.cy >= 243);
    CHECK(WinDestroyWindow(preview_dialog));
    POINTL unchanged[] = {{1, 1}, {INT32_MAX, INT32_MAX}};
    CHECK(!WinMapDlgPoints(HWND_DESKTOP, unchanged, 2, TRUE));
    CHECK(unchanged[0].x == 1 && unchanged[0].y == 1 && unchanged[1].x == INT32_MAX);
    CHECK(!WinMapDlgPoints(0x7fffffffu, &dialog, 1, TRUE));
    CHECK(WinMapDlgPoints(HWND_DESKTOP, NULL, 0, TRUE));
    CHECK(!WinMapDlgPoints(HWND_DESKTOP, NULL, 1, TRUE));
    GProcessState.display_dpi = 96;
    POINTL cached = {4, 8};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &cached, 1, TRUE) && cached.x == 6 && cached.y == 16);
    GProcessState.display_dpi = 120;
    cached = (POINTL){4, 8};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &cached, 1, TRUE) && cached.x == 8 && cached.y == 20);
    GProcessState.display_dpi = dpi;
    cached = (POINTL){4, 8};
    CHECK(WinMapDlgPoints(HWND_DESKTOP, &cached, 1, TRUE) && cached.x == cases[selected].cell.x &&
          cached.y == cases[selected].cell.y);
    PSZ printer_data[2] = {"2INEPDF", "2INEPDF"};
    HDC printer = DevOpenDC(anchor, 6, "*", 2, printer_data, 0);
    CHECK(printer);
    CHECK(DevQueryCaps(printer, 0, 42, caps) && caps[37] == 300 && caps[38] == 300);
    CHECK(DevCloseDC(printer) != (ULONG)-1);
    CHECK(GpiDestroyPS(space));
    CHECK(DevCloseDC(dc) != (ULONG)-1);
    CHECK(WinDestroyMsgQueue(queue));
    CHECK(WinTerminate(anchor));
    return 0;
}
