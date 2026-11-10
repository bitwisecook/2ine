#include "os2/include/bsedos.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmdevp.h"
#include "os2/ps/pmgpi/internal.h"

/* Public domain. No rights reserved. Wholly AI-authored. */
/* GPI structures are copied from checked guest spans before host drawing. */

/* Bounds on what one guest call may make the host copy or allocate. */
enum
{
    BRIDGE_MAX_BYTES = 64 * 1024 * 1024,
    BRIDGE_MAX_NAME = 256,
    BRIDGE_MAX_DEVOPEN_STRING = 32768,
    BRIDGE_MAX_PATH = 65536,
    BRIDGE_MAX_POINTS = 65536,
    BRIDGE_MAX_STRING_CHARS = 65536,
    BRIDGE_MAX_REGION_RECTS = 4096,
    BRIDGE_MAX_ESCAPE_BYTES = 1048576,
    BRIDGE_MAX_BITMAP_FORMATS = 65536,
    /* RGNRECT.crc */
    BRIDGE_MAX_QUERY_RECTS = 65536,
    /* DevQueryHardcopyCaps: HCINFO is CHAR szFormname[32] and nine LONGs. */
    HCINFO_SIZE = 68
};

static uint32 gpi_word(const uint8 *a, unsigned i)
{
    return lx_guest_load_u32(a + 4 * i);
}

static uint32 bridge32_GpiCreatePS(uint8 *a)
{
    SIZEL size;
    if (!lx_execution_read(GProcessState.execution, gpi_word(a, 2), &size, sizeof size))
        return 0;
    return GpiCreatePS(gpi_word(a, 0), gpi_word(a, 1), &size, gpi_word(a, 3));
}

static uint32 bridge32_GpiDestroyPS(uint8 *a)
{
    return GpiDestroyPS(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetDrawingMode(uint8 *a)
{
    return GpiSetDrawingMode(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiSetColor(uint8 *a)
{
    return GpiSetColor(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiMove(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &p, sizeof p)
               ? GpiMove(gpi_word(a, 0), &p)
               : 0;
}

static uint32 bridge32_GpiLine(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &p, sizeof p)
               ? GpiLine(gpi_word(a, 0), &p)
               : 0;
}

static uint32 bridge32_GpiSetPel(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &p, sizeof p)
               ? GpiSetPel(gpi_word(a, 0), &p)
               : 0;
}

static uint32 bridge32_GpiErase(uint8 *a)
{
    return GpiErase(gpi_word(a, 0));
}

static uint32 bridge32_GpiQueryColorIndex(uint8 *a)
{
    return (uint32)GpiQueryColorIndex(gpi_word(a, 0), gpi_word(a, 1), (LONG)gpi_word(a, 2));
}

static uint32 bridge32_GpiQueryRGBColor(uint8 *a)
{
    return (uint32)GpiQueryRGBColor(gpi_word(a, 0), gpi_word(a, 1), (LONG)gpi_word(a, 2));
}

static uint32 bridge32_GpiQueryColorData(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1), output[QCD_LCT_OPTIONS + 1];
    if (count < 0 || count > QCD_LCT_OPTIONS + 1)
        return 0;
    uint32 result = GpiQueryColorData(gpi_word(a, 0), count, output);
    if (result && count)
    {
        uint8 wire[sizeof output];
        for (LONG i = 0; i < count; i++)
            lx_guest_store_u32(wire + 4 * i, output[i]);
        result =
            lx_execution_write(GProcessState.execution, gpi_word(a, 2), wire, (size_t)count * 4);
    }
    return result;
}

static uint32 bridge32_GpiQueryFonts(uint8 *a)
{
    const uint32 name_pointer = gpi_word(a, 2);
    char *face = name_pointer ? lx_execution_copy_string(GProcessState.execution, name_pointer,
                                                         BRIDGE_MAX_NAME)
                              : NULL;
    LxGuestBorrow count = {0}, output = {0};
    uint32 result = (uint32)-255;
    if ((!name_pointer || face) && lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), 4,
                                                       LX_GUEST_READ | LX_GUEST_WRITE, &count))
    {
        LONG requested = (LONG)lx_guest_load_u32(count.host), size = (LONG)gpi_word(a, 4);
        uint64 bytes = requested > 0 && size > 0 ? (uint64)(uint32)requested * (uint32)size : 0;
        if (requested >= 0 && size >= 0 && bytes <= UINT32_MAX &&
            (!requested || (size && lx_execution_borrow(GProcessState.execution, gpi_word(a, 5),
                                                        (size_t)bytes, LX_GUEST_WRITE, &output))))
        {
            result = (uint32)GpiQueryFonts(gpi_word(a, 0), gpi_word(a, 1), face, &requested, size,
                                           output.host);
            if ((LONG)result != -255)
                lx_guest_store_u32(count.host, (uint32)requested);
        }
    }
    lx_execution_release(&output);
    lx_execution_release(&count);
    free(face);
    return result;
}

static uint32 bridge32_GpiQueryFontMetrics(uint8 *a)
{
    const LONG size = (LONG)gpi_word(a, 1);
    if (size < 0)
        return 0;
    LxGuestBorrow output = {0};
    const uint32 bytes = (uint32)size < sizeof(FONTMETRICS) ? (uint32)size : sizeof(FONTMETRICS);
    if (bytes && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), bytes,
                                      LX_GUEST_WRITE, &output))
        return 0;
    uint32 result = GpiQueryFontMetrics(gpi_word(a, 0), size, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DevQueryCaps(uint8 *a)
{
    LONG start = (LONG)gpi_word(a, 1), count = (LONG)gpi_word(a, 2);
    if (start < 0 || count < 0 || start > GPI_CAPS_COUNT || count > GPI_CAPS_COUNT - start)
        return 0;
    LxGuestBorrow output = {0};
    if (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)count * 4,
                                      LX_GUEST_WRITE, &output))
        return 0;
    uint32 result = DevQueryCaps(gpi_word(a, 0), start, count, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GpiQueryTextBox(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1), points = (LONG)gpi_word(a, 3);
    if (count < 0 || count > GPI_TEXTBOX_MAX_CHARS || points < 0)
        return 0;
    LxGuestBorrow text = {0}, output = {0};
    uint32 result = 0;
    size_t bytes = (size_t)(points < TXTBOX_COUNT ? points : TXTBOX_COUNT) * sizeof(POINTL);
    if ((!count || lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)count,
                                       LX_GUEST_READ, &text)) &&
        (!bytes || lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), bytes,
                                       LX_GUEST_WRITE, &output)))
        result = GpiQueryTextBox(gpi_word(a, 0), count, text.host, points, output.host);
    lx_execution_release(&output);
    lx_execution_release(&text);
    return result;
}

static uint32 bridge32_DevOpenDC(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 3);
    if (count < 0 || count > GPI_DEVOPEN_FIELDS)
        return 0;
    char *token = gpi_word(a, 2) ? lx_execution_copy_string(GProcessState.execution, gpi_word(a, 2),
                                                            BRIDGE_MAX_NAME)
                                 : NULL;
    if (gpi_word(a, 2) && !token)
        return 0;
    void *fields[GPI_DEVOPEN_FIELDS] = {0};
    uint8 pointers[GPI_DEVOPEN_FIELDS * 4];
    uint32 result = 0;
    if (count &&
        !lx_execution_read(GProcessState.execution, gpi_word(a, 4), pointers, (size_t)count * 4))
        goto done;
    for (LONG i = 0; i < count; i++)
    {
        uint32 address = lx_guest_load_u32(pointers + i * 4);
        if (i == GPI_DEVOPEN_DRIVDATA)
            fields[i] = (void *)(uintptr_t)address;
        else if (address && !(fields[i] = lx_execution_copy_string(GProcessState.execution, address,
                                                                   BRIDGE_MAX_DEVOPEN_STRING)))
            goto done;
    }
    result = DevOpenDC(gpi_word(a, 0), (LONG)gpi_word(a, 1), token, count, fields, gpi_word(a, 5));
