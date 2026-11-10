/* Public domain. No rights reserved. Wholly AI-authored. */
/* Raw bitmap uploads, scan transfers and palette/ROP/clip conversion contracts. */
#include "os2/include/private/export.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Guest BITMAPINFOHEADER (12 bytes) and BITMAPINFOHEADER2 (64 bytes) layouts */
enum
{
    BMIH_SIZE = 12,
    BMIH_CX_OFFSET = 4,
    BMIH_CY_OFFSET = 6,
    BMIH_CPLANES_OFFSET = 8,
    BMIH_CBITCOUNT_OFFSET = 10,
    RGB_SIZE = 3,
    BMIH2_SIZE = 64,
    BMIH2_CX_OFFSET = 4,
    BMIH2_CY_OFFSET = 8,
    BMIH2_CPLANES_OFFSET = 12,
    BMIH2_CBITCOUNT_OFFSET = 14,
    RGB2_SIZE = 4
};

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static uint32 pixel(HPS ps, LONG x, LONG y)
{
    uint32 result;
    CHECK(lx_pm_raster_read(ps, x, y, 1, 1, &result));
    return result;
}

int main(void)
{
    HAB hab = WinInitialize(0);
    CHECK(hab);
    HDC dc = DevOpenDC(hab, OD_MEMORY, "*", 0, NULL, 0);
    SIZEL size = {8, 8};
    HPS ps = GpiCreatePS(hab, dc, &size, GPIA_ASSOC | GPIT_MICRO | PU_PELS);
    CHECK(ps);
    uint8 header[BMIH2_SIZE] = {BMIH2_SIZE};
    lx_guest_store_u32(header + BMIH2_CX_OFFSET, 8);
    lx_guest_store_u32(header + BMIH2_CY_OFFSET, 8);
    lx_guest_store_u16(header + BMIH2_CPLANES_OFFSET, 1);
    lx_guest_store_u16(header + BMIH2_CBITCOUNT_OFFSET, 24);
    ULONG bitmap = GpiCreateBitmap(ps, header, 0, NULL, NULL);
    CHECK(bitmap && GpiSetBitmap(ps, bitmap) == 0);
    uint8 info[BMIH2_SIZE + 2 * RGB2_SIZE] = {BMIH2_SIZE};
    lx_guest_store_u32(info + BMIH2_CX_OFFSET, 2);
    lx_guest_store_u32(info + BMIH2_CY_OFFSET, 2);
    lx_guest_store_u16(info + BMIH2_CPLANES_OFFSET, 1);
    lx_guest_store_u16(info + BMIH2_CBITCOUNT_OFFSET, 1);
    info[BMIH2_SIZE + 2] = 255;                     /* index0 red */
    info[BMIH2_SIZE + RGB2_SIZE + 1] = 255;         /* index1 green */
    uint8 bits[8] = {0x40, 0, 0, 0, 0x80, 0, 0, 0}; /* bottom red/green; top green/red */
    POINTL points[4] = {{1, 1}, {5, 5}, {0, 0}, {2, 2}};
    CHECK(GpiDrawBits(ps, bits, info, 4, points, ROP_SRCCOPY, BBO_IGNORE) == GPI_OK);
    CHECK(pixel(ps, 1, 1) == 0xff0000 && pixel(ps, 4, 1) == 0x00ff00);
    CHECK(pixel(ps, 1, 4) == 0x00ff00 && pixel(ps, 4, 4) == 0xff0000);
    CHECK(GpiDrawBits(ps, bits, info, 4, points, ROP_SRCINVERT, BBO_IGNORE) == GPI_OK);
    CHECK(pixel(ps, 1, 1) == 0);
    CHECK(GpiDrawBits(ps, bits, info, 4, points, ROP_SRCCOPY, 4) == GPI_ERROR);
    POINTL outside[4] = {{-1, -1}, {3, 3}, {0, 0}, {2, 2}};
    CHECK(GpiDrawBits(ps, bits, info, 4, outside, ROP_SRCINVERT, BBO_IGNORE) &&
          pixel(ps, 0, 0) == 0xff0000);
    CHECK(GpiDrawBits(ps, bits, info, 4, outside, ROP_SRCINVERT, BBO_IGNORE) &&
          pixel(ps, 0, 0) == 0);
    /* Compression OR/AND operates on indexed values, even with inverted colors. */
    POINTL small[4] = {{0, 0}, {1, 1}, {0, 0}, {2, 2}};
    CHECK(GpiDrawBits(ps, bits, info, 4, small, ROP_SRCCOPY, BBO_OR) &&
          pixel(ps, 0, 0) == 0x00ff00);
    CHECK(GpiDrawBits(ps, bits, info, 4, small, ROP_SRCCOPY, BBO_AND) &&
          pixel(ps, 0, 0) == 0xff0000);
    RECTL clip = {2, 2, 4, 4};
    ULONG region = GpiCreateRegion(ps, 1, &clip);
    CHECK(region);
    ULONG previous = 0;
    CHECK(GpiSetClipRegion(ps, region, &previous));
    CHECK(GpiDrawBits(ps, bits, info, 4, points, ROP_SRCCOPY, BBO_IGNORE));
    CHECK(pixel(ps, 1, 1) == 0 && pixel(ps, 2, 2) == 0xff0000);
    CHECK(GpiSetClipRegion(ps, 0, &previous) && previous == region && GpiDestroyRegion(ps, region));
    uint8 query[BMIH2_SIZE] = {BMIH2_SIZE}, output[8 * 3 * 8 + 4];
    memset(output, 0xa5, sizeof output);
    lx_guest_store_u16(query + BMIH2_CPLANES_OFFSET, 1);
    lx_guest_store_u16(query + BMIH2_CBITCOUNT_OFFSET, 24);
    CHECK(GpiQueryBitmapBits(ps, 0, 1, output, query) == 1);
    CHECK(lx_guest_load_u32(query + BMIH2_CX_OFFSET) == 8 &&
          lx_guest_load_u32(query + BMIH2_CY_OFFSET) == 8);
    CHECK(output[0] == 0 && output[1] == 0 && output[2] == 255 && output[24] == 0xa5);
    CHECK(GpiQueryBitmapBits(ps, 8, 0, NULL, query) == 0);
    CHECK(GpiQueryBitmapBits(ps, 9, 1, output, query) == GPI_ALTERROR);
    CHECK(GpiQueryBitmapBits(ps, 0, 1, NULL, query) == GPI_ALTERROR);
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteBitmap(bitmap));
    /* Preserve the original palette and high-bit-first DWORD-padded scan rows. */
    bitmap = GpiCreateBitmap(ps, info, CBM_INIT, bits, info);
    CHECK(bitmap && GpiSetBitmap(ps, bitmap) == 0);
    /* A BITMAPINFOHEADER, two RGB entries and a guard */
    uint8 indexed[BMIH_SIZE + 2 * RGB_SIZE + 4] = {BMIH_SIZE}, scans[12];
    memset(scans, 0xa5, sizeof scans);
    memset(indexed + BMIH_SIZE + 2 * RGB_SIZE, 0xa5, 4);
    lx_guest_store_u16(indexed + BMIH_CPLANES_OFFSET, 1);
    lx_guest_store_u16(indexed + BMIH_CBITCOUNT_OFFSET, 1);
    CHECK(GpiQueryBitmapBits(ps, 0, 20, scans, indexed) == 2);
    CHECK(lx_guest_load_u16(indexed + BMIH_CX_OFFSET) == 2 &&
          lx_guest_load_u16(indexed + BMIH_CY_OFFSET) == 2);
    CHECK(!memcmp(indexed + BMIH_SIZE, info + BMIH2_SIZE, RGB_SIZE) &&
          !memcmp(indexed + BMIH_SIZE + RGB_SIZE, info + BMIH2_SIZE + RGB2_SIZE, RGB_SIZE));
    CHECK(!memcmp(scans, bits, 8) && scans[8] == 0xa5 && indexed[BMIH_SIZE + 2 * RGB_SIZE] == 0xa5);
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteBitmap(bitmap));
    CHECK(GpiDestroyPS(ps) && DevCloseDC(dc) == 0 && WinTerminate(hab));
    puts("GpiDrawBits and QueryBitmapBits palette/scan/scaling/ROP/clipping contracts passed");
    return 0;
}
