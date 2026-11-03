/* Public domain. No rights reserved. Wholly AI-authored. */
/* Raw bitmap uploads, scan transfers and palette/ROP/clip conversion contracts. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/graphics.h"
#include "guest/abi.h"
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
    HDC dc = DevOpenDC(hab, 8, "*", 0, NULL, 0);
    SIZEL size = {8, 8};
    HPS ps = GpiCreatePS(hab, dc, &size, 0x5008);
    CHECK(ps);
    uint8 header[64] = {64};
    lx_guest_store_u32(header + 4, 8);
    lx_guest_store_u32(header + 8, 8);
    lx_guest_store_u16(header + 12, 1);
    lx_guest_store_u16(header + 14, 24);
    ULONG bitmap = GpiCreateBitmap(ps, header, 0, NULL, NULL);
    CHECK(bitmap && GpiSetBitmap(ps, bitmap) == 0);
    uint8 info[64 + 8] = {64};
    lx_guest_store_u32(info + 4, 2);
    lx_guest_store_u32(info + 8, 2);
    lx_guest_store_u16(info + 12, 1);
    lx_guest_store_u16(info + 14, 1);
    info[64 + 2] = 255;                             /* index0 red */
    info[68 + 1] = 255;                             /* index1 green */
    uint8 bits[8] = {0x40, 0, 0, 0, 0x80, 0, 0, 0}; /* bottom red/green; top green/red */
    POINTL points[4] = {{1, 1}, {5, 5}, {0, 0}, {2, 2}};
    CHECK(GpiDrawBits(ps, bits, info, 4, points, 0xcc, 2) == 1);
    CHECK(pixel(ps, 1, 1) == 0xff0000 && pixel(ps, 4, 1) == 0x00ff00);
    CHECK(pixel(ps, 1, 4) == 0x00ff00 && pixel(ps, 4, 4) == 0xff0000);
    CHECK(GpiDrawBits(ps, bits, info, 4, points, 0x66, 2) == 1);
    CHECK(pixel(ps, 1, 1) == 0);
    CHECK(GpiDrawBits(ps, bits, info, 4, points, 0xcc, 4) == 0);
    POINTL outside[4] = {{-1, -1}, {3, 3}, {0, 0}, {2, 2}};
    CHECK(GpiDrawBits(ps, bits, info, 4, outside, 0x66, 2) && pixel(ps, 0, 0) == 0xff0000);
    CHECK(GpiDrawBits(ps, bits, info, 4, outside, 0x66, 2) && pixel(ps, 0, 0) == 0);
    /* Compression OR/AND operates on indexed values, even with inverted colors. */
    POINTL small[4] = {{0, 0}, {1, 1}, {0, 0}, {2, 2}};
    CHECK(GpiDrawBits(ps, bits, info, 4, small, 0xcc, 0) && pixel(ps, 0, 0) == 0x00ff00);
    CHECK(GpiDrawBits(ps, bits, info, 4, small, 0xcc, 1) && pixel(ps, 0, 0) == 0xff0000);
    RECTL clip = {2, 2, 4, 4};
    ULONG region = GpiCreateRegion(ps, 1, &clip);
    CHECK(region);
    ULONG previous = 0;
    CHECK(GpiSetClipRegion(ps, region, &previous));
    CHECK(GpiDrawBits(ps, bits, info, 4, points, 0xcc, 2));
    CHECK(pixel(ps, 1, 1) == 0 && pixel(ps, 2, 2) == 0xff0000);
    CHECK(GpiSetClipRegion(ps, 0, &previous) && previous == region && GpiDestroyRegion(ps, region));
    uint8 query[64] = {64}, output[8 * 3 * 8 + 4];
    memset(output, 0xa5, sizeof output);
    lx_guest_store_u16(query + 12, 1);
    lx_guest_store_u16(query + 14, 24);
    CHECK(GpiQueryBitmapBits(ps, 0, 1, output, query) == 1);
    CHECK(lx_guest_load_u32(query + 4) == 8 && lx_guest_load_u32(query + 8) == 8);
    CHECK(output[0] == 0 && output[1] == 0 && output[2] == 255 && output[24] == 0xa5);
    CHECK(GpiQueryBitmapBits(ps, 8, 0, NULL, query) == 0);
    CHECK(GpiQueryBitmapBits(ps, 9, 1, output, query) == -1);
    CHECK(GpiQueryBitmapBits(ps, 0, 1, NULL, query) == -1);
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteBitmap(bitmap));
    /* Preserve the original palette and high-bit-first DWORD-padded scan rows. */
    bitmap = GpiCreateBitmap(ps, info, 4, bits, info);
    CHECK(bitmap && GpiSetBitmap(ps, bitmap) == 0);
    uint8 indexed[12 + 6 + 4] = {12}, scans[12];
    memset(scans, 0xa5, sizeof scans);
    memset(indexed + 18, 0xa5, 4);
    lx_guest_store_u16(indexed + 8, 1);
    lx_guest_store_u16(indexed + 10, 1);
    CHECK(GpiQueryBitmapBits(ps, 0, 20, scans, indexed) == 2);
    CHECK(lx_guest_load_u16(indexed + 4) == 2 && lx_guest_load_u16(indexed + 6) == 2);
    CHECK(!memcmp(indexed + 12, info + 64, 3) && !memcmp(indexed + 15, info + 68, 3));
    CHECK(!memcmp(scans, bits, 8) && scans[8] == 0xa5 && indexed[18] == 0xa5);
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteBitmap(bitmap));
    CHECK(GpiDestroyPS(ps) && DevCloseDC(dc) == 0 && WinTerminate(hab));
    puts("GpiDrawBits and QueryBitmapBits palette/scan/scaling/ROP/clipping contracts passed");
    return 0;
}