done:
    for (unsigned i = 0; i < GPI_DEVOPEN_FIELDS; i++)
        if (i != GPI_DEVOPEN_DRIVDATA)
            free(fields[i]);
    free(token);
    return result;
}

static uint32 bridge32_DevCloseDC(uint8 *a)
{
    return DevCloseDC(gpi_word(a, 0));
}

static uint32 bridge32_GpiQueryDevice(uint8 *a)
{
    return GpiQueryDevice(gpi_word(a, 0));
}

static uint32 bridge32_GpiAssociate(uint8 *a)
{
    return GpiAssociate(gpi_word(a, 0), gpi_word(a, 1));
}

static uint32 bridge32_GpiSetCp(uint8 *a)
{
    return GpiSetCp(gpi_word(a, 0), gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryCp(uint8 *a)
{
    return GpiQueryCp(gpi_word(a, 0));
}

static uint32 bridge32_GpiCreateLogColorTable(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 4);
    /* An LCOLF_RGB table has no entries. */
    if (gpi_word(a, 2) == LCOLF_RGB)
        return GpiCreateLogColorTable(gpi_word(a, 0), gpi_word(a, 1), LCOLF_RGB,
                                      (LONG)gpi_word(a, 3), count, NULL);
    if (count < 0 || count > GPI_MAX_PALETTE_COLORS)
        return 0;
    LxGuestBorrow colors = {0};
    if (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 5), (size_t)count * 4,
                                      LX_GUEST_READ, &colors))
        return 0;
    uint32 result = GpiCreateLogColorTable(gpi_word(a, 0), gpi_word(a, 1), (LONG)gpi_word(a, 2),
                                           (LONG)gpi_word(a, 3), count, colors.host);
    lx_execution_release(&colors);
    return result;
}

static uint32 bridge32_GpiCreateLogFont(uint8 *a)
{
    FATTRS attrs;
    uint8 name[8]; /* STR8 */
    if (!lx_execution_read(GProcessState.execution, gpi_word(a, 3), &attrs, sizeof attrs) ||
        (gpi_word(a, 1) &&
         !lx_execution_read(GProcessState.execution, gpi_word(a, 1), name, sizeof name)))
        return 0;
    return GpiCreateLogFont(gpi_word(a, 0), gpi_word(a, 1) ? name : NULL, (LONG)gpi_word(a, 2),
                            &attrs);
}

