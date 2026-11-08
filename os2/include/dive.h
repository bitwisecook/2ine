/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_DIVE_H
#define TWOINE_DIVE_H
#include "os2/include/os2types.h"
#include "os2/include/pmwin.h"
#define DIVE_SUCCESS 0
#define DIVE_ERR_INVALID_INSTANCE 0x1000
#define DIVE_ERR_SOURCE_FORMAT 0x1001
#define DIVE_ERR_DESTINATION_FORMAT 0x1002
#define DIVE_ERR_BLITTER_NOT_SETUP 0x1003
#define DIVE_ERR_INSUFFICIENT_LENGTH 0x1004
#define DIVE_ERR_TOO_MANY_INSTANCES 0x1005
#define DIVE_ERR_NO_DIRECT_ACCESS 0x1006
#define DIVE_ERR_NOT_BANK_SWITCHED 0x1007
#define DIVE_ERR_INVALID_BANK_NUMBER 0x1008
#define DIVE_ERR_FB_NOT_ACQUIRED 0x1009
#define DIVE_ERR_FB_ALREADY_ACQUIRED 0x100a
#define DIVE_ERR_ACQUIRE_FAILED 0x100b
#define DIVE_ERR_INVALID_PALETTE 0x100e
#define DIVE_ERR_INVALID_DESTINATION_RECTL 0x100f
#define DIVE_ERR_INVALID_BUFFER_NUMBER 0x1010
#define DIVE_ERR_BUFFER_ALREADY_ACCESSED 0x1012
#define DIVE_ERR_BUFFER_NOT_ACCESSED 0x1013
#define DIVE_ERR_TOO_MANY_BUFFERS 0x1014
#define DIVE_ERR_ALLOCATION_ERROR 0x1015
#define DIVE_ERR_INVALID_LINESIZE 0x1016
#define DIVE_ERR_INVALID_CONVERSION 0x1018
#define DIVE_ERR_OUT_OF_RANGE 0x101b
#define DIVE_FOURCC_LUT8 0x3854554cUL
#define DIVE_FOURCC_BGR4 0x34524742UL
#define DIVE_FOURCC_R565 0x35363552UL
#define DIVE_FOURCC_R555 0x35353552UL
#define DIVE_FOURCC_R664 0x34363652UL
#define DIVE_FOURCC_RGB3 0x33424752UL
#define DIVE_FOURCC_BGR3 0x33524742UL
#define DIVE_FOURCC_RGB4 0x34424752UL
#define DIVE_FOURCC_LT12 0x3231544cUL
#define DIVE_FOURCC_MONO 0x4f4e4f4dUL
#define DIVE_FOURCC_VGA 0x20414756UL
#define DIVE_FOURCC_Y888 0x38383859UL
#define DIVE_FOURCC_Y2X2 0x32583259UL
#define DIVE_FOURCC_Y4X4 0x34583459UL
#define DIVE_FOURCC_YUV9 0x39565559UL
#define DIVE_FOURCC_Y644 0x34343659UL
#define DIVE_FOURCC_Y422 0x32323459UL
#define DIVE_FOURCC_Y42B 0x42323459UL
#define DIVE_FOURCC_Y42D 0x44323459UL
#define DIVE_FOURCC_Y411 0x31313459UL
#define DIVE_TBM_NONE 0
#define DIVE_TBM_EXCLUDE_SOURCE_VALUE 1
#define DIVE_TBM_EXCLUDE_SOURCE_RGB_RANGE 2
#define DIVE_TBM_INCLUDE_SOURCE_RGB_RANGE 3
#define DIVE_TBM_EXCLUDE_SOURCE_YUV_RANGE 4
#define DIVE_TBM_INCLUDE_SOURCE_YUV_RANGE 5
#define DIVE_FULLY_VISIBLE 0xffffffffUL
#define DIVE_BUFFER_SCREEN 0
#define DIVE_BUFFER_GRAPHICS_PLANE 1
#define DIVE_BUFFER_ALTERNATE_PLANE 2
#define DIVE_MAX_INSTANCES 64

typedef struct
{
    ULONG ulStructLen, ulPlaneCount;
    BOOL fScreenDirect, fBankSwitched;
    ULONG ulDepth, ulHorizontalResolution, ulVerticalResolution, ulScanLineBytes;
    ULONG fccColorEncoding, ulApertureSize, ulInputFormats, ulOutputFormats, ulFormatLength;
    PVOID pFormatData;
} DIVE_CAPS;

typedef DIVE_CAPS *PDIVE_CAPS;

typedef struct
{
    ULONG ulStructLen, fInvert, fccSrcColorFormat, ulSrcWidth, ulSrcHeight, ulSrcPosX, ulSrcPosY;
    ULONG ulDitherType, fccDstColorFormat, ulDstWidth, ulDstHeight;
    LONG lDstPosX, lDstPosY, lScreenPosX, lScreenPosY;
    ULONG ulNumDstRects;
    PRECTL pVisDstRects;
} SETUP_BLITTER;

typedef SETUP_BLITTER *PSETUP_BLITTER;
OS2EXPORT ULONG OS2API DiveQueryCaps(PDIVE_CAPS caps, ULONG plane);
OS2EXPORT ULONG OS2API DiveOpen(PULONG instance, ULONG non_screen, PVOID framebuffer);
OS2EXPORT ULONG OS2API DiveClose(ULONG instance);
OS2EXPORT ULONG OS2API DiveSetupBlitter(ULONG instance, PSETUP_BLITTER setup);
OS2EXPORT ULONG OS2API DiveBlitImage(ULONG instance, ULONG source, ULONG destination);
OS2EXPORT ULONG OS2API DiveAcquireFrameBuffer(ULONG instance, PRECTL rect);
OS2EXPORT ULONG OS2API DiveSwitchBank(ULONG instance, ULONG bank);
OS2EXPORT ULONG OS2API DiveDeacquireFrameBuffer(ULONG instance);
OS2EXPORT ULONG OS2API DiveSetDestinationPalette(ULONG instance, ULONG start, ULONG count,
                                                 PBYTE entries);
OS2EXPORT ULONG OS2API DiveSetSourcePalette(ULONG instance, ULONG start, ULONG count,
                                            PBYTE entries);
OS2EXPORT ULONG OS2API DiveCalcFrameBufferAddress(ULONG instance, PRECTL rect, PVOID address,
                                                  PULONG bank, PULONG lines);
OS2EXPORT ULONG OS2API DiveAllocImageBuffer(ULONG instance, PULONG number, ULONG format,
                                            ULONG width, ULONG height, ULONG stride, PBYTE pixels);
OS2EXPORT ULONG OS2API DiveFreeImageBuffer(ULONG instance, ULONG number);
OS2EXPORT ULONG OS2API DiveBeginImageBufferAccess(ULONG instance, ULONG number, PVOID pointer,
                                                  PULONG stride, PULONG lines);
OS2EXPORT ULONG OS2API DiveEndImageBufferAccess(ULONG instance, ULONG number);
OS2EXPORT VOID OS2API DiveFullScreenInit(PVOID aperture, PVOID framebuffer_info);
OS2EXPORT VOID OS2API DiveFullScreenTerm(void);
OS2EXPORT ULONG OS2API DiveSetTransparentBlitMode(ULONG instance, ULONG mode, ULONG first,
                                                  ULONG last);
OS2EXPORT ULONG OS2API DiveBlitImageLines(ULONG instance, ULONG source, ULONG destination,
                                          PBYTE lines);
#endif
