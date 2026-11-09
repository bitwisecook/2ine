/* Public domain. No rights reserved. Wholly AI-authored. */
/* LUT8 software conversion, cross-thread presentation and buffer ownership. */
#include "os2/include/dive.h"
#include "os2/mm/video/dive/dive_pixels.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "emulator/runtime.h"
#include "emulator/features.h"
#include "host/thread.h"
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
static ULONG instance, image;

static void *blit_worker(void *unused)
{
    (void)unused;
    CHECK(DiveBlitImage(instance, image, DIVE_BUFFER_SCREEN) == DIVE_SUCCESS);
    return NULL;
}

int main(void)
{
    HAB hab = WinInitialize(0);
    CHECK(hab);
    HMQ queue = WinCreateMsgQueue(hab, 0);
    CHECK(queue);
    CHECK(WinRegisterClass(hab, "DiveContract", WinDefWindowProc, 0, 0));
    HWND window = WinCreateWindow(HWND_DESKTOP, "DiveContract", "DIVE", WS_VISIBLE, 20, 20, 4, 4, 0,
                                  HWND_TOP, 1, NULL, NULL);
    CHECK(window);
    ULONG formats[64];
    DIVE_CAPS caps = {0};
    caps.ulStructLen = sizeof caps;
    caps.ulFormatLength = sizeof formats;
    caps.pFormatData = formats;
    CHECK(lxRuntimeConfigureFeature("dive", "false") == 1);
    CHECK(DiveQueryCaps(&caps, 0) == DIVE_ERR_NO_DIRECT_ACCESS);
    CHECK(DiveOpen(&instance, 0, NULL) == DIVE_ERR_NO_DIRECT_ACCESS);
    CHECK(!instance);
    CHECK(lxRuntimeConfigureFeature("dive", "true") == 1);
    CHECK(DiveQueryCaps(&caps, 0) == DIVE_SUCCESS);
    CHECK(caps.ulDepth == 32 && caps.fScreenDirect && caps.ulInputFormats == DiveInputFormatCount &&
          caps.ulOutputFormats == DiveOutputFormatCount && formats[0] == DIVE_FOURCC_LUT8 &&
          formats[1] == DIVE_FOURCC_BGR4);
    CHECK(DiveOpen(&instance, 0, NULL) == DIVE_SUCCESS);
    CHECK(DiveAllocImageBuffer(instance, &image, DIVE_FOURCC_LUT8, 2, 2, 1, NULL) ==
          DIVE_ERR_INVALID_LINESIZE);
    CHECK(DiveAllocImageBuffer(instance, &image, DIVE_FOURCC_LUT8, 2, 2, 0, NULL) == DIVE_SUCCESS);
    PBYTE pixels;
    ULONG stride, lines;
    CHECK(DiveBeginImageBufferAccess(instance, image, &pixels, &stride, &lines) == DIVE_SUCCESS);
    CHECK(stride == 4 && lines == 2);
    CHECK(DiveBeginImageBufferAccess(instance, image, &pixels, &stride, &lines) ==
          DIVE_ERR_BUFFER_ALREADY_ACCESSED);
    CHECK(DiveFreeImageBuffer(instance, image) == DIVE_ERR_BUFFER_ALREADY_ACCESSED);
    pixels[0] = 1;
    pixels[1] = 2;
    pixels[stride] = 3;
    pixels[stride + 1] = 4;
    CHECK(DiveEndImageBufferAccess(instance, image) == DIVE_SUCCESS);
    CHECK(DiveEndImageBufferAccess(instance, image) == DIVE_ERR_BUFFER_NOT_ACCESSED);
    CHECK(DiveBlitImage(instance, image, 0) == DIVE_ERR_BLITTER_NOT_SETUP);
    BYTE palette[16] = {0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0};
    CHECK(DiveSetSourcePalette(instance, 1, 4, palette) == DIVE_SUCCESS);
    CHECK(DiveSetSourcePalette(instance, 255, 4, palette) == DIVE_ERR_INVALID_PALETTE);
    SETUP_BLITTER setup = {0};
    setup.ulStructLen = sizeof setup;
    setup.fInvert = 1;
    setup.fccSrcColorFormat = DIVE_FOURCC_LUT8;
    setup.ulSrcWidth = setup.ulSrcHeight = 2;
    setup.ulDstWidth = setup.ulDstHeight = 4;
    setup.lScreenPosX = setup.lScreenPosY = 20;
    setup.ulNumDstRects = DIVE_FULLY_VISIBLE;
    CHECK(DiveSetupBlitter(instance, &setup) == DIVE_SUCCESS);
    LxHostThread *worker = lx_host_thread_create(blit_worker, NULL, NULL);
    CHECK(worker && !lx_host_thread_join(worker, NULL));
    QMSG message;
    WinPeekMsg(hab, &message, 0, 0, 0, 0);
    HPS ps = WinGetPS(window);
    CHECK(ps);
    uint32_t actual[16];
    CHECK(lx_pm_raster_read(ps, 0, 0, 4, 4, actual));
    const uint32_t expected[16] = {0xff0000, 0xff0000, 0x00ff00, 0x00ff00, 0xff0000, 0xff0000,
                                   0x00ff00, 0x00ff00, 0x0000ff, 0x0000ff, 0xffffff, 0xffffff,
                                   0x0000ff, 0x0000ff, 0xffffff, 0xffffff};
    CHECK(!memcmp(actual, expected, sizeof actual));
    CHECK(WinReleasePS(ps));
    /* Transparent and partial-line submissions preserve all other retained pixels. */
    CHECK(DiveSetTransparentBlitMode(instance, DIVE_TBM_EXCLUDE_SOURCE_VALUE, 1, 0) ==
          DIVE_SUCCESS);
    CHECK(DiveBeginImageBufferAccess(instance, image, &pixels, &stride, &lines) == DIVE_SUCCESS);
    pixels[0] = pixels[1] = 1;
    pixels[stride] = pixels[stride + 1] = 2;
    CHECK(DiveEndImageBufferAccess(instance, image) == DIVE_SUCCESS);
    BYTE changed[2] = {255, 0};
    CHECK(DiveBlitImageLines(instance, image, 0, changed) == DIVE_SUCCESS);
    WinPeekMsg(hab, &message, 0, 0, 0, 0);
    ps = WinGetPS(window);
    CHECK(ps && lx_pm_raster_read(ps, 0, 0, 4, 4, actual));
    CHECK(!memcmp(actual, expected, sizeof actual));
    CHECK(WinReleasePS(ps));
    CHECK(DiveSetTransparentBlitMode(instance, DIVE_TBM_NONE, 0, 0) == DIVE_SUCCESS);
    /* Linear framebuffer addresses are top-down and remain guest-owned until close. */
    RECTL direct = {20, 20, 24, 24};
    PBYTE address = NULL;
    ULONG bank = 99, remaining = 0;
    CHECK(DiveCalcFrameBufferAddress(instance, &direct, &address, &bank, &remaining) ==
          DIVE_SUCCESS);
    CHECK(address && bank == 0 && remaining == 4);
    CHECK(DiveDeacquireFrameBuffer(instance) == DIVE_ERR_FB_NOT_ACQUIRED);
    CHECK(DiveAcquireFrameBuffer(instance, &direct) == DIVE_SUCCESS);
    CHECK(DiveAcquireFrameBuffer(instance, &direct) == DIVE_ERR_FB_ALREADY_ACQUIRED);
    CHECK(DiveSwitchBank(instance, 0) == DIVE_ERR_NOT_BANK_SWITCHED);
    address[0] = 0x33;
    address[1] = 0x22;
    address[2] = 0x11;
    CHECK(DiveDeacquireFrameBuffer(instance) == DIVE_SUCCESS);
    WinPeekMsg(hab, &message, 0, 0, 0, 0);
    ps = WinGetPS(window);
    uint32_t direct_pixel = 0;
    CHECK(ps && lx_pm_raster_read(ps, 0, 3, 1, 1, &direct_pixel) && direct_pixel == 0x112233);
    CHECK(WinReleasePS(ps));
    uint32_t clipped_expected[16];
    ps = WinGetPS(window);
    CHECK(ps && lx_pm_raster_read(ps, 0, 0, 4, 4, clipped_expected));
    CHECK(WinReleasePS(ps));
    CHECK(DiveBeginImageBufferAccess(instance, image, &pixels, &stride, &lines) == DIVE_SUCCESS);
    pixels[0] = pixels[1] = pixels[stride] = pixels[stride + 1] = 2;
    CHECK(DiveEndImageBufferAccess(instance, image) == DIVE_SUCCESS);
    RECTL clips[] = {{-3, -3, 1, 4}, {0, 0, 3, 1}};
    setup.ulNumDstRects = 2;
    setup.pVisDstRects = clips;
    CHECK(DiveSetupBlitter(instance, &setup) == DIVE_SUCCESS);
    CHECK(DiveBlitImage(instance, image, 0) == DIVE_SUCCESS);
    WinPeekMsg(hab, &message, 0, 0, 0, 0);
    for (unsigned y = 0; y < 4; y++)
        for (unsigned x = 0; x < 4; x++)
            if (x == 0 || (y == 0 && x < 3))
                clipped_expected[y * 4 + x] = 0x00ff00;
    ps = WinGetPS(window);
    CHECK(ps && lx_pm_raster_read(ps, 0, 0, 4, 4, actual));
    CHECK(!memcmp(actual, clipped_expected, sizeof actual));
    CHECK(WinReleasePS(ps));
    setup.ulNumDstRects = 0;
    setup.pVisDstRects = NULL;
    CHECK(DiveSetupBlitter(instance, &setup) == DIVE_SUCCESS);
    CHECK(DiveBeginImageBufferAccess(instance, image, &pixels, &stride, &lines) == DIVE_SUCCESS);
    pixels[0] = pixels[1] = pixels[stride] = pixels[stride + 1] = 3;
    CHECK(DiveEndImageBufferAccess(instance, image) == DIVE_SUCCESS);
    CHECK(DiveBlitImage(instance, image, 0) == DIVE_SUCCESS);
    WinPeekMsg(hab, &message, 0, 0, 0, 0);
    ps = WinGetPS(window);
    CHECK(ps && lx_pm_raster_read(ps, 0, 0, 4, 4, actual));
    CHECK(!memcmp(actual, clipped_expected, sizeof actual));
    CHECK(WinReleasePS(ps));
    setup.ulNumDstRects = DIVE_FULLY_VISIBLE;
    CHECK(DiveSetupBlitter(instance, &setup) == DIVE_SUCCESS);
    CHECK(WinDestroyWindow(window));
    CHECK(DiveBlitImage(instance, image, 0) == DIVE_ERR_INVALID_DESTINATION_RECTL);
    CHECK(DiveSetupBlitter(instance, NULL) == DIVE_SUCCESS);
    CHECK(DiveBlitImage(instance, image, 0) == DIVE_ERR_BLITTER_NOT_SETUP);
    CHECK(DiveFreeImageBuffer(instance, image) == DIVE_SUCCESS);
    CHECK(DiveFreeImageBuffer(instance, image) == DIVE_ERR_INVALID_BUFFER_NUMBER);
    BYTE association[8] = {0};
    image = 0;
    CHECK(DiveAllocImageBuffer(instance, &image, DIVE_FOURCC_LUT8, 2, 2, 4, association) ==
          DIVE_SUCCESS);
    CHECK(DiveBeginImageBufferAccess(instance, image, &pixels, &stride, &lines) == DIVE_SUCCESS);
    CHECK(pixels == association);
    CHECK(DiveEndImageBufferAccess(instance, image) == DIVE_SUCCESS);
    CHECK(DiveFreeImageBuffer(instance, image) == DIVE_SUCCESS);
    CHECK(DiveClose(instance) == DIVE_SUCCESS);
    CHECK(DiveClose(instance) == DIVE_ERR_INVALID_INSTANCE);
    ULONG offscreen = 0, src = 0, dst = 0;
    CHECK(DiveOpen(&offscreen, TRUE, NULL) == DIVE_SUCCESS);
    CHECK(DiveAcquireFrameBuffer(offscreen, &direct) == DIVE_ERR_NO_DIRECT_ACCESS);
    BYTE rgb[8] = {255, 0, 0, 0, 0, 255, 0, 0};
    BYTE bgr[8] = {0};
    CHECK(DiveAllocImageBuffer(offscreen, &src, DIVE_FOURCC_RGB4, 2, 1, 8, rgb) == DIVE_SUCCESS);
    CHECK(DiveAllocImageBuffer(offscreen, &dst, DIVE_FOURCC_BGR4, 2, 1, 8, bgr) == DIVE_SUCCESS);
    SETUP_BLITTER off = {0};
    off.ulStructLen = 28;
    off.fInvert = 1;
    off.fccSrcColorFormat = DIVE_FOURCC_RGB4;
    off.ulSrcWidth = 2;
    off.ulSrcHeight = 1;
    CHECK(DiveSetupBlitter(offscreen, &off) == DIVE_SUCCESS);
    CHECK(DiveBlitImage(offscreen, src, dst) == DIVE_SUCCESS);
    CHECK(bgr[0] == 0 && bgr[1] == 0 && bgr[2] == 255 && bgr[4] == 0 && bgr[5] == 255);
    CHECK(DiveSetTransparentBlitMode(offscreen, DIVE_TBM_EXCLUDE_SOURCE_VALUE, 0xff0000, 0) ==
          DIVE_SUCCESS);
    memset(bgr, 0x77, sizeof bgr);
    CHECK(DiveBlitImage(offscreen, src, dst) == DIVE_SUCCESS);
    CHECK(bgr[0] == 0x77 && bgr[1] == 0x77 && bgr[2] == 0x77 && bgr[5] == 255);
    ULONG yuv_image = 0;
    BYTE yuv[4] = {0, 0, 0, 255};
    CHECK(DiveAllocImageBuffer(offscreen, &yuv_image, DIVE_FOURCC_Y422, 2, 1, 4, yuv) ==
          DIVE_SUCCESS);
    off.ulStructLen = sizeof off;
    off.fccSrcColorFormat = DIVE_FOURCC_Y422;
    off.fccDstColorFormat = DIVE_FOURCC_BGR4;
    off.ulDstWidth = 2;
    off.ulDstHeight = 1;
    CHECK(DiveSetupBlitter(offscreen, &off) == DIVE_SUCCESS);
    CHECK(DiveSetTransparentBlitMode(offscreen, DIVE_TBM_EXCLUDE_SOURCE_VALUE, 0x0000ff, 0) ==
          DIVE_SUCCESS);
    memset(bgr, 0x77, sizeof bgr);
    CHECK(DiveBlitImage(offscreen, yuv_image, dst) == DIVE_SUCCESS && bgr[2] == 0x77);
    CHECK(DiveSetTransparentBlitMode(offscreen, DIVE_TBM_EXCLUDE_SOURCE_YUV_RANGE, 0x0000ff,
                                     0x0000ff) == DIVE_SUCCESS);
    CHECK(DiveBlitImage(offscreen, yuv_image, dst) == DIVE_SUCCESS && bgr[2] == 0x77);
    CHECK(DiveSetTransparentBlitMode(offscreen, DIVE_TBM_INCLUDE_SOURCE_YUV_RANGE, 0x0000ff,
                                     0x0000ff) == DIVE_SUCCESS);
    CHECK(DiveBlitImage(offscreen, yuv_image, dst) == DIVE_SUCCESS && bgr[0] == 0 && bgr[1] == 0 &&
          bgr[2] == 173);
    CHECK(DiveClose(offscreen) == DIVE_SUCCESS);
    CHECK(WinDestroyMsgQueue(queue));
    CHECK(WinTerminate(hab));
    puts("DIVE contracts passed");
    return 0;
}