static uint32 bridge32_GpiSetCharSet(uint8 *a)
{
    return GpiSetCharSet(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryCharSet(uint8 *a)
{
    return GpiQueryCharSet(gpi_word(a, 0));
}

static uint32 bridge32_GpiDeleteSetId(uint8 *a)
{
    return GpiDeleteSetId(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiSetCharBox(uint8 *a)
{
    SIZEF box;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &box, sizeof box) &&
           GpiSetCharBox(gpi_word(a, 0), &box);
}

static uint32 bridge32_GpiQueryCharBox(uint8 *a)
{
    SIZEF box;
    return GpiQueryCharBox(gpi_word(a, 0), &box) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), &box, sizeof box);
}

static uint32 bridge32_DevQueryDeviceNames(uint8 *a)
{
    char *driver =
        lx_execution_copy_string(GProcessState.execution, gpi_word(a, 1), BRIDGE_MAX_NAME);
    LONG devices = 0, types = 0;
    uint32 result = 0;
    /* STR32 device names, STR64 descriptions and STR16 data types */
    char name[32], description[64], type[16];
    if (driver &&
        lx_execution_read(GProcessState.execution, gpi_word(a, 2), &devices, sizeof devices) &&
        lx_execution_read(GProcessState.execution, gpi_word(a, 5), &types, sizeof types))
    {
        LONG dc = devices, tc = types;
        result = DevQueryDeviceNames(gpi_word(a, 0), driver, &devices, gpi_word(a, 3) ? name : NULL,
                                     gpi_word(a, 4) ? description : NULL, &types,
                                     gpi_word(a, 6) ? type : NULL);
        if (result && ((dc && (!lx_execution_write(GProcessState.execution, gpi_word(a, 3), name,
                                                   sizeof name) ||
                               !lx_execution_write(GProcessState.execution, gpi_word(a, 4),
                                                   description, sizeof description))) ||
                       (tc && !lx_execution_write(GProcessState.execution, gpi_word(a, 6), type,
                                                  sizeof type))))
            result = 0;
        if (!lx_execution_write(GProcessState.execution, gpi_word(a, 2), &devices,
                                sizeof devices) ||
            !lx_execution_write(GProcessState.execution, gpi_word(a, 5), &types, sizeof types))
            result = 0;
    }
    free(driver);
    return result;
}

static uint32 bridge32_GpiSetBackMix(uint8 *a)
{
    return GpiSetBackMix(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryBackMix(uint8 *a)
{
    return GpiQueryBackMix(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetMix(uint8 *a)
{
    return GpiSetMix(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryMix(uint8 *a)
{
    return GpiQueryMix(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetBackColor(uint8 *a)
{
    return GpiSetBackColor(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryBackColor(uint8 *a)
{
    return GpiQueryBackColor(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetLineType(uint8 *a)
{
    return GpiSetLineType(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryLineType(uint8 *a)
{
    return GpiQueryLineType(gpi_word(a, 0));
}

static uint32 bridge32_GpiSavePS(uint8 *a)
{
    return GpiSavePS(gpi_word(a, 0));
}

static uint32 bridge32_GpiRestorePS(uint8 *a)
{
    return GpiRestorePS(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiSetPageViewport(uint8 *a)
{
    RECTL r;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &r, sizeof r) &&
           GpiSetPageViewport(gpi_word(a, 0), &r);
}

static uint32 bridge32_GpiQueryPageViewport(uint8 *a)
{
    RECTL r;
    return GpiQueryPageViewport(gpi_word(a, 0), &r) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), &r, sizeof r);
}

static uint32 bridge32_GpiSetAttrs(uint8 *a)
{
    uint8 bundle[CHARBUNDLE_SIZE] = {0};
    LONG primitive = (LONG)gpi_word(a, 1);
    ULONG mask = gpi_word(a, 2), defaults = gpi_word(a, 3);
    unsigned size = primitive == PRIM_CHAR     ? CHARBUNDLE_SIZE
                    : primitive == PRIM_LINE   ? LINEBUNDLE_SIZE
                    : primitive == PRIM_MARKER ? MARKERBUNDLE_SIZE
                    : primitive == PRIM_AREA   ? AREABUNDLE_SIZE
                                               : IMAGEBUNDLE_SIZE;
    if ((mask & ~defaults) &&
        !lx_execution_read(GProcessState.execution, gpi_word(a, 4), bundle, size))
        return 0;
    return GpiSetAttrs(gpi_word(a, 0), primitive, mask, defaults, bundle);
}

static uint32 bridge32_GpiLoadBitmap(uint8 *a)
{
    return GpiLoadBitmap(gpi_word(a, 0), gpi_word(a, 1), gpi_word(a, 2), (LONG)gpi_word(a, 3),
                         (LONG)gpi_word(a, 4));
}

static uint32 bridge32_GpiDeleteBitmap(uint8 *a)
{
    return GpiDeleteBitmap(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetBitmap(uint8 *a)
{
    return GpiSetBitmap(gpi_word(a, 0), gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryBitmapParameters(uint8 *a)
{
    uint8 data[BMIH_SIZE];
    return GpiQueryBitmapParameters(gpi_word(a, 0), data) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), data, sizeof data);
}

static uint32 bridge32_GpiQueryBitmapInfoHeader(uint8 *a)
{
    uint8 data[BMIH2_SIZE] = {0};
    /* cbFix selects the header version. */
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), data, 4) &&
           GpiQueryBitmapInfoHeader(gpi_word(a, 0), data) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), data, sizeof data);
}

static uint32 bridge32_GpiQueryDeviceBitmapFormats(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    LxGuestBorrow out = {0};
    if (count < 0 || count > BRIDGE_MAX_BITMAP_FORMATS ||
        (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)count * 4,
                                       LX_GUEST_WRITE, &out)))
        return 0;
    uint32 result = GpiQueryDeviceBitmapFormats(gpi_word(a, 0), count, out.host);
    lx_execution_release(&out);
    return result;
}

static uint32 bitmap_blt_bridge(uint8 *a, BOOL bitmap)
{
    LONG count = (LONG)gpi_word(a, 2);
    POINTL points[4];
    if ((count != 3 && count != 4) || !lx_execution_read(GProcessState.execution, gpi_word(a, 3),
                                                         points, (size_t)count * sizeof *points))
        return 0;
    return bitmap ? GpiWCBitBlt(gpi_word(a, 0), gpi_word(a, 1), count, points, (LONG)gpi_word(a, 4),
                                gpi_word(a, 5))
                  : GpiBitBlt(gpi_word(a, 0), gpi_word(a, 1), count, points, (LONG)gpi_word(a, 4),
                              gpi_word(a, 5));
}

static uint32 bridge32_GpiBitBlt(uint8 *a)
{
    return bitmap_blt_bridge(a, FALSE);
}

static uint32 bridge32_GpiWCBitBlt(uint8 *a)
{
    return bitmap_blt_bridge(a, TRUE);
}

static uint8 *bitmap_bridge_info(uint32 address, size_t *bytes, BOOL palette)
{
    uint8 header[BMIH2_SIZE] = {0};
    if (!lx_execution_read(GProcessState.execution, address, header, 4))
        return NULL;
    uint32 fixed = lx_guest_load_u32(header);
    if ((fixed != BMIH_SIZE && fixed != BMIH2_SIZE) ||
        !lx_execution_read(GProcessState.execution, address, header, fixed))
        return NULL;
    unsigned width = fixed == BMIH_SIZE ? lx_guest_load_u16(header + BMIH_CX_OFFSET)
                                        : lx_guest_load_u32(header + BMIH2_CX_OFFSET);
    unsigned height = fixed == BMIH_SIZE ? lx_guest_load_u16(header + BMIH_CY_OFFSET)
                                         : lx_guest_load_u32(header + BMIH2_CY_OFFSET);
    unsigned planes = lx_guest_load_u16(
        header + (fixed == BMIH_SIZE ? BMIH_CPLANES_OFFSET : BMIH2_CPLANES_OFFSET));
    unsigned depth = lx_guest_load_u16(
        header + (fixed == BMIH_SIZE ? BMIH_CBITCOUNT_OFFSET : BMIH2_CBITCOUNT_OFFSET));
    if (!width || !height || width > LX_PM_RASTER_MAX_DIMENSION ||
        height > LX_PM_RASTER_MAX_DIMENSION || (uint64)width * height > LX_PM_RASTER_MAX_PELS ||
        planes != 1 || (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
        return NULL;
    unsigned colors = depth <= 8 ? 1u << depth : 0;
    if (fixed == BMIH2_SIZE && lx_guest_load_u32(header + BMIH2_CCLRUSED_OFFSET))
        colors = lx_guest_load_u32(header + BMIH2_CCLRUSED_OFFSET);
    if (colors > GPI_MAX_PALETTE_COLORS)
        return NULL;
    size_t size = fixed + (palette ? colors * (fixed == BMIH_SIZE ? RGB_SIZE : RGB2_SIZE) : 0);
    uint8 *info = malloc(size);
    if (!info || !lx_execution_read(GProcessState.execution, address, info, size))
    {
        free(info);
        return NULL;
    }
    *bytes = ((size_t)width * depth + 31) / 32 * 4 * height;
    return info;
}

static uint32 bridge32_GpiDrawBits(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 3);
    POINTL points[4];
    if ((count != 3 && count != 4) || !lx_execution_read(GProcessState.execution, gpi_word(a, 4),
                                                         points, (size_t)count * sizeof *points))
        return 0;
    size_t bytes;
    uint8 *info = bitmap_bridge_info(gpi_word(a, 2), &bytes, TRUE);
    LxGuestBorrow bits = {0};
    uint32 result = 0;
    if (info &&
        lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), bytes, LX_GUEST_READ, &bits))
        result = (uint32)GpiDrawBits(gpi_word(a, 0), bits.host, info, count, points,
                                     (LONG)gpi_word(a, 5), gpi_word(a, 6));
    lx_execution_release(&bits);
    free(info);
    return result;
}

static uint32 bridge32_GpiQueryBitmapBits(uint8 *a)
{
    LONG start = (LONG)gpi_word(a, 1), scans = (LONG)gpi_word(a, 2);
    uint8 header[BMIH2_SIZE];
    if (start < 0 || scans < 0 ||
        !lx_execution_read(GProcessState.execution, gpi_word(a, 4), header, 4))
        return (uint32)GPI_ALTERROR;
    unsigned fixed = lx_guest_load_u32(header);
    if ((fixed != BMIH_SIZE && fixed != BMIH2_SIZE) ||
        !lx_execution_read(GProcessState.execution, gpi_word(a, 4), header, fixed))
        return (uint32)GPI_ALTERROR;
    unsigned planes = lx_guest_load_u16(
        header + (fixed == BMIH_SIZE ? BMIH_CPLANES_OFFSET : BMIH2_CPLANES_OFFSET));
    unsigned depth = lx_guest_load_u16(
        header + (fixed == BMIH_SIZE ? BMIH_CBITCOUNT_OFFSET : BMIH2_CBITCOUNT_OFFSET));
    if (planes != 1 || (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
        return (uint32)GPI_ALTERROR;
    PresentationSpace *ps = lx_pm_font_space(gpi_word(a, 0));
    PmBitmap *bitmap = ps ? ps->bitmap_lease.resource : NULL;
    if (!bitmap || (ULONG)start > bitmap->height)
        return (uint32)GPI_ALTERROR;
    unsigned rows =
        (ULONG)scans > bitmap->height - (ULONG)start ? bitmap->height - (ULONG)start : (ULONG)scans;
    size_t size =
        fixed + (depth <= 8 ? (1u << depth) * (fixed == BMIH_SIZE ? RGB_SIZE : RGB2_SIZE) : 0);
    size_t bytes = ((size_t)bitmap->width * depth + 31) / 32 * 4 * rows;
    LxGuestBorrow info = {0}, bits = {0};
    uint32 result = (uint32)GPI_ALTERROR;
    if (lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), size,
                            LX_GUEST_READ | LX_GUEST_WRITE, &info) &&
        (!bytes || lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), bytes,
                                       LX_GUEST_WRITE, &bits)))
        result = (uint32)GpiQueryBitmapBits(gpi_word(a, 0), start, scans, bits.host, info.host);
    lx_execution_release(&bits);
    lx_execution_release(&info);
    return result;
}

static uint32 bridge32_GpiCreateBitmap(uint8 *a)
{
    size_t bytes = 0, initial_bytes = 0;
    uint8 *header = bitmap_bridge_info(gpi_word(a, 1), &bytes, FALSE);
    if (!header)
        return 0;
    uint8 *info = NULL;
    LxGuestBorrow bits = {0};
    uint32 result = 0;
    if (gpi_word(a, 2) & CBM_INIT)
    {
        info = bitmap_bridge_info(gpi_word(a, 4), &initial_bytes, TRUE);
        if (!info || !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), initial_bytes,
                                          LX_GUEST_READ, &bits))
            goto done;
    }
    result = GpiCreateBitmap(gpi_word(a, 0), header, gpi_word(a, 2), bits.host, info);
done:
    lx_execution_release(&bits);
    free(info);
    free(header);
    return result;
}

static uint32 bridge32_GpiSetBitmapId(uint8 *a)
{
    return GpiSetBitmapId(gpi_word(a, 0), gpi_word(a, 1), (LONG)gpi_word(a, 2));
}

static uint32 bridge32_GpiQueryBitmapHandle(uint8 *a)
{
    return GpiQueryBitmapHandle(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryNumberSetIds(uint8 *a)
{
    return GpiQueryNumberSetIds(gpi_word(a, 0));
}

static uint32 bridge32_GpiQuerySetIds(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    LxGuestBorrow types = {0}, names = {0}, ids = {0};
    uint32 result = 0;
    if (count < 0 || count > GPI_LCID_MAX + 1)
        return 0;
    /* STR8 names */
    if (!count || (lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)count * 4,
                                       LX_GUEST_WRITE, &types) &&
                   lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)count * 8,
                                       LX_GUEST_WRITE, &names) &&
                   lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), (size_t)count * 4,
                                       LX_GUEST_WRITE, &ids)))
        result = GpiQuerySetIds(gpi_word(a, 0), count, types.host, names.host, ids.host);
    lx_execution_release(&ids);
    lx_execution_release(&names);
    lx_execution_release(&types);
    return result;
}

static uint32 bridge32_GpiQueryWidthTable(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 2);
    LxGuestBorrow out = {0};
    /* One width for each single-byte code point. */
    if (count < 0 || count > 256 ||
        (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)count * 4,
                                       LX_GUEST_WRITE, &out)))
        return 0;
    uint32 result = GpiQueryWidthTable(gpi_word(a, 0), (LONG)gpi_word(a, 1), count, out.host);
    lx_execution_release(&out);
    return result;
}

