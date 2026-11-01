/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_DIVE_H
#define TWOINE_DIVE_H
#include "os2types.h"
#include "pmwin.h"
#define DIVE_SUCCESS 0
#define DIVE_ERR_INVALID_INSTANCE 0x1000
#define DIVE_ERR_SOURCE_FORMAT 0x1001
#define DIVE_ERR_DESTINATION_FORMAT 0x1002
#define DIVE_ERR_BLITTER_NOT_SETUP 0x1003
#define DIVE_ERR_INSUFFICIENT_LENGTH 0x1004
#define DIVE_ERR_TOO_MANY_INSTANCES 0x1005
#define DIVE_ERR_NO_DIRECT_ACCESS 0x1006
#define DIVE_ERR_INVALID_PALETTE 0x100e
#define DIVE_ERR_INVALID_DESTINATION_RECTL 0x100f
#define DIVE_ERR_INVALID_BUFFER_NUMBER 0x1010
#define DIVE_ERR_BUFFER_ALREADY_ACCESSED 0x1012
#define DIVE_ERR_BUFFER_NOT_ACCESSED 0x1013
#define DIVE_ERR_TOO_MANY_BUFFERS 0x1014
#define DIVE_ERR_ALLOCATION_ERROR 0x1015
#define DIVE_ERR_INVALID_LINESIZE 0x1016
#define DIVE_ERR_OUT_OF_RANGE 0x101b
#define DIVE_FOURCC_LUT8 0x3854554cUL
#define DIVE_FOURCC_BGR4 0x34524742UL
#define DIVE_FULLY_VISIBLE 0xffffffffUL
#define DIVE_BUFFER_SCREEN 0
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
OS2EXPORT ULONG OS2API DiveQueryCaps(PDIVE_CAPS caps, ULONG plane) OS2APIINFO(1);
OS2EXPORT ULONG OS2API DiveOpen(PULONG instance, ULONG non_screen, PVOID framebuffer) OS2APIINFO(2);
OS2EXPORT ULONG OS2API DiveClose(ULONG instance) OS2APIINFO(3);
OS2EXPORT ULONG OS2API DiveSetupBlitter(ULONG instance, PSETUP_BLITTER setup) OS2APIINFO(4);
OS2EXPORT ULONG OS2API DiveBlitImage(ULONG instance, ULONG source, ULONG destination) OS2APIINFO(5);
OS2EXPORT ULONG OS2API DiveSetDestinationPalette(ULONG instance, ULONG start, ULONG count,
                                                 PBYTE entries) OS2APIINFO(9);
OS2EXPORT ULONG OS2API DiveSetSourcePalette(ULONG instance, ULONG start, ULONG count, PBYTE entries)
    OS2APIINFO(10);
OS2EXPORT ULONG OS2API DiveAllocImageBuffer(ULONG instance, PULONG number, ULONG format,
                                            ULONG width, ULONG height, ULONG stride, PBYTE pixels)
    OS2APIINFO(12);
OS2EXPORT ULONG OS2API DiveFreeImageBuffer(ULONG instance, ULONG number) OS2APIINFO(13);
OS2EXPORT ULONG OS2API DiveBeginImageBufferAccess(ULONG instance, ULONG number, PVOID pointer,
                                                  PULONG stride, PULONG lines) OS2APIINFO(14);
OS2EXPORT ULONG OS2API DiveEndImageBufferAccess(ULONG instance, ULONG number) OS2APIINFO(15);
OS2EXPORT VOID OS2API DiveFullScreenInit(PVOID aperture, PVOID framebuffer_info) OS2APIINFO(16);
OS2EXPORT VOID OS2API DiveFullScreenTerm(void) OS2APIINFO(17);
#endif
