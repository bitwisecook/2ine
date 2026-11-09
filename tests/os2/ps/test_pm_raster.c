/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pixel coverage of the shared graphics engine: scan conversion, lines and raster operations, as
   GPI (PMGPI) and Win32 GDI (PMWINX) draw them. Each case compares a whole canvas. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/os2win.h"
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

enum
{
    SIZE = 16
};

static int failures;

/* One character per pel, top row first. */
static char pel_char(uint32_t rgb)
{
    switch (rgb & 0xffffff)
    {
        case 0xffffff:
            return '.';
        case 0x000000:
            return '#';
        case 0xff0000:
            return 'R';
        case 0x00ff00:
            return 'G';
        case 0x0000ff:
            return 'B';
        case 0x00ffff:
            return 'c';
        case 0xff00ff:
            return 'm';
        case 0xffff00:
            return 'y';
        default:
            return '?';
    }
}

static void compare(const char *name, const char *actual, const char *expected)
{
    if (!strcmp(actual, expected))
        return;
    failures++;
    fprintf(stderr, "%s differs; observed:\n", name);
    for (const char *row = actual; *row; row += SIZE + 1)
        fprintf(stderr, "        \"%.*s\\n\"\n", SIZE, row);
}

static void expect_gpi(const char *name, HPS ps, const char *expected)
{
    char actual[SIZE * (SIZE + 1) + 1], *out = actual;
    for (LONG y = SIZE - 1; y >= 0; y--)
    {
        uint32_t row[SIZE];
        CHECK(lx_pm_raster_read(ps, 0, y, SIZE, 1, row));
        for (unsigned x = 0; x < SIZE; x++)
            *out++ = pel_char(row[x]);
        *out++ = '\n';
    }
    *out = 0;
    compare(name, actual, expected);
}

static void expect_gdi(const char *name, ULONG dc, const char *expected)
{
    char actual[SIZE * (SIZE + 1) + 1], *out = actual;
    for (LONG y = 0; y < SIZE; y++)
    {
        for (LONG x = 0; x < SIZE; x++)
        {
            ULONG color = GetPixel(dc, x, y);
            *out++ = pel_char((color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255));
        }
        *out++ = '\n';
    }
    *out = 0;
    compare(name, actual, expected);
}

static void clear_gpi(HPS ps)
{
    RECTL all = {0, 0, SIZE, SIZE};
    CHECK(WinFillRect(ps, &all, CLR_WHITE));
}

/* A self-intersecting star: its centre is outside under the alternate rule. */
static const POINTL star[] = {{8, 15}, {13, 1}, {1, 10}, {15, 10}, {3, 1}};

static void fill_star_path(HPS ps, LONG options)
{
    clear_gpi(ps);
    CHECK(GpiBeginPath(ps, 1));
    CHECK(GpiMove(ps, (PPOINTL)&star[0]));
    CHECK(GpiPolyLine(ps, 4, (PPOINTL)&star[1]));
    CHECK(GpiCloseFigure(ps) && GpiEndPath(ps));
    CHECK(GpiFillPath(ps, 1, options));
}