static uint32 bridge32_GpiCharString(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    LxGuestBorrow text = {0};
    if (count < 0 || count > BRIDGE_MAX_STRING_CHARS ||
        (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)count,
                                       LX_GUEST_READ, &text)))
        return 0;
    uint32 result = GpiCharString(gpi_word(a, 0), count, text.host);
    lx_execution_release(&text);
    return result;
}

static uint32 bridge32_GpiCharStringAt(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 2);
    LxGuestBorrow text = {0};
    POINTL point;
    if (count < 0 || count > BRIDGE_MAX_STRING_CHARS ||
        !lx_execution_read(GProcessState.execution, gpi_word(a, 1), &point, sizeof point) ||
        (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)count,
                                       LX_GUEST_READ, &text)))
        return 0;
    uint32 result = GpiCharStringAt(gpi_word(a, 0), &point, count, text.host);
    lx_execution_release(&text);
    return result;
}

static uint32 bridge32_GpiQueryPatternSet(uint8 *a)
{
    return GpiQueryPatternSet(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetPatternSet(uint8 *a)
{
    return GpiSetPatternSet(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryPattern(uint8 *a)
{
    return GpiQueryPattern(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetPattern(uint8 *a)
{
    return GpiSetPattern(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryCurrentPosition(uint8 *a)
{
    POINTL p;
    return GpiQueryCurrentPosition(gpi_word(a, 0), &p) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), &p, sizeof p);
}

static uint32 bridge32_GpiSetCurrentPosition(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &p, sizeof p) &&
           GpiSetCurrentPosition(gpi_word(a, 0), &p);
}

static uint32 bridge32_GpiCreateRegion(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    LxGuestBorrow rects = {0};
    if (count < 0 || count > BRIDGE_MAX_REGION_RECTS ||
        (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2),
                                       (size_t)count * sizeof(RECTL), LX_GUEST_READ, &rects)))
        return 0;
    uint32 result = GpiCreateRegion(gpi_word(a, 0), count, rects.host);
    lx_execution_release(&rects);
    return result;
}

static uint32 bridge32_GpiSetRegion(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 2);
    LxGuestBorrow rects = {0};
    if (count < 0 || count > BRIDGE_MAX_REGION_RECTS ||
        (count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3),
                                       (size_t)count * sizeof(RECTL), LX_GUEST_READ, &rects)))
        return 0;
    uint32 result = GpiSetRegion(gpi_word(a, 0), gpi_word(a, 1), count, rects.host);
    lx_execution_release(&rects);
    return result;
}

static uint32 bridge32_GpiDestroyRegion(uint8 *a)
{
    return GpiDestroyRegion(gpi_word(a, 0), gpi_word(a, 1));
}

static uint32 bridge32_GpiCombineRegion(uint8 *a)
{
    return GpiCombineRegion(gpi_word(a, 0), gpi_word(a, 1), gpi_word(a, 2), gpi_word(a, 3),
                            (LONG)gpi_word(a, 4));
}

static uint32 bridge32_GpiQueryClipRegion(uint8 *a)
{
    return GpiQueryClipRegion(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetClipRegion(uint8 *a)
{
    ULONG old = 0;
    LxGuestBorrow out = {0};
    if (gpi_word(a, 2) &&
        !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), 4, LX_GUEST_WRITE, &out))
        return 0;
    uint32 result = GpiSetClipRegion(gpi_word(a, 0), gpi_word(a, 1), &old);
    if (result && out.host)
        lx_guest_store_u32(out.host, old);
    lx_execution_release(&out);
    return result;
}

static uint32 bridge32_GpiQueryRegionBox(uint8 *a)
{
    RECTL rect = {0};
    uint32 result = GpiQueryRegionBox(gpi_word(a, 0), gpi_word(a, 1), &rect);
    return result && lx_execution_write(GProcessState.execution, gpi_word(a, 2), &rect, sizeof rect)
               ? result
               : 0;
}

static uint32 bridge32_GpiQueryClipBox(uint8 *a)
{
    RECTL rect = {0};
    uint32 result = GpiQueryClipBox(gpi_word(a, 0), &rect);
    return result && lx_execution_write(GProcessState.execution, gpi_word(a, 1), &rect, sizeof rect)
               ? result
               : 0;
}

static uint32 bridge32_GpiRectVisible(uint8 *a)
{
    RECTL rect;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &rect, sizeof rect)
               ? GpiRectVisible(gpi_word(a, 0), &rect)
               : 0;
}

static uint32 bridge32_GpiIntersectClipRectangle(uint8 *a)
{
    RECTL rect;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &rect, sizeof rect)
               ? GpiIntersectClipRectangle(gpi_word(a, 0), &rect)
               : 0;
}

static uint32 bridge32_GpiOffsetRegion(uint8 *a)
{
    POINTL point;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 2), &point, sizeof point)
               ? GpiOffsetRegion(gpi_word(a, 0), gpi_word(a, 1), &point)
               : 0;
}

static uint32 bridge32_GpiOffsetClipRegion(uint8 *a)
{
    POINTL point;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &point, sizeof point)
               ? GpiOffsetClipRegion(gpi_word(a, 0), &point)
               : 0;
}

static uint32 positioned_text_bridge(uint8 *a, BOOL at)
{
    unsigned offset = at ? 1 : 0;
    LONG count = (LONG)gpi_word(a, 3 + offset);
    ULONG flags = gpi_word(a, 2 + offset);
    POINTL point;
    RECTL rect;
    LxGuestBorrow text = {0}, advances = {0};
    uint32 result = 0;
    if (count < 0 || count > BRIDGE_MAX_STRING_CHARS ||
        (at && !lx_execution_read(GProcessState.execution, gpi_word(a, 1), &point, sizeof point)) ||
        (gpi_word(a, 1 + offset) &&
         !lx_execution_read(GProcessState.execution, gpi_word(a, 1 + offset), &rect, sizeof rect)))
        return 0;
    if ((count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 4 + offset),
                                       (size_t)count, LX_GUEST_READ, &text)) ||
        ((flags & CHS_VECTOR) && count &&
         !lx_execution_borrow(GProcessState.execution, gpi_word(a, 5 + offset), (size_t)count * 4,
                              LX_GUEST_READ, &advances)))
        goto done;
    result = at ? GpiCharStringPosAt(gpi_word(a, 0), &point, gpi_word(a, 2) ? &rect : NULL, flags,
                                     count, text.host, advances.host)
                : GpiCharStringPos(gpi_word(a, 0), gpi_word(a, 1) ? &rect : NULL, flags, count,
                                   text.host, advances.host);
done:
    lx_execution_release(&advances);
    lx_execution_release(&text);
    return result;
}

