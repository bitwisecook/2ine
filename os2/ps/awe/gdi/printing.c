/* Public domain. No rights reserved. Wholly AI-authored. */
/* Printing: document and page brackets with the abort procedure, printer DC services
   (ResetDC, DeviceCapabilities, Escape) and job defaults from DEVMODE. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmsplp.h"
#include "host/print.h"
#include "os2/ps/awe/win32.h"

/* The abort procedure runs guest code at each page and document boundary, before any device
   output. It may itself end the job or delete the DC, so state is looked up again afterwards. */
enum
{
    SP_ERROR = -1,
    SP_APPABORT = -2
};

enum
{
    /* Windows 95 extended DOCINFO with lpszDatatype and then fwType. */
    DOCINFO_SIZE_WITH_DATATYPE = 16,
    DOCINFO_SIZE_WITH_TYPE = 20,
    PRINT_TITLE_LIMIT = 4096,
    EXTTEXTOUT_STRING_LIMIT = 65536,
    /* DEVMODE up to and including dmCollate: every field the queues act on. */
    DEVMODE_CHECKED_SIZE = 70,
    /* sizeof(DEVMODE): 126 bytes of fields plus tail padding. */
    DEVMODE_STRUCT_SIZE = 128,
    /* Both queues render at 300 dpi; a page is measured in device pixels. */
    PRINTER_DPI = 300,
    LETTER_WIDTH_PIXELS = 2550,
    LETTER_HEIGHT_PIXELS = 3300,
    LEGAL_HEIGHT_PIXELS = 4200,
    A4_WIDTH_PIXELS = 2480,
    A4_HEIGHT_PIXELS = 3508,
    /* DeviceCapabilities paper sizes are in tenths of a millimetre. */
    LETTER_WIDTH_TENTH_MM = 2159,
    LETTER_HEIGHT_TENTH_MM = 2794,
    LEGAL_HEIGHT_TENTH_MM = 3556,
    A4_WIDTH_TENTH_MM = 2100,
    A4_HEIGHT_TENTH_MM = 2970,
    PAPER_COUNT = 3,
    /* DC_BINNAMES and DC_PAPERNAMES fill fixed-width name slots. */
    BIN_NAME_LENGTH = 24,
    PAPER_NAME_LENGTH = 64,
    /* Landscape is portrait turned through 90 degrees. */
    LANDSCAPE_ROTATION_DEGREES = 90,
    /* The Win32 default miter limit. */
    DEFAULT_MITER_LIMIT = 10
};

/* lx_pm_printer_kind and the queue index: the PDF queue prints colour, the PCL queue mono. */
enum
{
    PRINTER_INDEX_PDF = 0,
    PRINTER_KIND_PDF = 1,
    PRINTER_KIND_PCL = 2,
    PRINTER_PDF_BITS_PER_PIXEL = 24,
    PRINTER_PCL_BITS_PER_PIXEL = 1
};

/* Win16 EXTTEXTOUT escape input: POINT16 origin, then text and advance pointers. */
enum
{
    EXTTEXTOUT_X = 0,
    EXTTEXTOUT_Y = 2,
    EXTTEXTOUT_TEXT = 4,
    EXTTEXTOUT_ADVANCES = 8,
    EXTTEXTOUT_SIZE = 12
};

static LONG print_boundary(ULONG handle, Open32DC **context)
{
    BOOL proceed = open32AbortCheck(handle);
    Open32DC *dc = open32FindDC(handle);
    *context = dc;
    if (!dc)
        return SP_ERROR;
    if (proceed)
        return 1;
    if (dc->print_active)
        DevEscape(GpiQueryDevice(handle), DEVESC_ABORTDOC, 0, NULL, NULL, NULL);
    dc->print_active = dc->print_page = FALSE;
    return SP_APPABORT;
}