static void gpi_contracts(HAB hab)
{
    HDC dc = DevOpenDC(hab, 8, "*", 0, NULL, 0);
    SIZEL size = {SIZE, SIZE};
    HPS ps = GpiCreatePS(hab, dc, &size, PU_PELS | GPIA_ASSOC);
    uint8_t header[12] = {12, 0, 0, 0, SIZE, 0, SIZE, 0, 1, 0, 24, 0};
    ULONG bitmap = GpiCreateBitmap(ps, header, 0, NULL, NULL);
    CHECK(dc && ps && bitmap && GpiSetBitmap(ps, bitmap) == 0);
    CHECK(GpiSetColor(ps, CLR_RED));

    fill_star_path(ps, FPATH_ALTERNATE);
    expect_gpi("GpiFillPath alternate inclusive", ps,
               "........R.......\n"
               "........R.......\n"
               ".......RRR......\n"
               ".......RRR......\n"
               ".......RRR......\n"
               "......RRRRR.....\n"
               "..RRRRR...RRRRR.\n"
               "...RRRR...RRRR..\n"
               "....RR.....RR...\n"
               ".....RR...RR....\n"
               ".....RRR.RRR....\n"
               "....RRRRRRRRR...\n"
               "....RRR...RRR...\n"
               "....RR.....RR...\n"
               "...RR.......RR..\n"
               "................\n");
    fill_star_path(ps, FPATH_WINDING);
    expect_gpi("GpiFillPath winding inclusive", ps,
               "........R.......\n"
               "........R.......\n"
               ".......RRR......\n"
               ".......RRR......\n"
               ".......RRR......\n"
               "......RRRRR.....\n"
               "..RRRRRRRRRRRRR.\n"
               "...RRRRRRRRRRR..\n"
               "....RRRRRRRRR...\n"
               ".....RRRRRRR....\n"
               ".....RRRRRRR....\n"
               "....RRRRRRRRR...\n"
               "....RRR...RRR...\n"
               "....RR.....RR...\n"
               "...RR.......RR..\n"
               "................\n");
    fill_star_path(ps, FPATH_ALTERNATE | FPATH_EXCL);
    expect_gpi("GpiFillPath alternate exclusive", ps,
               "................\n"
               "................\n"
               ".......RR.......\n"
               ".......RR.......\n"
               ".......RR.......\n"
               "......RRRR......\n"
               "..RRRR....RRRR..\n"
               "...RRR....RRR...\n"
               "....R......R....\n"
               ".....R....R.....\n"
               ".....RR..RR.....\n"
               "....RRRRRRRR....\n"
               "....RR....RR....\n"
               "....R......R....\n"
               "...R........R...\n"
               "................\n");

    clear_gpi(ps);
    CHECK(GpiBeginPath(ps, 1));
    CHECK(GpiMove(ps, (PPOINTL)&star[0]));
    CHECK(GpiPolyLine(ps, 4, (PPOINTL)&star[1]));
    CHECK(GpiCloseFigure(ps) && GpiEndPath(ps));
    ULONG region = GpiPathToRegion(ps, 1, FPATH_WINDING);
    CHECK(region && GpiPaintRegion(ps, region) && GpiDestroyRegion(ps, region));
    expect_gpi("GpiPathToRegion winding inclusive", ps,
               "........R.......\n"
               "........R.......\n"
               ".......RRR......\n"
               ".......RRR......\n"
               ".......RRR......\n"
               "......RRRRR.....\n"
               "..RRRRRRRRRRRRR.\n"
               "...RRRRRRRRRRR..\n"
               "....RRRRRRRRR...\n"
               ".....RRRRRRR....\n"
               ".....RRRRRRR....\n"
               "....RRRRRRRRR...\n"
               "....RRR...RRR...\n"
               "....RR.....RR...\n"
               "...RR.......RR..\n"
               "................\n");

    clear_gpi(ps);
    CHECK(GpiSetColor(ps, CLR_BLUE));
    POINTL from = {1, 2}, to = {14, 7}, steep = {4, 14}, back = {12, 0};
    CHECK(GpiMove(ps, &from) && GpiLine(ps, &to) && GpiLine(ps, &steep) && GpiLine(ps, &back));
    expect_gpi("GpiLine end points inclusive", ps,
               "................\n"
               "....B...........\n"
               ".....BB.........\n"
               ".....B.B........\n"
               "......B.B.......\n"
               "......B..BB.....\n"
               ".......B...B....\n"
               ".......B....BB..\n"
               "........B....BB.\n"
               ".........B.BB...\n"
               "........BBB.....\n"
               ".....BBB..B.....\n"
               "...BB.....B.....\n"
               ".BB........B....\n"
               "...........B....\n"
               "............B...\n");

    /* Raster operations: source stripes against destination stripes under a pattern. */
    HDC source_dc = DevOpenDC(hab, 8, "*", 0, NULL, 0);
    HPS source = GpiCreatePS(hab, source_dc, &size, PU_PELS | GPIA_ASSOC);
    ULONG source_bitmap = GpiCreateBitmap(source, header, 0, NULL, NULL);
    CHECK(source && source_bitmap && GpiSetBitmap(source, source_bitmap) == 0);
    clear_gpi(source);
    RECTL column = {0, 0, 4, SIZE};
    CHECK(WinFillRect(source, &column, CLR_RED));
    column = (RECTL){8, 0, 12, SIZE};
    CHECK(WinFillRect(source, &column, CLR_BLACK));
    /* 0xb8 (PSDPxax) takes the pattern where the source is 0 and the destination where it is 1. */
    static const LONG rops[] = {ROP_SRCCOPY,   ROP_SRCINVERT, ROP_SRCAND,     ROP_MERGEPAINT,
                                ROP_PATINVERT, ROP_DSTINVERT, ROP_NOTSRCCOPY, ROP_SRCERASE,
                                ROP_MERGECOPY, 0xb8};
    static const char *const rop_expected[] = {"................\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               "BRRRR...####...B\n"
                                               "BRRRR...####...B\n"
                                               "BRRRR...####...B\n"
                                               "BRRRR...####...B\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               "GRRRR...####...G\n"
                                               "GRRRR...####...G\n"
                                               "GRRRR...####...G\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               "BmmmmyyyBBBByyyB\n"
                                               "BmmmmyyyBBBByyyB\n"
                                               "BmmmmyyyBBBByyyB\n"
                                               "BmmmmyyyBBBByyyB\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               "GyyyymmmGGGGmmmG\n"
                                               "GyyyymmmGGGGmmmG\n"
                                               "GyyyymmmGGGGmmmG\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               "B####BBB####BBBB\n"
                                               "B####BBB####BBBB\n"
                                               "B####BBB####BBBB\n"
                                               "B####BBB####BBBB\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               ".RRRR...####....\n"
                                               "G####GGG####GGGG\n"
                                               "G####GGG####GGGG\n"
                                               "G####GGG####GGGG\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               "................\n"
                                               "................\n"
                                               "................\n"
                                               "BccccBBB....BBBB\n"
                                               "BccccBBB....BBBB\n"
                                               "BccccBBB....BBBB\n"
                                               "BccccBBB....BBBB\n"
                                               "................\n"
                                               "................\n"
                                               "................\n"
                                               "................\n"
                                               "GccccGGG....GGGG\n"
                                               "GccccGGG....GGGG\n"
                                               "GccccGGG....GGGG\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".cccccccccccccc.\n"
                                               ".cccccccccccccc.\n"
                                               ".cccccccccccccc.\n"
                                               "BmmmmmmmmmmmmmmB\n"
                                               "BmmmmmmmmmmmmmmB\n"
                                               "BmmmmmmmmmmmmmmB\n"
                                               "BmmmmmmmmmmmmmmB\n"
                                               ".cccccccccccccc.\n"
                                               ".cccccccccccccc.\n"
                                               ".cccccccccccccc.\n"
                                               ".cccccccccccccc.\n"
                                               "GyyyyyyyyyyyyyyG\n"
                                               "GyyyyyyyyyyyyyyG\n"
                                               "GyyyyyyyyyyyyyyG\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               "ByyyyyyyyyyyyyyB\n"
                                               "ByyyyyyyyyyyyyyB\n"
                                               "ByyyyyyyyyyyyyyB\n"
                                               "ByyyyyyyyyyyyyyB\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               "GmmmmmmmmmmmmmmG\n"
                                               "GmmmmmmmmmmmmmmG\n"
                                               "GmmmmmmmmmmmmmmG\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               "Bcccc###....###B\n"
                                               "Bcccc###....###B\n"
                                               "Bcccc###....###B\n"
                                               "Bcccc###....###B\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               ".cccc###....###.\n"
                                               "Gcccc###....###G\n"
                                               "Gcccc###....###G\n"
                                               "Gcccc###....###G\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               "BRRRRyyy####yyyB\n"
                                               "BRRRRyyy####yyyB\n"
                                               "BRRRRyyy####yyyB\n"
                                               "BRRRRyyy####yyyB\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               ".##############.\n"
                                               "GRRRRmmm####mmmG\n"
                                               "GRRRRmmm####mmmG\n"
                                               "GRRRRmmm####mmmG\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".RRRRRRR####RRR.\n"
                                               ".RRRRRRR####RRR.\n"
                                               ".RRRRRRR####RRR.\n"
                                               "BRRRRRRR####RRRB\n"
                                               "BRRRRRRR####RRRB\n"
                                               "BRRRRRRR####RRRB\n"
                                               "BRRRRRRR####RRRB\n"
                                               ".RRRRRRR####RRR.\n"
                                               ".RRRRRRR####RRR.\n"
                                               ".RRRRRRR####RRR.\n"
                                               ".RRRRRRR####RRR.\n"
                                               "GRRRRRRR####RRRG\n"
                                               "GRRRRRRR####RRRG\n"
                                               "GRRRRRRR####RRRG\n"
                                               "GGGGGGGGGGGGGGGG\n",
                                               "................\n"
                                               ".RRRR...RRRR....\n"
                                               ".RRRR...RRRR....\n"
                                               ".RRRR...RRRR....\n"
                                               "B####BBBRRRRBBBB\n"
                                               "B####BBBRRRRBBBB\n"
                                               "B####BBBRRRRBBBB\n"
                                               "B####BBBRRRRBBBB\n"
                                               ".RRRR...RRRR....\n"
                                               ".RRRR...RRRR....\n"
                                               ".RRRR...RRRR....\n"
                                               ".RRRR...RRRR....\n"
                                               "G####GGGRRRRGGGG\n"
                                               "G####GGGRRRRGGGG\n"
                                               "G####GGGRRRRGGGG\n"
                                               "GGGGGGGGGGGGGGGG\n"};
    for (unsigned i = 0; i < sizeof rops / sizeof *rops; i++)
    {
        clear_gpi(ps);
        RECTL row = {0, 0, SIZE, 4};
        CHECK(WinFillRect(ps, &row, CLR_GREEN));
        row = (RECTL){0, 8, SIZE, 12};
        CHECK(WinFillRect(ps, &row, CLR_BLUE));
        CHECK(GpiSetColor(ps, CLR_RED) && GpiSetPattern(ps, PATSYM_SOLID));
        POINTL points[4] = {{1, 1}, {15, 15}, {0, 0}, {SIZE, SIZE}};
        CHECK(GpiBitBlt(ps, source, 4, points, rops[i], BBO_IGNORE) == 1);
        char name[48];
        snprintf(name, sizeof name, "GpiBitBlt stretch ROP %02lX", (unsigned long)rops[i]);
        expect_gpi(name, ps, rop_expected[i]);
    }
    CHECK(GpiSetBitmap(source, 0) == source_bitmap && GpiDeleteBitmap(source_bitmap));
    CHECK(GpiDestroyPS(source) && DevCloseDC(source_dc) == 0);
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteBitmap(bitmap));
    CHECK(GpiDestroyPS(ps) && DevCloseDC(dc) == 0);
}

