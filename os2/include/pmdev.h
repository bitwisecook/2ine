/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMDEV_H_
#define _INCL_PMDEV_H_

#include "os2/include/pmgpi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_DEV */

    /* DevQueryCaps and DevPostDeviceModes results */
    enum
    {
        DEV_ERROR = 0,
        DEV_OK = 1,
        DEV_BAD_PARAMETERS = 2,
        DEV_WARNING = 3,
        DEV_PROP_BUF_TOO_SMALL = 4,
        DEV_ITEM_BUF_TOO_SMALL = 5,
        DEV_INV_INP_JOBPROPERTIES = 6
    };

    /* DevOpenDC types */
    enum
    {
        OD_SCREEN = 0,
        OD_QUEUED = 2,
        OD_DIRECT = 5,
        OD_INFO = 6,
        OD_METAFILE = 7,
        OD_MEMORY = 8,
        OD_METAFILE_NOQUERY = 9
    };

    /* DevQueryCaps indexes */
    enum
    {
        CAPS_FAMILY = 0,
        CAPS_IO_CAPS = 1,
        CAPS_TECHNOLOGY = 2,
        CAPS_DRIVER_VERSION = 3,
        CAPS_WIDTH = 4,
        CAPS_HEIGHT = 5,
        CAPS_WIDTH_IN_CHARS = 6,
        CAPS_HEIGHT_IN_CHARS = 7,
        CAPS_HORIZONTAL_RESOLUTION = 8,
        CAPS_VERTICAL_RESOLUTION = 9,
        CAPS_CHAR_WIDTH = 10,
        CAPS_CHAR_HEIGHT = 11,
        CAPS_SMALL_CHAR_WIDTH = 12,
        CAPS_SMALL_CHAR_HEIGHT = 13,
        CAPS_COLORS = 14,
        CAPS_COLOR_PLANES = 15,
        CAPS_COLOR_BITCOUNT = 16,
        CAPS_COLOR_TABLE_SUPPORT = 17,
        CAPS_MOUSE_BUTTONS = 18,
        CAPS_FOREGROUND_MIX_SUPPORT = 19,
        CAPS_BACKGROUND_MIX_SUPPORT = 20,
        CAPS_DEVICE_WINDOWING = 31,
        CAPS_ADDITIONAL_GRAPHICS = 32,
        CAPS_VIO_LOADABLE_FONTS = 21,
        CAPS_WINDOW_BYTE_ALIGNMENT = 22,
        CAPS_BITMAP_FORMATS = 23,
        CAPS_RASTER_CAPS = 24,
        CAPS_MARKER_HEIGHT = 25,
        CAPS_MARKER_WIDTH = 26,
        CAPS_DEVICE_FONTS = 27,
        CAPS_GRAPHICS_SUBSET = 28,
        CAPS_GRAPHICS_VERSION = 29,
        CAPS_GRAPHICS_VECTOR_SUBSET = 30,
        CAPS_PHYS_COLORS = 33,
        CAPS_COLOR_INDEX = 34,
        CAPS_GRAPHICS_CHAR_WIDTH = 35,
        CAPS_GRAPHICS_CHAR_HEIGHT = 36,
        CAPS_HORIZONTAL_FONT_RES = 37,
        CAPS_VERTICAL_FONT_RES = 38,
        CAPS_DEVICE_FONT_SIM = 39,
        CAPS_LINEWIDTH_THICK = 40,
        CAPS_DEVICE_POLYSET_POINTS = 41
    };

    /* CAPS_TECHNOLOGY values */
    enum
    {
        CAPS_TECH_UNKNOWN = 0,
        CAPS_TECH_VECTOR_PLOTTER = 1,
        CAPS_TECH_RASTER_DISPLAY = 2,
        CAPS_TECH_RASTER_PRINTER = 3,
        CAPS_TECH_RASTER_CAMERA = 4,
        CAPS_TECH_POSTSCRIPT = 5
    };

    /* Other DevQueryCaps values */
    enum
    {
        CAPS_IO_DUMMY = 1,
        CAPS_IO_SUPPORTS_OP = 2,
        CAPS_IO_SUPPORTS_IP = 3,
        CAPS_IO_SUPPORTS_IO = 4,
        CAPS_COLTABL_RGB_8 = 1,
        CAPS_COLTABL_RGB_8_PLUS = 2,
        CAPS_COLTABL_TRUE_MIX = 4,
        CAPS_COLTABL_REALIZE = 8,
        CAPS_FM_OR = 1,
        CAPS_FM_OVERPAINT = 2,
        CAPS_FM_XOR = 8,
        CAPS_FM_LEAVEALONE = 16,
        CAPS_FM_AND = 32,
        CAPS_FM_GENERAL_BOOLEAN = 64,
        CAPS_DEV_WINDOWING_SUPPORT = 1,
        CAPS_BYTE_ALIGN_REQUIRED = 0,
        CAPS_BYTE_ALIGN_RECOMMENDED = 1,
        CAPS_BYTE_ALIGN_NOT_REQUIRED = 2
    };

    /* DevPostDeviceModes options and DevQueryHardcopyCaps results */
    enum
    {
        DPDM_ERROR = -1,
        DPDM_NONE = 0,
        DPDM_POSTJOBPROP = 0,
        DPDM_CHANGEPROP = 1,
        DPDM_QUERYJOBPROP = 2,
        DQHC_ERROR = -1,
        HCAPS_CURRENT = 1,
        HCAPS_SELECTABLE = 2
    };

    /* DevEscape codes and results */
    enum
    {
        DEVESC_ERROR = -1,
        DEVESC_NOTIMPLEMENTED = 0,
        DEVESC_QUERYESCSUPPORT = 0,
        DEVESC_GETSCALINGFACTOR = 1,
        DEVESC_QUERYVIOCELLSIZES = 2,
        DEVESC_GETCP = 8000,
        DEVESC_SETABORTPROC = 8001,
        DEVESC_QUERYPDL = 8002,
        DEVESC_STARTDOC = 8150,
        DEVESC_ENDDOC = 8151,
        DEVESC_NEXTBAND = 8152,
        DEVESC_ABORTDOC = 8153,
        DEVESC_GETJOBID = 8160,
        DEVESC_QUERY_RASTER = 8161,
        DEVESC_QUERYSIZE = 8162,
        DEVESC_QUERYJOBPROPERTIES = 8163,
        DEVESC_SETJOBPROPERTIES = 8164,
        DEVESC_DEFAULTJOBPROPERTIES = 8165,
        DEVESC_CHANGEOUTPUTPORT = 8166,
        DEVESC_NEWFRAME = 16300,
        DEVESC_DRAFTMODE = 16301,
        DEVESC_FLUSHOUTPUT = 16302,
        DEVESC_RAWDATA = 16303
    };

    OS2EXPORT BOOL OS2API DevQueryCaps(HDC dc, LONG start, LONG count, PLONG output);

    OS2EXPORT LONG OS2API DevEscape(HDC dc, LONG code, LONG input_count, PVOID input,
                                    PLONG output_count, PVOID output);

    OS2EXPORT LONG OS2API DevPostDeviceModes(HAB hab, PVOID data, PSZ driver, PSZ device, PSZ name,
                                             ULONG options);

    OS2EXPORT BOOL OS2API DevQueryDeviceNames(HAB hab, PSZ driver, PLONG devices, PVOID names,
                                              PVOID descriptions, PLONG types, PVOID data_types);

    OS2EXPORT LONG OS2API DevQueryHardcopyCaps(HDC dc, LONG first, LONG count, PVOID output);

    OS2EXPORT HDC OS2API DevOpenDC(HAB hab, LONG type, PSZ token, LONG count, PVOID data,
                                   HDC compatible);

    OS2EXPORT ULONG OS2API DevCloseDC(HDC dc);

#ifdef __cplusplus
}
#endif

#endif