LONG StartDoc(ULONG handle, PVOID info)
{
    Open32DC *dc = open32FindDC(handle);
    ULONG size = info ? lx_guest_load_u32(info) : 0;
    if (!dc || dc->kind != OPEN32_DC_PRINTER || dc->print_active ||
        (size != WIN32_DOCINFO_SIZE && size != DOCINFO_SIZE_WITH_DATATYPE &&
         size != DOCINFO_SIZE_WITH_TYPE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    uint32 name = lx_guest_load_u32((uint8 *)info + WIN32_DOCINFO_DOCUMENT_NAME),
           output = lx_guest_load_u32((uint8 *)info + WIN32_DOCINFO_OUTPUT);
    char *title = name ? lx_execution_copy_string(GProcessState.execution, name, PRINT_TITLE_LIMIT)
                       : strdup("Print Job"),
         *destination =
             output ? lx_execution_copy_string(GProcessState.execution, output, PRINT_TITLE_LIMIT)
                    : NULL;
    if (!title || (output && !destination) ||
        (destination && *destination && strcasecmp(destination, "FILE:")))
    {
        free(title);
        free(destination);
        open32SetError(ERROR_NOT_SUPPORTED);
        return -1;
    }
    LONG result = print_boundary(handle, &dc);
    if (result > 0)
    {
        if (dc->print_active)
            result = SP_ERROR;
        else
            result = DevEscape(GpiQueryDevice(handle), DEVESC_STARTDOC, (LONG)strlen(title) + 1,
                               title, NULL, NULL);
    }
    free(title);
    free(destination);
    if (result > 0)
    {
        dc->print_active = TRUE;
        dc->print_page = FALSE;
    }
    return result;
}

LONG StartPage(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active || dc->print_page)
        return SP_ERROR;
    LONG result = print_boundary(handle, &dc);
    if (result <= 0)
        return result;
    if (!dc->print_active || dc->print_page)
        return SP_ERROR;
    dc->print_page = TRUE;
    return 1;
}

LONG EndPage(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active || !dc->print_page)
        return SP_ERROR;
    LONG result = print_boundary(handle, &dc);
    if (result <= 0)
        return result;
    if (!dc->print_active || !dc->print_page)
        return SP_ERROR;
    result = DevEscape(GpiQueryDevice(handle), DEVESC_NEWFRAME, 0, NULL, NULL, NULL);
    if (result > 0)
        dc->print_page = FALSE;
    return result;
}

LONG EndDoc(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active)
        return SP_ERROR;
    LONG result = print_boundary(handle, &dc);
    if (result <= 0)
        return result;
    if (!dc->print_active)
        return SP_ERROR;
    result = DevEscape(GpiQueryDevice(handle), DEVESC_ENDDOC, 0, NULL, NULL, NULL);
    dc->print_active = dc->print_page = FALSE;
    return result;
}

LONG AbortDoc(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active)
        return SP_ERROR;
    LONG result = DevEscape(GpiQueryDevice(handle), DEVESC_ABORTDOC, 0, NULL, NULL, NULL);
    dc->print_active = dc->print_page = FALSE;
    return result;
}