static uint32 bridge32_GpiCharStringPosAt(uint8 *a)
{
    return positioned_text_bridge(a, TRUE);
}

static uint32 bridge32_GpiCharStringPos(uint8 *a)
{
    return positioned_text_bridge(a, FALSE);
}

static uint32 bridge32_GpiBox(uint8 *a)
{
    POINTL point;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 2), &point, sizeof point)
               ? GpiBox(gpi_word(a, 0), (LONG)gpi_word(a, 1), &point, (LONG)gpi_word(a, 3),
                        (LONG)gpi_word(a, 4))
               : 0;
}

static uint32 bridge32_DevQueryHardcopyCaps(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 2);
    if (count < 0 || count > 1)
        return (uint32)DQHC_ERROR;
    LxGuestBorrow output = {0};
    uint32 result = (uint32)DQHC_ERROR;
    if (!count || lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), HCINFO_SIZE,
                                      LX_GUEST_WRITE, &output))
        result =
            (uint32)DevQueryHardcopyCaps(gpi_word(a, 0), (LONG)gpi_word(a, 1), count, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GpiQueryColor(uint8 *a)
{
    return (uint32)GpiQueryColor(gpi_word(a, 0));
}

static uint32 bridge32_GpiQueryLogColorTable(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 3);
    if (count < 0)
        return (uint32)QLCT_ERROR;
    /* At most an index and a value for each entry. */
    LONG values[2 * GPI_MAX_PALETTE_COLORS];
    size_t copied = count > (LONG)(sizeof values / sizeof *values) ? sizeof values / sizeof *values
                                                                   : (size_t)count;
    LxGuestBorrow output = {0};
    if (copied && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), copied * 4,
                                       LX_GUEST_WRITE, &output))
        return (uint32)QLCT_ERROR;
    LONG result = GpiQueryLogColorTable(gpi_word(a, 0), gpi_word(a, 1), (LONG)gpi_word(a, 2),
                                        (LONG)copied, values);
    for (LONG i = 0; i < result; i++)
        lx_guest_store_u32((uint8 *)output.host + i * 4, (uint32)values[i]);
    lx_execution_release(&output);
    return (uint32)result;
}

static uint32 bridge32_GpiSetCharMode(uint8 *a)
{
    return GpiSetCharMode(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryCharMode(uint8 *a)
{
    return (uint32)GpiQueryCharMode(gpi_word(a, 0));
}

static uint32 bridge32_GpiConvert(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 3);
    if (count < 0 || count > BRIDGE_MAX_POINTS)
        return 0;
    size_t bytes = (size_t)count * sizeof(POINTL);
    POINTL *points = count ? malloc(bytes) : NULL;
    if (count &&
        (!points || !lx_execution_read(GProcessState.execution, gpi_word(a, 4), points, bytes)))
    {
        free(points);
        return 0;
    }
    BOOL result =
        GpiConvert(gpi_word(a, 0), (LONG)gpi_word(a, 1), (LONG)gpi_word(a, 2), count, points);
    if (result && count)
        result = lx_execution_write(GProcessState.execution, gpi_word(a, 4), points, bytes);
    free(points);
    return result;
}

static uint32 bridge32_GpiSetPatternRefPoint(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &p, sizeof p)
               ? GpiSetPatternRefPoint(gpi_word(a, 0), &p)
               : 0;
}

static uint32 bridge32_GpiQueryPatternRefPoint(uint8 *a)
{
    POINTL p;
    return GpiQueryPatternRefPoint(gpi_word(a, 0), &p) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), &p, sizeof p);
}

static uint32 bridge32_GpiPolyLine(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    if (count < 0 || count > BRIDGE_MAX_POINTS)
        return 0;
    POINTL *points = count ? malloc((size_t)count * sizeof *points) : NULL;
    if (count && (!points || !lx_execution_read(GProcessState.execution, gpi_word(a, 2), points,
                                                (size_t)count * sizeof *points)))
    {
        free(points);
        return 0;
    }
    uint32 result = GpiPolyLine(gpi_word(a, 0), count, points);
    free(points);
    return result;
}

static uint32 bridge32_GpiQueryRegionRects(uint8 *a)
{
    RGNRECT control;
    RECTL bounds;
    if (!lx_execution_read(GProcessState.execution, gpi_word(a, 3), &control, sizeof control) ||
        (gpi_word(a, 4) && control.crc > BRIDGE_MAX_QUERY_RECTS))
        return 0;
    if (gpi_word(a, 2) &&
        !lx_execution_read(GProcessState.execution, gpi_word(a, 2), &bounds, sizeof bounds))
        return 0;
    LxGuestBorrow control_output = {0}, rectangles = {0};
    if (!lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), sizeof control,
                             LX_GUEST_WRITE, &control_output))
        return 0;
    if (gpi_word(a, 4) &&
        (!control.crc ||
         !lx_execution_borrow(GProcessState.execution, gpi_word(a, 4),
                              (size_t)control.crc * sizeof(RECTL), LX_GUEST_WRITE, &rectangles)))
    {
        lx_execution_release(&control_output);
        return 0;
    }
    BOOL result = GpiQueryRegionRects(gpi_word(a, 0), gpi_word(a, 1),
                                      gpi_word(a, 2) ? &bounds : NULL, &control, rectangles.host);
    if (result)
        memcpy(control_output.host, &control, sizeof control);
    lx_execution_release(&rectangles);
    lx_execution_release(&control_output);
    return result;
}

static uint32 bridge32_GpiQueryCharAngle(uint8 *a)
{
    POINTL angle;
    return GpiQueryCharAngle(gpi_word(a, 0), &angle) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), &angle, sizeof angle);
}

static uint32 bridge32_GpiQueryCharShear(uint8 *a)
{
    POINTL shear;
    return GpiQueryCharShear(gpi_word(a, 0), &shear) &&
           lx_execution_write(GProcessState.execution, gpi_word(a, 1), &shear, sizeof shear);
}

static uint32 bridge32_GpiQueryTextAlignment(uint8 *a)
{
    LONG horizontal, vertical;
    LxGuestBorrow h = {0}, v = {0};
    uint32 result = lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), sizeof horizontal,
                                        LX_GUEST_WRITE, &h) &&
                    lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), sizeof vertical,
                                        LX_GUEST_WRITE, &v) &&
                    GpiQueryTextAlignment(gpi_word(a, 0), &horizontal, &vertical);
    if (result)
    {
        lx_guest_store_u32(h.host, horizontal);
        lx_guest_store_u32(v.host, vertical);
    }
    lx_execution_release(&v);
    lx_execution_release(&h);
    return result;
}

static uint32 bridge32_GpiSetCharAngle(uint8 *a)
{
    POINTL value;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &value, sizeof value) &&
           GpiSetCharAngle(gpi_word(a, 0), &value);
}

static uint32 bridge32_GpiSetCharShear(uint8 *a)
{
    POINTL value;
    return lx_execution_read(GProcessState.execution, gpi_word(a, 1), &value, sizeof value) &&
           GpiSetCharShear(gpi_word(a, 0), &value);
}

static uint32 bridge32_GpiSetCharDirection(uint8 *a)
{
    return GpiSetCharDirection(gpi_word(a, 0), (LONG)gpi_word(a, 1));
}

static uint32 bridge32_GpiQueryCharDirection(uint8 *a)
{
    return (uint32)GpiQueryCharDirection(gpi_word(a, 0));
}

static uint32 bridge32_GpiSetTextAlignment(uint8 *a)
{
    return GpiSetTextAlignment(gpi_word(a, 0), (LONG)gpi_word(a, 1), (LONG)gpi_word(a, 2));
}

