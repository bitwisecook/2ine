/* Public domain. No rights reserved. Wholly AI-authored. */
/* LUT8 software conversion, cross-thread presentation and buffer ownership. */
#include "os2libs/dive.h"
#include "os2libs/pmwin/graphics.h"
#include "runtime/runtime.h"
#include "hal/host_thread.h"
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
    ULONG formats[2];
    DIVE_CAPS caps = {0};
    caps.ulStructLen = sizeof caps;
    caps.ulFormatLength = sizeof formats;
    caps.pFormatData = formats;
    CHECK(DiveQueryCaps(&caps, 0) == DIVE_SUCCESS);
    CHECK(caps.ulDepth == 32 && !caps.fScreenDirect && caps.ulInputFormats == 1 &&
          caps.ulOutputFormats == 1 && formats[0] == DIVE_FOURCC_LUT8 &&
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
    CHECK(WinDestroyMsgQueue(queue));
    CHECK(WinTerminate(hab));
    puts("DIVE contracts passed");
    return 0;
}