BOOL open32PrinterDefaults(unsigned kind, const char *output, PVOID mode)
{
    if (output && *output && strcasecmp(output, "FILE:"))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!mode)
        return TRUE;
    uint8 settings[DEVMODE_CHECKED_SIZE];
    if ((uintptr_t)mode > UINT32_MAX ||
        !lx_execution_read(GProcessState.execution, (uint32)(uintptr_t)mode, settings,
                           sizeof settings) ||
        lx_guest_load_u16(settings + WIN32_DEVMODE_SIZE_FIELD) < sizeof settings ||
        !memchr(settings, 0, WIN32_CCHDEVICENAME))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (kind < PRINTER_KIND_PDF || kind > PRINTER_KIND_PCL ||
        strcasecmp((const char *)settings, lx_printer_queues[kind - 1]))
    {
        open32SetError(ERROR_INVALID_PRINTER_NAME);
        return FALSE;
    }
    ULONG fields = lx_guest_load_u32(settings + WIN32_DEVMODE_FIELDS),
          paper = lx_guest_load_u16(settings + WIN32_DEVMODE_PAPER_SIZE),
          orientation = lx_guest_load_u16(settings + WIN32_DEVMODE_ORIENTATION);
    SHORT quality = (SHORT)lx_guest_load_u16(settings + WIN32_DEVMODE_PRINT_QUALITY);
    const unsigned supported = WIN32_DM_ORIENTATION | WIN32_DM_PAPERSIZE | WIN32_DM_COPIES |
                               WIN32_DM_PRINTQUALITY | WIN32_DM_COLOR | WIN32_DM_DUPLEX |
                               WIN32_DM_YRESOLUTION;
    if ((fields & ~supported) ||
        ((fields & WIN32_DM_ORIENTATION) && orientation != WIN32_DMORIENT_PORTRAIT &&
         orientation != WIN32_DMORIENT_LANDSCAPE) ||
        ((fields & WIN32_DM_PAPERSIZE) && paper != WIN32_DMPAPER_LETTER &&
         paper != WIN32_DMPAPER_LEGAL && paper != WIN32_DMPAPER_A4) ||
        ((fields & WIN32_DM_COPIES) && !lx_guest_load_u16(settings + WIN32_DEVMODE_COPIES)) ||
        ((fields & WIN32_DM_PRINTQUALITY) && quality != PRINTER_DPI &&
         (quality < WIN32_DMRES_HIGH || quality > WIN32_DMRES_DRAFT)) ||
        ((fields & WIN32_DM_DUPLEX) &&
         lx_guest_load_u16(settings + WIN32_DEVMODE_DUPLEX) > WIN32_DMDUP_HORIZONTAL) ||
        ((fields & WIN32_DM_COLOR) &&
         lx_guest_load_u16(settings + WIN32_DEVMODE_COLOR) !=
             (kind == PRINTER_KIND_PDF ? WIN32_DMCOLOR_COLOR : WIN32_DMCOLOR_MONOCHROME)) ||
        ((fields & WIN32_DM_YRESOLUTION) &&
         lx_guest_load_u16(settings + WIN32_DEVMODE_Y_RESOLUTION) != PRINTER_DPI))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return TRUE;
}

/* The virtual queues print Letter, Legal and A4 at 300 dpi in either orientation. */
BOOL open32PrinterApply(ULONG handle, PVOID mode)
{
    uint8 settings[DEVMODE_CHECKED_SIZE];
    if (!mode)
        return TRUE;
    if (!lx_execution_read(GProcessState.execution, (uint32)(uintptr_t)mode, settings,
                           sizeof settings))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG fields = lx_guest_load_u32(settings + WIN32_DEVMODE_FIELDS);
    ULONG paper = fields & WIN32_DM_PAPERSIZE
                      ? lx_guest_load_u16(settings + WIN32_DEVMODE_PAPER_SIZE)
                      : WIN32_DMPAPER_A4;
    unsigned width = paper == WIN32_DMPAPER_A4 ? A4_WIDTH_PIXELS : LETTER_WIDTH_PIXELS,
             height = paper == WIN32_DMPAPER_A4      ? A4_HEIGHT_PIXELS
                      : paper == WIN32_DMPAPER_LEGAL ? LEGAL_HEIGHT_PIXELS
                                                     : LETTER_HEIGHT_PIXELS;
    BOOL landscape =
        (fields & WIN32_DM_ORIENTATION) &&
        lx_guest_load_u16(settings + WIN32_DEVMODE_ORIENTATION) == WIN32_DMORIENT_LANDSCAPE;
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !lx_pm_printer_page(GpiQueryDevice(handle), landscape ? height : width,
                                   landscape ? width : height))
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (fields & WIN32_DM_COPIES)
        dc->print_copies = lx_guest_load_u16(settings + WIN32_DEVMODE_COPIES);
    if (fields & WIN32_DM_PRINTQUALITY)
        dc->print_draft =
            (SHORT)lx_guest_load_u16(settings + WIN32_DEVMODE_PRINT_QUALITY) == WIN32_DMRES_DRAFT;
    if (fields & WIN32_DM_DUPLEX)
        dc->print_duplex = lx_guest_load_u16(settings + WIN32_DEVMODE_DUPLEX);
    return lx_pm_printer_options(GpiQueryDevice(handle), dc->print_copies, dc->print_duplex,
                                 dc->print_draft);
}