static uint32 bridge32_DevEscape(uint8 *a)
{
    LONG input_count = (LONG)gpi_word(a, 2), output_count = 0;
    if (input_count < 0 || input_count > BRIDGE_MAX_ESCAPE_BYTES)
        return (uint32)DEVESC_ERROR;
    LxGuestBorrow input = {0}, count = {0}, output = {0};
    uint32 result = (uint32)DEVESC_ERROR;
    if (input_count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3),
                                            (size_t)input_count, LX_GUEST_READ, &input))
        goto done;
    if (gpi_word(a, 4))
    {
        if (!lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), 4,
                                 LX_GUEST_READ | LX_GUEST_WRITE, &count))
            goto done;
        output_count = (LONG)lx_guest_load_u32(count.host);
        if (output_count < 0 || output_count > BRIDGE_MAX_ESCAPE_BYTES ||
            (output_count && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 5),
                                                  (size_t)output_count, LX_GUEST_WRITE, &output)))
            goto done;
    }
    result = (uint32)DevEscape(gpi_word(a, 0), (LONG)gpi_word(a, 1), input_count, input.host,
                               count.host ? &output_count : NULL, output.host);
    if (count.host)
        lx_guest_store_u32(count.host, (ULONG)output_count);
done:
    lx_execution_release(&output);
    lx_execution_release(&count);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_DevPostDeviceModes(uint8 *a)
{
    /* The driver, device and queue names */
    char *strings[3] = {NULL, NULL, NULL};
    LxGuestBorrow data = {0};
    uint32 result = (uint32)DPDM_ERROR;
    for (unsigned i = 0; i < 3; i++)
        if (gpi_word(a, i + 2) &&
            !(strings[i] = lx_execution_copy_string(GProcessState.execution, gpi_word(a, i + 2),
                                                    BRIDGE_MAX_NAME)))
            goto done;
    if (gpi_word(a, 1) &&
        !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), LX_PRINTER_DRIVER_DATA_SIZE,
                             LX_GUEST_READ | LX_GUEST_WRITE, &data))
        goto done;
    result = (uint32)DevPostDeviceModes(gpi_word(a, 0), data.host, strings[0], strings[1],
                                        strings[2], gpi_word(a, 5));
done:
    lx_execution_release(&data);
    for (unsigned i = 0; i < 3; i++)
        free(strings[i]);
    return result;
}

static uint32 bridge32_GpiDeleteMetaFile(uint8 *a)
{
    return GpiDeleteMetaFile(gpi_word(a, 0));
}

static uint32 bridge32_GpiLoadMetaFile(uint8 *a)
{
    char *filename =
        lx_execution_copy_string(GProcessState.execution, gpi_word(a, 1), BRIDGE_MAX_PATH);
    uint32 result = filename ? GpiLoadMetaFile(gpi_word(a, 0), filename) : 0;
    free(filename);
    return result;
}

static uint32 bridge32_GpiPlayMetaFile(uint8 *a)
{
    /* The unavailable metafile engine neither reads nor writes guest arrays. */
    return (uint32)GpiPlayMetaFile(gpi_word(a, 0), gpi_word(a, 1), (LONG)gpi_word(a, 2), NULL, NULL,
                                   (LONG)gpi_word(a, 5), NULL);
}

static uint32 bridge32_GpiSetMetaFileBits(uint8 *a)
{
    LONG length = (LONG)gpi_word(a, 2);
    LxGuestBorrow input = {0};
    uint32 result = 0;
    if (length >= 0 && length <= BRIDGE_MAX_BYTES &&
        (!length || lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)length,
                                        LX_GUEST_READ, &input)))
        result = GpiSetMetaFileBits(gpi_word(a, 0), (LONG)gpi_word(a, 1), length, input.host);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_GpiQueryBoundaryData(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), sizeof(RECTL), LX_GUEST_WRITE,
                            &output))
    {
        RECTL bounds;
        result = GpiQueryBoundaryData(gpi_word(a, 0), &bounds);
        if (result)
            memcpy(output.host, &bounds, sizeof bounds);
    }
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GpiSetDrawControl(uint8 *a)
{
    return GpiSetDrawControl(gpi_word(a, 0), (LONG)gpi_word(a, 1), (LONG)gpi_word(a, 2));
}

static uint32 bridge32_GpiSetDefaultViewMatrix(uint8 *a)
{
    uint8 matrix[GPI_MATRIX_ELEMENTS * 4];
    return (LONG)gpi_word(a, 1) == GPI_MATRIX_ELEMENTS &&
           lx_execution_read(GProcessState.execution, gpi_word(a, 2), matrix, sizeof matrix) &&
           GpiSetDefaultViewMatrix(gpi_word(a, 0), GPI_MATRIX_ELEMENTS, matrix,
                                   (LONG)gpi_word(a, 3));
}

static uint32 bridge32_GpiSetBitmapBits(uint8 *a)
{
    LONG scans = (LONG)gpi_word(a, 2), start = (LONG)gpi_word(a, 1);
    size_t ignored;
    uint8 *info = bitmap_bridge_info(gpi_word(a, 4), &ignored, TRUE);
    if (!info || scans < 0 || scans > LX_PM_RASTER_MAX_DIMENSION || start < 0)
    {
        free(info);
        return (uint32)GPI_ALTERROR;
    }
    PresentationSpace *ps = lx_pm_font_space(gpi_word(a, 0));
    PmBitmap *bitmap = ps ? ps->bitmap_lease.resource : NULL;
    if (!bitmap || (ULONG)start > bitmap->height)
    {
        free(info);
        return (uint32)GPI_ALTERROR;
    }
    unsigned rows =
        (ULONG)scans > bitmap->height - (ULONG)start ? bitmap->height - (ULONG)start : (ULONG)scans;
    ULONG fixed = lx_guest_load_u32(info);
    ULONG width = fixed == BMIH_SIZE ? lx_guest_load_u16(info + BMIH_CX_OFFSET)
                                     : lx_guest_load_u32(info + BMIH2_CX_OFFSET);
    ULONG depth = lx_guest_load_u16(
        info + (fixed == BMIH_SIZE ? BMIH_CBITCOUNT_OFFSET : BMIH2_CBITCOUNT_OFFSET));
    size_t bytes = ((size_t)width * depth + 31) / 32 * 4 * rows;
    LxGuestBorrow input = {0};
    uint32 result = (uint32)GPI_ALTERROR;
    if (!bytes ||
        lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), bytes, LX_GUEST_READ, &input))
        result = (uint32)GpiSetBitmapBits(gpi_word(a, 0), start, scans, input.host, info);
    lx_execution_release(&input);
    free(info);
    return result;
}