/* PMWINX's world transform, which EMF playback reaches through SetWorldTransform records. */
extern BOOL open32WorldTransform(ULONG dc, const double matrix[6], ULONG mode);

static void store_points(uint8_t *wire, const POINTL *points, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
    {
        lx_guest_store_u32(wire + i * 8, (ULONG)points[i].x);
        lx_guest_store_u32(wire + i * 8 + 4, (ULONG)points[i].y);
    }
}

static void clear_gdi(ULONG dc)
{
    CHECK(PatBlt(dc, 0, 0, SIZE, SIZE, 0xFF0062));
}

static void gdi_contracts(void)
{
    ULONG dc = CreateCompatibleDC(0);
    ULONG bitmap = CreateBitmap(SIZE, SIZE, 1, 24, NULL);
    CHECK(dc && bitmap && SelectObject(dc, bitmap));
    static const POINTL gdi_star[] = {{8, 0}, {13, 14}, {1, 5}, {15, 5}, {3, 14}};
    uint8_t wire[64];
    store_points(wire, gdi_star, 5);
    ULONG red = CreateSolidBrush(0x0000ff), null_pen = CreatePen(5, 0, 0);
    CHECK(red && null_pen && SelectObject(dc, red) && SelectObject(dc, null_pen));

    clear_gdi(dc);
    CHECK(SetPolyFillMode(dc, 1) && Polygon(dc, wire, 5));
    expect_gdi("Polygon alternate", dc,
               "................\n"
               ".......RR.......\n"
               ".......RR.......\n"
               ".......RR.......\n"
               "......RRRR......\n"
               "..RRRR....RRRR..\n"
               "...RRR....RRR...\n"
               "....R......R....\n"
               ".....R....R.....\n"
               ".....RR..RR.....\n"
               "....RRRRRRRR....\n"
               "....RR....RR....\n"
               "....R......R....\n"
               "...R........R...\n"
               "................\n"
               "................\n");
    clear_gdi(dc);
    CHECK(SetPolyFillMode(dc, 2) && Polygon(dc, wire, 5));
    expect_gdi("Polygon winding", dc,
               "................\n"
               ".......RR.......\n"
               ".......RR.......\n"
               ".......RR.......\n"
               "......RRRR......\n"
               "..RRRRRRRRRRRR..\n"
               "...RRRRRRRRRR...\n"
               "....RRRRRRRR....\n"
               ".....RRRRRR.....\n"
               ".....RRRRRR.....\n"
               "....RRRRRRRR....\n"
               "....RR....RR....\n"
               "....R......R....\n"
               "...R........R...\n"
               "................\n"
               "................\n");

    ULONG blue = CreatePen(0, 1, 0xff0000);
    CHECK(blue && SelectObject(dc, blue));
    clear_gdi(dc);
    CHECK(SetPolyFillMode(dc, 1) && Polygon(dc, wire, 5));
    expect_gdi("Polygon alternate with cosmetic outline", dc,
               "........B.......\n"
               ".......RB.......\n"
               ".......BRB......\n"
               ".......BRB......\n"
               "......RBRB......\n"
               ".BBBBBBBBBBBBBBB\n"
               "..BRRRB...BRRBB.\n"
               "...BBB....BRB...\n"
               ".....B....RB....\n"
               ".....BB..BBB....\n"
               "....BRRBBRRRB...\n"
               "....BR.B.BRRB...\n"
               "....BBB...BRB...\n"
               "...BB......BBB..\n"
               "...B.........B..\n"
               "................\n");

    ULONG wide = CreatePen(0, 3, 0x00ff00);
    CHECK(wide && SelectObject(dc, wide));
    clear_gdi(dc);
    CHECK(Polyline(dc, wire, 5));
    expect_gdi("Polyline three-pel cosmetic", dc,
               ".......GGG......\n"
               ".......GGGG.....\n"
               ".......GGGG.....\n"
               "........GGG.....\n"
               "GGGGGGGGGGGGGGGG\n"
               "GGGGGGGGGGGGGGGG\n"
               "GGGGGGGGGGGGGGGG\n"
               ".GGGGGG..GGGGGGG\n"
               "..GGGGGGGGGGGG..\n"
               "....GGGGGGGGGG..\n"
               ".....GGGGGGGGG..\n"
               "....GGGGGGGGGG..\n"
               "...GGGGGGGGGGGG.\n"
               "...GGGGG.GGGGGG.\n"
               "...GGG....GGGGG.\n"
               "............GGG.\n");

    uint8_t logbrush[12] = {0};
    lx_guest_store_u32(logbrush + 4, 0x000000);
    ULONG geometric = ExtCreatePen(0x10000 | 0x100 | 0x2000, 4, logbrush, 0, NULL);
    CHECK(geometric && SelectObject(dc, geometric));
    static const POINTL chevron[] = {{2, 12}, {7, 3}, {13, 12}};
    store_points(wire, chevron, 3);
    clear_gdi(dc);
    CHECK(Polyline(dc, wire, 3));
    expect_gdi("Polyline geometric square cap mitre join", dc,
               "................\n"
               "................\n"
               ".....####.......\n"
               "....######......\n"
               "....######......\n"
               "...########.....\n"
               "...#########....\n"
               "..#####.####....\n"
               "..####..#####...\n"
               ".#####...#####..\n"
               ".####.....####..\n"
               ".####.....#####.\n"
               ".###.......#####\n"
               ".##.........##..\n"
               "..#.........#...\n"
               "................\n");

    ULONG dashed = CreatePen(1, 1, 0x000000);
    CHECK(dashed && SelectObject(dc, dashed));
    clear_gdi(dc);
    CHECK(MoveToEx(dc, 0, 2, NULL) && LineTo(dc, 15, 2) && LineTo(dc, 2, 13));
    expect_gdi("LineTo dashed, end points exclusive", dc,
               "................\n"
               "................\n"
               "######..######.#\n"
               "..............#.\n"
               ".............#..\n"
               "...........##...\n"
               "..........#.....\n"
               "................\n"
               "................\n"
               ".......#........\n"
               ".....##.........\n"
               "....#...........\n"
               "...#............\n"
               "................\n"
               "................\n"
               "................\n");

    static const POINTL squares[] = {{2, 2}, {10, 2}, {10, 10}, {2, 10},
                                     {6, 6}, {14, 6}, {14, 14}, {6, 14}};
    uint8_t counts[8];
    lx_guest_store_u32(counts, 4);
    lx_guest_store_u32(counts + 4, 4);
    store_points(wire, squares, 8);
    ULONG region = CreatePolyPolygonRgn(wire, counts, 2, 1);
    clear_gdi(dc);
    CHECK(region && FillRgn(dc, region, red) && DeleteObject(region));
    expect_gdi("CreatePolyPolygonRgn alternate", dc,
               "................\n"
               "................\n"
               "..RRRRRRRR......\n"
               "..RRRRRRRR......\n"
               "..RRRRRRRR......\n"
               "..RRRRRRRR......\n"
               "..RRRR....RRRR..\n"
               "..RRRR....RRRR..\n"
               "..RRRR....RRRR..\n"
               "..RRRR....RRRR..\n"
               "......RRRRRRRR..\n"
               "......RRRRRRRR..\n"
               "......RRRRRRRR..\n"
               "......RRRRRRRR..\n"
               "................\n"
               "................\n");
    region = CreatePolyPolygonRgn(wire, counts, 2, 2);
    clear_gdi(dc);
    CHECK(region && FillRgn(dc, region, red) && DeleteObject(region));
    expect_gdi("CreatePolyPolygonRgn winding", dc,
               "................\n"
               "................\n"
               "..RRRRRRRR......\n"
               "..RRRRRRRR......\n"
               "..RRRRRRRR......\n"
               "..RRRRRRRR......\n"
               "..RRRRRRRRRRRR..\n"
               "..RRRRRRRRRRRR..\n"
               "..RRRRRRRRRRRR..\n"
               "..RRRRRRRRRRRR..\n"
               "......RRRRRRRR..\n"
               "......RRRRRRRR..\n"
               "......RRRRRRRR..\n"
               "......RRRRRRRR..\n"
               "................\n"
               "................\n");

    ULONG hatch = CreateHatchBrush(5, 0x0000ff);
    CHECK(hatch && SelectObject(dc, hatch));
    clear_gdi(dc);
    CHECK(SetBkColor(dc, 0x00ff00) != 0xffffffff && PatBlt(dc, 1, 1, 14, 14, 0xF00021));
    expect_gdi("PatBlt PATCOPY cross hatch, opaque", dc,
               "................\n"
               ".RGGGGGRGRGGGGG.\n"
               ".GRGGGRGGGRGGGR.\n"
               ".GGRGRGGGGGRGRG.\n"
               ".GGGRGGGGGGGRGG.\n"
               ".GGRGRGGGGGRGRG.\n"
               ".GRGGGRGGGRGGGR.\n"
               ".RGGGGGRGRGGGGG.\n"
               ".GGGGGGGRGGGGGG.\n"
               ".RGGGGGRGRGGGGG.\n"
               ".GRGGGRGGGRGGGR.\n"
               ".GGRGRGGGGGRGRG.\n"
               ".GGGRGGGGGGGRGG.\n"
               ".GGRGRGGGGGRGRG.\n"
               ".GRGGGRGGGRGGGR.\n"
               "................\n");
    CHECK(SetBkMode(dc, 1) && PatBlt(dc, 0, 0, 8, 16, 0x5A0049));
    expect_gdi("PatBlt PATINVERT cross hatch, transparent", dc,
               "c...............\n"
               ".#GGGGG#GRGGGGG.\n"
               ".G#GGG#GGGRGGGR.\n"
               ".GG#G#GGGGGRGRG.\n"
               ".GGG#GGGGGGGRGG.\n"
               ".GG#G#GGGGGRGRG.\n"
               ".G#GGG#GGGRGGGR.\n"
               ".#GGGGG#GRGGGGG.\n"
               "cGGGGGGGRGGGGGG.\n"
               ".#GGGGG#GRGGGGG.\n"
               ".G#GGG#GGGRGGGR.\n"
               ".GG#G#GGGGGRGRG.\n"
               ".GGG#GGGGGGGRGG.\n"
               ".GG#G#GGGGGRGRG.\n"
               ".G#GGG#GGGRGGGR.\n"
               ".c.....c........\n");
    CHECK(SetBkMode(dc, 2) && SelectObject(dc, red));

    /* A 3x2 bottom-up DIB: red green blue / black white black. */
    uint8_t info[40] = {0}, bits[24] = {0};
    lx_guest_store_u32(info, 40);
    lx_guest_store_u32(info + 4, 3);
    lx_guest_store_u32(info + 8, 2);
    lx_guest_store_u16(info + 12, 1);
    lx_guest_store_u16(info + 14, 24);
    static const uint8_t rows[2][9] = {{0, 0, 0, 255, 255, 255, 0, 0, 0},
                                       {0, 0, 255, 0, 255, 0, 255, 0, 0}};
    memcpy(bits, rows[0], 9);
    memcpy(bits + 12, rows[1], 9);
    clear_gdi(dc);
    CHECK(StretchDIBits(dc, 1, 1, 13, 9, 0, 0, 3, 2, bits, info, 0, 0xCC0020) == 2);
    expect_gdi("StretchDIBits SRCCOPY", dc,
               "................\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               ".#####....####..\n"
               ".#####....####..\n"
               ".#####....####..\n"
               ".#####....####..\n"
               "................\n"
               "................\n"
               "................\n"
               "................\n"
               "................\n"
               "................\n");
    CHECK(StretchDIBits(dc, 4, 4, -9, 11, 0, 0, 3, 2, bits, info, 0, 0x660046) == 2);
    expect_gdi("StretchDIBits SRCINVERT mirrored", dc,
               "................\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               "m###RRGGGGBBBB..\n"
               "m###RRGGGGBBBB..\n"
               "mRRR##....####..\n"
               "mRRR##....####..\n"
               "mRRR##....####..\n"
               "mRRR##....####..\n"
               "#...............\n"
               "#...............\n"
               "#...............\n"
               "#...............\n"
               "#...............\n"
               "................\n");
    CHECK(StretchDIBits(dc, 0, 12, 16, 4, 0, 0, 3, 2, bits, info, 0, 0xB8074A) == 2);
    expect_gdi("StretchDIBits pattern-selected source", dc,
               "................\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               ".RRRRRGGGGBBBB..\n"
               "m###RRGGGGBBBB..\n"
               "m###RRGGGGBBBB..\n"
               "mRRR##....####..\n"
               "mRRR##....####..\n"
               "mRRR##....####..\n"
               "mRRR##....####..\n"
               "#...............\n"
               "#...............\n"
               "#RRRRRyyyyymmmmm\n"
               "#RRRRRyyyyymmmmm\n"
               "RRRRRR.....RRRRR\n"
               "RRRRRR.....RRRRR\n");
    CHECK(SetStretchBltMode(dc, 1));
    clear_gdi(dc);
    CHECK(StretchDIBits(dc, 2, 2, 2, 1, 0, 0, 3, 2, bits, info, 0, 0xCC0020) == 2);
    CHECK(SetStretchBltMode(dc, 3));
    CHECK(StretchDIBits(dc, 6, 2, 2, 1, 0, 0, 3, 2, bits, info, 0, 0xCC0020) == 2);
    /* A rotating world transform maps every target pel back to one source pel. */
    static const double rotation[6] = {0.8, 0.6, -0.6, 0.8, 8, 4};
    CHECK(open32WorldTransform(dc, rotation, 0));
    CHECK(StretchDIBits(dc, 0, 0, 9, 6, 0, 0, 3, 2, bits, info, 0, 0xCC0020) == 2);
    CHECK(open32WorldTransform(dc, rotation, 1));
    expect_gdi("StretchDIBits reduced and rotated", dc,
               "................\n"
               "................\n"
               "..##..RG........\n"
               "................\n"
               "........R.......\n"
               ".......RRR......\n"
               "......RRRRGG....\n"
               ".....##RRGGGG...\n"
               "....####.GGGBB..\n"
               ".....##...GBBBB.\n"
               "..........##BB..\n"
               ".........####...\n"
               ".........###....\n"
               "..........#.....\n"
               "................\n"
               "................\n");

    CHECK(SelectObject(dc, null_pen) && DeleteObject(blue) && DeleteObject(wide) &&
          DeleteObject(geometric) && DeleteObject(dashed) && DeleteObject(hatch));
    CHECK(DeleteDC(dc) && DeleteObject(bitmap) && DeleteObject(red) && DeleteObject(null_pen));
}

int main(void)
{
    HAB hab = WinInitialize(0);
    CHECK(hab && WinCreateMsgQueue(hab, 0));
    gpi_contracts(hab);
    gdi_contracts();
    return failures != 0;
}