LONG SetAbortProc(ULONG h, ULONG procedure)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !procedure || !open32ValidProcedure(procedure))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    dc->abort_proc = procedure;
    return 1;
}

BOOL open32AbortCheck(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        return FALSE;
    if (!dc->abort_proc)
        return TRUE;
    return lx_execution_callback32_4(GProcessState.execution, dc->abort_proc, h, 0, 0, 0) != 0;
}

BOOL ResetDC(ULONG h, PVOID mode)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || (dc->kind != OPEN32_DC_PRINTER && dc->kind != OPEN32_DC_PRINTER_INFORMATION) ||
        dc->print_page || !mode)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    unsigned kind = lx_pm_printer_kind(GpiQueryDevice(h));
    if (!open32PrinterDefaults(kind, NULL, mode) || !open32PrinterApply(h, mode))
        return 0;
    /* The original returns the HDC itself despite the SDK's BOOL declaration. */
    return h;
}

ULONG DeviceCapabilities(PSZ device, PSZ port, ULONG capability, PVOID output, PVOID mode)
{
    (void)port;
    (void)mode;
    int printer = lx_printer_index(device);
    if (printer < 0)
    {
        open32SetError(ERROR_INVALID_PRINTER_NAME);
        return UINT32_MAX;
    }
    static const USHORT papers[PAPER_COUNT] = {WIN32_DMPAPER_LETTER, WIN32_DMPAPER_LEGAL,
                                               WIN32_DMPAPER_A4};
    static const ULONG sizes[PAPER_COUNT][2] = {{LETTER_WIDTH_TENTH_MM, LETTER_HEIGHT_TENTH_MM},
                                                {LETTER_WIDTH_TENTH_MM, LEGAL_HEIGHT_TENTH_MM},
                                                {A4_WIDTH_TENTH_MM, A4_HEIGHT_TENTH_MM}};
    switch (capability)
    {
        case WIN32_DC_FIELDS:
            return WIN32_DM_ORIENTATION | WIN32_DM_PAPERSIZE | WIN32_DM_PRINTQUALITY |
                   WIN32_DM_COLOR | WIN32_DM_YRESOLUTION;
        case WIN32_DC_PAPERS:
            for (unsigned i = 0; output && i < PAPER_COUNT; i++)
                lx_guest_store_u16((uint8 *)output + i * 2, papers[i]);
            return PAPER_COUNT;
        case WIN32_DC_PAPERSIZE:
            for (unsigned i = 0; output && i < PAPER_COUNT; i++)
            {
                lx_guest_store_u32((uint8 *)output + i * 8, sizes[i][0]);
                lx_guest_store_u32((uint8 *)output + i * 8 + 4, sizes[i][1]);
            }
            return PAPER_COUNT;
        case WIN32_DC_MINEXTENT:
            return A4_WIDTH_TENTH_MM | ((unsigned)LETTER_HEIGHT_TENTH_MM << 16);
        case WIN32_DC_MAXEXTENT:
            return LETTER_WIDTH_TENTH_MM | ((unsigned)LEGAL_HEIGHT_TENTH_MM << 16);
        case WIN32_DC_BINS:
            if (output)
                lx_guest_store_u16(output, WIN32_DMBIN_AUTO);
            return 1;
        case WIN32_DC_DUPLEX:
            return 0;
        case WIN32_DC_SIZE:
            return DEVMODE_STRUCT_SIZE;
        case WIN32_DC_EXTRA:
            return 0;
        case WIN32_DC_VERSION:
            return WIN32_DM_SPECVERSION;
        case WIN32_DC_DRIVER:
            return 1;
        case WIN32_DC_BINNAMES:
            if (output)
            {
                memset(output, 0, BIN_NAME_LENGTH);
                memcpy(output, "Auto", 5);
            }
            return 1;
        case WIN32_DC_ENUMRESOLUTIONS:
            if (output)
            {
                lx_guest_store_u32(output, PRINTER_DPI);
                lx_guest_store_u32((uint8 *)output + 4, PRINTER_DPI);
            }
            return 1;
        case WIN32_DC_FILEDEPENDENCIES:
            return 0;
        case WIN32_DC_TRUETYPE:
            return 0;
        case WIN32_DC_PAPERNAMES:
            if (output)
            {
                memset(output, 0, PAPER_COUNT * PAPER_NAME_LENGTH);
                memcpy(output, "Letter", 7);
                memcpy((uint8 *)output + PAPER_NAME_LENGTH, "Legal", 6);
                memcpy((uint8 *)output + 2 * PAPER_NAME_LENGTH, "A4", 3);
            }
            return PAPER_COUNT;
        case WIN32_DC_ORIENTATION:
            return LANDSCAPE_ROTATION_DEGREES;
        case WIN32_DC_COPIES:
            return 1;
        case WIN32_DC_BINADJUST:
            return 0;
        case WIN32_DC_EMF_COMPLIANT:
            return 0;
        case WIN32_DC_COLLATE:
            return printer == PRINTER_INDEX_PDF;
        case WIN32_DC_COLORDEVICE:
            return printer == PRINTER_INDEX_PDF ? PRINTER_PDF_BITS_PER_PIXEL
                                                : PRINTER_PCL_BITS_PER_PIXEL;
        default:
            return UINT32_MAX;
    }
}