static uint32 bridge32_GpiBeginArea(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiBeginArea((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiBeginPath(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiBeginPath((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiCloseFigure(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiCloseFigure((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiCopyMetaFile(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiCopyMetaFile((ULONG)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiEndArea(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiEndArea((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiEndPath(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiEndPath((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiFillPath(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiFillPath((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                            (LONG)gpi_word(a, 2));
    return result;
}

static uint32 bridge32_GpiFullArc(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiFullArc((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                           (LONG)gpi_word(a, 2));
    return result;
}

static uint32 bridge32_GpiPaintRegion(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiPaintRegion((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiPointArc(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = 2 * sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiPointArc((HPS)gpi_word(a, 0), (PPOINTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiPtInRegion(uint8 *a)
{
    LxGuestBorrow p2 = {0};
    uint64_t n2 = sizeof(POINTL);
    uint32 result = 0;
    if (n2 > BRIDGE_MAX_BYTES ||
        (n2 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)n2,
                                    LX_GUEST_READ, &p2)))
        goto done;
    result = (uint32)(uintptr_t)GpiPtInRegion((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1),
                                              (PPOINTL)p2.host);
done:
    lx_execution_release(&p2);
    return result;
}

static uint32 bridge32_GpiQueryDefaultViewMatrix(uint8 *a)
{
    LxGuestBorrow p2 = {0};
    uint64_t n2 = (uint64_t)gpi_word(a, 1) * 4;
    uint32 result = 0;
    if (n2 > BRIDGE_MAX_BYTES ||
        (n2 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)n2,
                                    LX_GUEST_WRITE, &p2)))
        goto done;
    /* A zero count still needs a non-NULL buffer, though nothing is stored in it. */
    result = (uint32)(uintptr_t)GpiQueryDefaultViewMatrix((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                                          n2               ? (PVOID)p2.host
                                                          : gpi_word(a, 2) ? (PVOID)a
                                                                           : NULL);
done:
    lx_execution_release(&p2);
    return result;
}

static uint32 bridge32_GpiQueryGraphicsField(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(RECTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_WRITE, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryGraphicsField((HPS)gpi_word(a, 0), (PRECTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiQueryLineEnd(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiQueryLineEnd((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiQueryLineJoin(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiQueryLineJoin((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiQueryLineWidthGeom(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiQueryLineWidthGeom((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiQueryMetaFileBits(uint8 *a)
{
    LxGuestBorrow p3 = {0};
    uint64_t n3 = (uint64_t)gpi_word(a, 2);
    uint32 result = 0;
    if (n3 > BRIDGE_MAX_BYTES ||
        (n3 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)n3,
                                    LX_GUEST_WRITE, &p3)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryMetaFileBits((ULONG)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                                     (LONG)gpi_word(a, 2), (PBYTE)p3.host);
done:
    lx_execution_release(&p3);
    return result;
}

static uint32 bridge32_GpiQueryMetaFileLength(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiQueryMetaFileLength((ULONG)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiQueryModelTransformMatrix(uint8 *a)
{
    LxGuestBorrow p2 = {0};
    uint64_t n2 = (uint64_t)gpi_word(a, 1) * 4;
    uint32 result = 0;
    if (n2 > BRIDGE_MAX_BYTES ||
        (n2 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)n2,
                                    LX_GUEST_WRITE, &p2)))
        goto done;
    /* A zero count still needs a non-NULL buffer, though nothing is stored in it. */
    result =
        (uint32)(uintptr_t)GpiQueryModelTransformMatrix((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                                        n2               ? (PVOID)p2.host
                                                        : gpi_word(a, 2) ? (PVOID)a
                                                                         : NULL);
done:
    lx_execution_release(&p2);
    return result;
}

static uint32 bridge32_GpiQueryNearestColor(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiQueryNearestColor((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1),
                                                     (LONG)gpi_word(a, 2));
    return result;
}

static uint32 bridge32_GpiQueryPS(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(SIZEL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_WRITE, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryPS((HPS)gpi_word(a, 0), (PSIZEL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiQueryPel(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryPel((HPS)gpi_word(a, 0), (PPOINTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiQueryPickAperturePosition(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_WRITE, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryPickAperturePosition((HPS)gpi_word(a, 0), (PPOINTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiQueryPickApertureSize(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(SIZEL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_WRITE, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryPickApertureSize((HPS)gpi_word(a, 0), (PSIZEL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiQueryRealColors(uint8 *a)
{
    LxGuestBorrow p4 = {0};
    uint64_t n4 = (uint64_t)gpi_word(a, 3) * 4;
    uint32 result = 0;
    if (n4 > BRIDGE_MAX_BYTES ||
        (n4 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), (size_t)n4,
                                    LX_GUEST_WRITE, &p4)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryRealColors((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1),
                                                   (LONG)gpi_word(a, 2), (LONG)gpi_word(a, 3),
                                                   (PLONG)p4.host);
done:
    lx_execution_release(&p4);
    return result;
}

static uint32 bridge32_GpiQueryViewingLimits(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(RECTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_WRITE, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryViewingLimits((HPS)gpi_word(a, 0), (PRECTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiRectInRegion(uint8 *a)
{
    LxGuestBorrow p2 = {0};
    uint64_t n2 = sizeof(RECTL);
    uint32 result = 0;
    if (n2 > BRIDGE_MAX_BYTES ||
        (n2 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)n2,
                                    LX_GUEST_READ, &p2)))
        goto done;
    result = (uint32)(uintptr_t)GpiRectInRegion((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1),
                                                (PRECTL)p2.host);
done:
    lx_execution_release(&p2);
    return result;
}

static uint32 bridge32_GpiResetBoundaryData(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiResetBoundaryData((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiResetPS(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiResetPS((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiSaveMetaFile(uint8 *a)
{
    char *s1 = lx_execution_copy_string(GProcessState.execution, gpi_word(a, 1), BRIDGE_MAX_PATH);
    uint32 result = 0;
    if (!s1)
        goto done;
    result = (uint32)(uintptr_t)GpiSaveMetaFile((ULONG)gpi_word(a, 0), s1);
done:
    free(s1);
    return result;
}

static uint32 bridge32_GpiSetArcParams(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = 4 * sizeof(LONG);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiSetArcParams((HPS)gpi_word(a, 0), (PLONG)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiSetGraphicsField(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(RECTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiSetGraphicsField((HPS)gpi_word(a, 0), (PRECTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiSetLineEnd(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiSetLineEnd((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiSetLineJoin(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiSetLineJoin((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiSetLineWidthGeom(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiSetLineWidthGeom((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiSetModelTransformMatrix(uint8 *a)
{
    LxGuestBorrow p2 = {0};
    uint64_t n2 = (uint64_t)gpi_word(a, 1) * 4;
    uint32 result = 0;
    if (n2 > BRIDGE_MAX_BYTES ||
        (n2 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)n2,
                                    LX_GUEST_READ, &p2)))
        goto done;
    result = (uint32)(uintptr_t)GpiSetModelTransformMatrix(
        (HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1), (PVOID)p2.host, (LONG)gpi_word(a, 3));
done:
    lx_execution_release(&p2);
    return result;
}

static uint32 bridge32_GpiSetPS(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(SIZEL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result =
        (uint32)(uintptr_t)GpiSetPS((HPS)gpi_word(a, 0), (PSIZEL)p1.host, (ULONG)gpi_word(a, 2));
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiSetPickAperturePosition(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiSetPickAperturePosition((HPS)gpi_word(a, 0), (PPOINTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiQueryStopDraw(uint8 *a)
{
    return (uint32)GpiQueryStopDraw((HPS)gpi_word(a, 0));
}

static uint32 bridge32_GpiSetStopDraw(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiSetStopDraw((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiSetViewingLimits(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(RECTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiSetViewingLimits((HPS)gpi_word(a, 0), (PRECTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiStrokePath(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiStrokePath((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                              (ULONG)gpi_word(a, 2));
    return result;
}

static uint32 bridge32_GpiPathToRegion(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiPathToRegion((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                                (LONG)gpi_word(a, 2));
    return result;
}

static uint32 bridge32_GpiTranslate(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = GPI_MATRIX_ELEMENTS * sizeof(LONG);
    LxGuestBorrow p3 = {0};
    uint64_t n3 = sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ | LX_GUEST_WRITE, &p1)) ||
        n3 > BRIDGE_MAX_BYTES ||
        (n3 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)n3,
                                    LX_GUEST_READ, &p3)))
        goto done;
    result = (uint32)(uintptr_t)GpiTranslate((HPS)gpi_word(a, 0), (PVOID)p1.host,
                                             (LONG)gpi_word(a, 2), (PPOINTL)p3.host);
done:
    lx_execution_release(&p1);
    lx_execution_release(&p3);
    return result;
}

static uint32 bridge32_GpiScale(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = GPI_MATRIX_ELEMENTS * sizeof(LONG);
    LxGuestBorrow p3 = {0};
    uint64_t n3 = 2 * sizeof(LONG);
    LxGuestBorrow p4 = {0};
    uint64_t n4 = sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ | LX_GUEST_WRITE, &p1)) ||
        n3 > BRIDGE_MAX_BYTES ||
        (n3 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)n3,
                                    LX_GUEST_READ, &p3)) ||
        n4 > BRIDGE_MAX_BYTES ||
        (n4 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), (size_t)n4,
                                    LX_GUEST_READ, &p4)))
        goto done;
    result = (uint32)(uintptr_t)GpiScale((HPS)gpi_word(a, 0), (PVOID)p1.host, (LONG)gpi_word(a, 2),
                                         (PLONG)p3.host, (PPOINTL)p4.host);
done:
    lx_execution_release(&p1);
    lx_execution_release(&p3);
    lx_execution_release(&p4);
    return result;
}

static uint32 bridge32_GpiRotate(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = GPI_MATRIX_ELEMENTS * sizeof(LONG);
    LxGuestBorrow p4 = {0};
    uint64_t n4 = sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ | LX_GUEST_WRITE, &p1)) ||
        n4 > BRIDGE_MAX_BYTES ||
        (n4 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), (size_t)n4,
                                    LX_GUEST_READ, &p4)))
        goto done;
    result = (uint32)(uintptr_t)GpiRotate((HPS)gpi_word(a, 0), (PVOID)p1.host, (LONG)gpi_word(a, 2),
                                          (LONG)gpi_word(a, 3), (PPOINTL)p4.host);
done:
    lx_execution_release(&p1);
    lx_execution_release(&p4);
    return result;
}

static uint32 bridge32_GpiQueryDefViewingLimits(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(RECTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_WRITE, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryDefViewingLimits((HPS)gpi_word(a, 0), (PRECTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiSetDefViewingLimits(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(RECTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiSetDefViewingLimits((HPS)gpi_word(a, 0), (PRECTL)p1.host);
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiDeletePalette(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiDeletePalette((ULONG)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiSelectPalette(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiSelectPalette((HPS)gpi_word(a, 0), (ULONG)gpi_word(a, 1));
    return result;
}

static uint32 bridge32_GpiQueryPalette(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiQueryPalette((HPS)gpi_word(a, 0));
    return result;
}

static uint32 bridge32_GpiQueryAttrs(uint8 *a)
{
    LxGuestBorrow p3 = {0};
    static const uint8 bundle[PRIM_IMAGE + 1] = {
        0, LINEBUNDLE_SIZE, CHARBUNDLE_SIZE, MARKERBUNDLE_SIZE, AREABUNDLE_SIZE, IMAGEBUNDLE_SIZE};
    uint32 primitive = gpi_word(a, 1);
    uint64_t n3 =
        gpi_word(a, 2) && gpi_word(a, 3) && primitive <= PRIM_IMAGE ? bundle[primitive] : 0;
    uint32 result = (uint32)GPI_ALTERROR;
    if (n3 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 3), (size_t)n3,
                                   LX_GUEST_READ | LX_GUEST_WRITE, &p3))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryAttrs((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                              (ULONG)gpi_word(a, 2), (PVOID)p3.host);
done:
    lx_execution_release(&p3);
    return result;
}

static uint32 bridge32_GpiSetPickApertureSize(uint8 *a)
{
    /* PICKAP_DEFAULT ignores the size pointer, which may be NULL. */
    LxGuestBorrow p2 = {0};
    uint64_t n2 = gpi_word(a, 1) == PICKAP_REC ? sizeof(SIZEL) : 0;
    uint32 result = 0;
    if (n2 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 2), (size_t)n2,
                                   LX_GUEST_READ, &p2))
        goto done;
    result = (uint32)(uintptr_t)GpiSetPickApertureSize((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1),
                                                       (PSIZEL)p2.host);
done:
    lx_execution_release(&p2);
    return result;
}

static uint32 bridge32_GpiCreatePalette(uint8 *a)
{
    LxGuestBorrow p4 = {0};
    uint64_t n4 = (uint64_t)gpi_word(a, 3) * 4;
    uint32 result = 0;
    if (n4 > BRIDGE_MAX_BYTES ||
        (n4 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 4), (size_t)n4,
                                    LX_GUEST_READ, &p4)))
        goto done;
    result = (uint32)(uintptr_t)GpiCreatePalette((HAB)gpi_word(a, 0), (ULONG)gpi_word(a, 1),
                                                 (ULONG)gpi_word(a, 2), (ULONG)gpi_word(a, 3),
                                                 (PULONG)p4.host);
done:
    lx_execution_release(&p4);
    return result;
}

static uint32 bridge32_GpiQueryPaletteInfo(uint8 *a)
{
    LxGuestBorrow p5 = {0};
    uint64_t n5 = (uint64_t)gpi_word(a, 4) * 4;
    uint32 result = 0;
    if (n5 > BRIDGE_MAX_BYTES ||
        (n5 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 5), (size_t)n5,
                                    LX_GUEST_WRITE, &p5)))
        goto done;
    result = (uint32)(uintptr_t)GpiQueryPaletteInfo((ULONG)gpi_word(a, 0), (HPS)gpi_word(a, 1),
                                                    (ULONG)gpi_word(a, 2), (ULONG)gpi_word(a, 3),
                                                    (ULONG)gpi_word(a, 4), (PULONG)p5.host);
done:
    lx_execution_release(&p5);
    return result;
}

static uint32 bridge32_GpiPartialArc(uint8 *a)
{
    LxGuestBorrow p1 = {0};
    uint64_t n1 = sizeof(POINTL);
    uint32 result = 0;
    if (n1 > BRIDGE_MAX_BYTES ||
        (n1 && !lx_execution_borrow(GProcessState.execution, gpi_word(a, 1), (size_t)n1,
                                    LX_GUEST_READ, &p1)))
        goto done;
    result = (uint32)(uintptr_t)GpiPartialArc((HPS)gpi_word(a, 0), (PPOINTL)p1.host,
                                              (LONG)gpi_word(a, 2), (LONG)gpi_word(a, 3),
                                              (LONG)gpi_word(a, 4));
done:
    lx_execution_release(&p1);
    return result;
}

static uint32 bridge32_GpiSetCharExtra(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)GpiSetCharExtra((HPS)gpi_word(a, 0), (LONG)gpi_word(a, 1));
    return result;
}

/* Guest POLYGON: ULONG ulPoints, PPOINTL aPointl. */
enum
{
    POLYGON_WIRE_SIZE = 8
};

static uint32 bridge32_GpiPolygons(uint8 *a)
{
    ULONG count = gpi_word(a, 1);
    if (count > BRIDGE_MAX_POINTS || !gpi_word(a, 2))
        return 0;
    LXPOLYGON *polygons = calloc(count ? count : 1, sizeof *polygons);
    LxGuestBorrow *borrows = calloc(count ? count : 1, sizeof *borrows);
    if (!polygons || !borrows)
    {
        free(polygons);
        free(borrows);
        return 0;
    }
    uint32 result = 0;
    size_t total = 0;
    for (ULONG i = 0; i < count; i++)
    {
        uint8 wire[POLYGON_WIRE_SIZE];
        uint64_t address = (uint64_t)gpi_word(a, 2) + i * POLYGON_WIRE_SIZE;
        if (address > UINT32_MAX ||
            !lx_execution_read(GProcessState.execution, (uint32)address, wire, sizeof wire))
            goto done;
        ULONG points = lx_guest_load_u32(wire), data = lx_guest_load_u32(wire + 4);
        if (points > BRIDGE_MAX_POINTS || total + points > BRIDGE_MAX_POINTS)
            goto done;
        total += points;
        if (points &&
            !lx_execution_borrow(GProcessState.execution, data, (size_t)points * sizeof(POINTL),
                                 LX_GUEST_READ, &borrows[i]))
            goto done;
        polygons[i].count = points;
        polygons[i].points = borrows[i].host;
    }
    result = (uint32)GpiPolygons(gpi_word(a, 0), count, polygons, gpi_word(a, 3), gpi_word(a, 4));
done:
    for (ULONG i = 0; i < count; i++)
        lx_execution_release(&borrows[i]);
    free(borrows);
    free(polygons);
    return result;
}

static uint32 bridge32_GpiLoadPublicFonts(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, gpi_word(a, 1), CCHMAXPATH + 1);
    BOOL result = name && GpiLoadPublicFonts((HAB)gpi_word(a, 0), name);
    free(name);
    return result;
}

static uint32 bridge32_GpiUnloadPublicFonts(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, gpi_word(a, 1), CCHMAXPATH + 1);
    BOOL result = name && GpiUnloadPublicFonts((HAB)gpi_word(a, 0), name);
    free(name);
    return result;
}