LONG Escape(ULONG h, LONG code, LONG count, PVOID input, PVOID output)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || count < 0 || (count && !input))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (code == WIN32_QUERYESCSUPPORT)
    {
        if (count < 2)
            return 0;
        ULONG query = count >= 4 ? lx_guest_load_u32(input) : lx_guest_load_u16(input);
        return query == WIN32_NEWFRAME || query == WIN32_ABORTDOC ||
               query == WIN32_QUERYESCSUPPORT || query == WIN32_SETABORTPROC ||
               query == WIN32_STARTDOC || query == WIN32_ENDDOC || query == WIN32_GETPHYSPAGESIZE ||
               query == WIN32_GETPRINTINGOFFSET || query == WIN32_GETSCALINGFACTOR ||
               query == WIN32_DRAFTMODE || query == WIN32_SETCOPYCOUNT ||
               query == WIN32_PASSTHROUGH || query == WIN32_GETTECHNOLOGY ||
               query == WIN32_SETMITERLIMIT || query == WIN32_ENABLEDUPLEX ||
               query == WIN32_GETSETPAPERBINS || query == WIN32_GETSETPRINTORIENT ||
               query == WIN32_ENUMPAPERBINS || query == WIN32_POSTSCRIPT_DATA ||
               query == WIN32_GETEXTENTTABLE || query == WIN32_EXTTEXTOUT ||
               query == WIN32_BEGIN_PATH || query == WIN32_CLIP_TO_PATH ||
               query == WIN32_END_PATH || query == WIN32_SET_ARC_DIRECTION;
    }
    if (code == WIN32_MFCOMMENT)
        return 0;
    if (code == WIN32_DRAFTMODE || code == WIN32_SETCOPYCOUNT || code == WIN32_ENABLEDUPLEX ||
        code == WIN32_GETSETPRINTORIENT)
    {
        /* Job properties: a printer DC and a word, dword or 20-byte input. */
        if (!input)
            return 0;
        if (count != 2 && count != 4 && count != 20)
            return -1;
        if (dc->kind != OPEN32_DC_PRINTER && dc->kind != OPEN32_DC_PRINTER_INFORMATION)
            return 0;
        LONG value = count == 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        HDC device = GpiQueryDevice(h);
        if (code == WIN32_DRAFTMODE)
            dc->print_draft = value != 0;
        else if (code == WIN32_SETCOPYCOUNT)
            dc->print_copies = value > 0 ? (ULONG)value : 1;
        else if (code == WIN32_ENABLEDUPLEX)
            dc->print_duplex = value == 0   ? WIN32_DMDUP_SIMPLEX
                               : value == 1 ? WIN32_DMDUP_VERTICAL
                                            : WIN32_DMDUP_HORIZONTAL;
        else
        {
            unsigned width = 0, height = 0;
            BOOL landscape = lx_guest_load_u32(input) != 0;
            if (dc->print_page || !lx_pm_printer_size(device, &width, &height))
                return -1;
            if (landscape != (width > height) && !lx_pm_printer_page(device, height, width))
                return -1;
        }
        lx_pm_printer_options(device, dc->print_copies, dc->print_duplex, dc->print_draft);
        if (code == WIN32_SETCOPYCOUNT && output)
        {
            if (count == 2)
                lx_guest_store_u16(output, (uint16)(dc->print_copies ? dc->print_copies : 1));
            else
                lx_guest_store_u32(output, dc->print_copies ? dc->print_copies : 1);
        }
        return 1;
    }
    if (code == WIN32_GETTECHNOLOGY)
    {
        /* The virtual queues and the display are raster devices. */
        if (!output)
            return 0;
        const char *technology =
            lx_pm_printer_kind(GpiQueryDevice(h)) ? "Raster Printer" : "Raster Display";
        memcpy(output, technology, strlen(technology) + 1);
        return 1;
    }
    /* SETLINECAP, SETLINEJOIN and GETFACENAME apply only to PostScript drivers. */
    if (code == WIN32_SETLINECAP || code == WIN32_SETLINEJOIN || code == WIN32_GETFACENAME)
        return 0;
    if (code == WIN32_SETMITERLIMIT)
    {
        if (!input || !output || (count != 2 && count != 4))
            return 0;
        LONG previous = dc->miter_limit > 0 ? (LONG)dc->miter_limit : DEFAULT_MITER_LIMIT;
        LONG value = count == 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        if (value > 0)
            dc->miter_limit = value;
        if (count == 4)
            lx_guest_store_u32(output, (ULONG)previous);
        else
            lx_guest_store_u16(output, (uint16)previous);
        return 1;
    }
    if (code == WIN32_GETSETPAPERBINS)
    {
        /* The queues have one bin: the current bin and the bin count. */
        if (dc->kind != OPEN32_DC_PRINTER && dc->kind != OPEN32_DC_PRINTER_INFORMATION)
            return 0;
        if (output)
        {
            lx_guest_store_u16(output, WIN32_DMBIN_AUTO);
            lx_guest_store_u16((uint8 *)output + 2, 1);
        }
        return 1;
    }
    if (code == WIN32_ENUMPAPERBINS)
    {
        if (dc->kind != OPEN32_DC_PRINTER && dc->kind != OPEN32_DC_PRINTER_INFORMATION)
            return 0;
        if (!input || (count != 2 && count != 4))
            return 0;
        LONG capacity =
            count == 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        if (output && capacity >= 1)
        {
            lx_guest_store_u16(output, WIN32_DMBIN_AUTO);
            memset((uint8 *)output + (size_t)capacity * 2, 0, BIN_NAME_LENGTH);
            memcpy((uint8 *)output + (size_t)capacity * 2, "Auto", 5);
        }
        return 1;
    }
    if (code == WIN32_GETEXTENTTABLE)
    {
        if (count != 2 || !output)
            return -1;
        ULONG first = ((uint8 *)input)[0], last = ((uint8 *)input)[1];
        if (last < first)
            return -1;
        /* One 32-bit width per byte-coded character. */
        uint8 widths[(UINT8_MAX + 1) * 4];
        if (!GetCharWidth(h, first, last, widths))
            return -1;
        for (ULONG i = 0; i <= last - first; i++)
            lx_guest_store_u16((uint8 *)output + i * 2, (uint16)lx_guest_load_u32(widths + i * 4));
        return 1;
    }
    if (code == WIN32_EXTTEXTOUT)
    {
        if (count != EXTTEXTOUT_SIZE || !input)
            return 0;
        const uint8 *record = input;
        ULONG text = lx_guest_load_u32(record + EXTTEXTOUT_TEXT),
              advances = lx_guest_load_u32(record + EXTTEXTOUT_ADVANCES);
        char *string =
            text ? lx_execution_copy_string(GProcessState.execution, text, EXTTEXTOUT_STRING_LIMIT)
                 : NULL;
        if (!string)
            return 0;
        ULONG length = (ULONG)strlen(string);
        LxGuestBorrow widths = {0};
        BOOL ready = !advances || !length ||
                     lx_execution_borrow(GProcessState.execution, advances, (size_t)length * 4,
                                         LX_GUEST_READ, &widths);
        LONG result = ready && ExtTextOut(h, lx_guest_load_u16(record + EXTTEXTOUT_X),
                                          lx_guest_load_u16(record + EXTTEXTOUT_Y), 0, NULL, string,
                                          length, (PLONG)widths.host);
        lx_execution_release(&widths);
        free(string);
        return result;
    }
    if (code == WIN32_BEGIN_PATH)
        return BeginPath(h) ? 1 : -1;
    if (code == WIN32_CLIP_TO_PATH)
        return open32PathClip(h, WIN32_RGN_COPY) ? 1 : -1;
    if (code == WIN32_END_PATH)
        return EndPath(h) ? 1 : -1;
    if (code == WIN32_SET_ARC_DIRECTION)
    {
        if (count < 2 || !input)
            return 0;
        LONG previous = dc->arc_clockwise ? WIN32_AD_CLOCKWISE : WIN32_AD_COUNTERCLOCKWISE;
        LONG value = count >= 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        if (value == WIN32_AD_COUNTERCLOCKWISE || value == WIN32_AD_CLOCKWISE)
            dc->arc_clockwise = value == WIN32_AD_CLOCKWISE;
        return previous;
    }
    if (code == WIN32_PASSTHROUGH || code == WIN32_POSTSCRIPT_DATA)
    {
        /* PASSTHROUGH: a word count, then raw device data, sent as DEVESC_RAWDATA. */
        if (!input || count < 2)
            return 0;
        LONG size = lx_guest_load_u16(input);
        if (size > count - 2)
            return 0;
        return DevEscape(GpiQueryDevice(h), DEVESC_RAWDATA, size, (uint8 *)input + 2, NULL, NULL);
    }
    if (code == WIN32_NEWFRAME)
    {
        LONG result = EndPage(h);
        if (result > 0)
            StartPage(h);
        return result;
    }
    if (code == WIN32_ABORTDOC)
        return AbortDoc(h);
    if (code == WIN32_STARTDOC)
    {
        if (!input || !count)
            return 0;
        ULONG address = 0;
        LxGuestMapping *title = lx_execution_map(GProcessState.execution, 0, (size_t)count + 1,
                                                 LX_GUEST_READ | LX_GUEST_WRITE, 0);
        if (!title)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        memcpy(lx_execution_mapping_host(title), input, (size_t)count);
        ((char *)lx_execution_mapping_host(title))[count] = 0;
        address = lx_execution_mapping_address(title);
        uint8 info[WIN32_DOCINFO_SIZE] = {0};
        lx_guest_store_u32(info + WIN32_DOCINFO_SIZE_FIELD, WIN32_DOCINFO_SIZE);
        lx_guest_store_u32(info + WIN32_DOCINFO_DOCUMENT_NAME, address);
        LONG result = StartDoc(h, info);
        lx_execution_unmap(GProcessState.execution, title);
        if (result > 0)
            StartPage(h);
        return result;
    }
    if (code == WIN32_ENDDOC)
        return EndDoc(h);
    if (code == WIN32_GETPHYSPAGESIZE || code == WIN32_GETPRINTINGOFFSET ||
        code == WIN32_GETSCALINGFACTOR)
    {
        if (!output)
            return 0;
        /* The virtual pages have no unprintable margin and no device scaling. */
        ULONG x = code == WIN32_GETPHYSPAGESIZE ? (ULONG)GetDeviceCaps(h, WIN32_PHYSICALWIDTH) : 0,
              y = code == WIN32_GETPHYSPAGESIZE ? (ULONG)GetDeviceCaps(h, WIN32_PHYSICALHEIGHT) : 0;
        lx_guest_store_u32(output, x);
        lx_guest_store_u32((uint8 *)output + 4, y);
        return 1;
    }
    if (output)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    return DevEscape(GpiQueryDevice(h), code, count, input, NULL, NULL);
}
