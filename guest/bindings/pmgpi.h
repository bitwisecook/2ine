/* Public domain. No rights reserved. Wholly AI-authored. */
/* GPI structures are copied from checked guest spans before host drawing. */
static uint32 gpi_word(const uint8 *a, unsigned i)
{
    return lx_guest_load_u32(a + 4 * i);
}
static uint32 bridge32_GpiCreatePS(uint8 *a)
{
    SIZEL size;
    if (!lx_execution_read(GLoaderState.execution, gpi_word(a, 2), &size, sizeof size))
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
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &p, sizeof p)
               ? GpiMove(gpi_word(a, 0), &p)
               : 0;
}
static uint32 bridge32_GpiLine(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &p, sizeof p)
               ? GpiLine(gpi_word(a, 0), &p)
               : 0;
}
static uint32 bridge32_GpiSetPel(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &p, sizeof p)
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
    LONG count = (LONG)gpi_word(a, 1), output[4];
    if (count < 0 || count > 4)
        return 0;
    uint32 result = GpiQueryColorData(gpi_word(a, 0), count, output);
    if (result && count)
    {
        uint8 wire[16];
        for (LONG i = 0; i < count; i++)
            lx_guest_store_u32(wire + 4 * i, output[i]);
        result =
            lx_execution_write(GLoaderState.execution, gpi_word(a, 2), wire, (size_t)count * 4);
    }
    return result;
}

static uint32 bridge32_GpiQueryFonts(uint8 *a)
{
    const uint32 name_pointer = gpi_word(a, 2);
    char *face =
        name_pointer ? lx_execution_copy_string(GLoaderState.execution, name_pointer, 256) : NULL;
    LxGuestBorrow count = {0}, output = {0};
    uint32 result = (uint32)-255;
    if ((!name_pointer || face) && lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3), 4,
                                                       LX_GUEST_READ | LX_GUEST_WRITE, &count))
    {
        LONG requested = (LONG)lx_guest_load_u32(count.host), size = (LONG)gpi_word(a, 4);
        uint64 bytes = requested > 0 && size > 0 ? (uint64)(uint32)requested * (uint32)size : 0;
        if (requested >= 0 && size >= 0 && bytes <= UINT32_MAX &&
            (!requested || (size && lx_execution_borrow(GLoaderState.execution, gpi_word(a, 5),
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
    if (bytes && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2), bytes, LX_GUEST_WRITE,
                                      &output))
        return 0;
    uint32 result = GpiQueryFontMetrics(gpi_word(a, 0), size, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DevQueryCaps(uint8 *a)
{
    LONG start = (LONG)gpi_word(a, 1), count = (LONG)gpi_word(a, 2);
    if (start < 0 || count < 0 || start > 42 || count > 42 - start)
        return 0;
    LxGuestBorrow output = {0};
    if (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3), (size_t)count * 4,
                                      LX_GUEST_WRITE, &output))
        return 0;
    uint32 result = DevQueryCaps(gpi_word(a, 0), start, count, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GpiQueryTextBox(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1), points = (LONG)gpi_word(a, 3);
    if (count < 0 || count > 512 || points < 0)
        return 0;
    LxGuestBorrow text = {0}, output = {0};
    uint32 result = 0;
    size_t bytes = (size_t)(points < 5 ? points : 5) * sizeof(POINTL);
    if ((!count || lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2), (size_t)count,
                                       LX_GUEST_READ, &text)) &&
        (!bytes || lx_execution_borrow(GLoaderState.execution, gpi_word(a, 4), bytes,
                                       LX_GUEST_WRITE, &output)))
        result = GpiQueryTextBox(gpi_word(a, 0), count, text.host, points, output.host);
    lx_execution_release(&output);
    lx_execution_release(&text);
    return result;
}

static uint32 bridge32_DevOpenDC(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 3);
    if (count < 0 || count > 9)
        return 0;
    char *token = gpi_word(a, 2)
                      ? lx_execution_copy_string(GLoaderState.execution, gpi_word(a, 2), 256)
                      : NULL;
    if (gpi_word(a, 2) && !token)
        return 0;
    void *fields[9] = {0};
    uint8 pointers[36];
    uint32 result = 0;
    if (count &&
        !lx_execution_read(GLoaderState.execution, gpi_word(a, 4), pointers, (size_t)count * 4))
        goto done;
    for (LONG i = 0; i < count; i++)
    {
        uint32 address = lx_guest_load_u32(pointers + i * 4);
        if (i == 2)
            fields[i] = (void *)(uintptr_t)address;
        else if (address &&
                 !(fields[i] = lx_execution_copy_string(GLoaderState.execution, address, 32768)))
            goto done;
    }
    result = DevOpenDC(gpi_word(a, 0), (LONG)gpi_word(a, 1), token, count, fields, gpi_word(a, 5));
done:
    for (unsigned i = 0; i < 9; i++)
        if (i != 2)
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
    if (gpi_word(a, 2) == 3)
        return GpiCreateLogColorTable(gpi_word(a, 0), gpi_word(a, 1), 3, (LONG)gpi_word(a, 3),
                                      count, NULL);
    if (count < 0 || count > 256)
        return 0;
    LxGuestBorrow colors = {0};
    if (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 5), (size_t)count * 4,
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
    uint8 name[8];
    if (!lx_execution_read(GLoaderState.execution, gpi_word(a, 3), &attrs, sizeof attrs) ||
        (gpi_word(a, 1) && !lx_execution_read(GLoaderState.execution, gpi_word(a, 1), name, 8)))
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
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &box, sizeof box) &&
           GpiSetCharBox(gpi_word(a, 0), &box);
}
static uint32 bridge32_GpiQueryCharBox(uint8 *a)
{
    SIZEF box;
    return GpiQueryCharBox(gpi_word(a, 0), &box) &&
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), &box, sizeof box);
}

static uint32 bridge32_DevQueryDeviceNames(uint8 *a)
{
    char *driver = lx_execution_copy_string(GLoaderState.execution, gpi_word(a, 1), 256);
    LONG devices = 0, types = 0;
    uint32 result = 0;
    char name[32], description[64], type[16];
    if (driver && lx_execution_read(GLoaderState.execution, gpi_word(a, 2), &devices, 4) &&
        lx_execution_read(GLoaderState.execution, gpi_word(a, 5), &types, 4))
    {
        LONG dc = devices, tc = types;
        result = DevQueryDeviceNames(gpi_word(a, 0), driver, &devices, gpi_word(a, 3) ? name : NULL,
                                     gpi_word(a, 4) ? description : NULL, &types,
                                     gpi_word(a, 6) ? type : NULL);
        if (result &&
            ((dc &&
              (!lx_execution_write(GLoaderState.execution, gpi_word(a, 3), name, 32) ||
               !lx_execution_write(GLoaderState.execution, gpi_word(a, 4), description, 64))) ||
             (tc && !lx_execution_write(GLoaderState.execution, gpi_word(a, 6), type, 16))))
            result = 0;
        if (!lx_execution_write(GLoaderState.execution, gpi_word(a, 2), &devices, 4) ||
            !lx_execution_write(GLoaderState.execution, gpi_word(a, 5), &types, 4))
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
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &r, sizeof r) &&
           GpiSetPageViewport(gpi_word(a, 0), &r);
}
static uint32 bridge32_GpiQueryPageViewport(uint8 *a)
{
    RECTL r;
    return GpiQueryPageViewport(gpi_word(a, 0), &r) &&
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), &r, sizeof r);
}
static uint32 bridge32_GpiSetAttrs(uint8 *a)
{
    uint8 bundle[52] = {0};
    LONG primitive = (LONG)gpi_word(a, 1);
    ULONG mask = gpi_word(a, 2), defaults = gpi_word(a, 3);
    unsigned size = primitive == 2   ? 52
                    : primitive == 1 ? 28
                    : primitive == 3 ? 24
                    : primitive == 4 ? 24
                                     : 12;
    if ((mask & ~defaults) &&
        !lx_execution_read(GLoaderState.execution, gpi_word(a, 4), bundle, size))
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
    uint8 data[12];
    return GpiQueryBitmapParameters(gpi_word(a, 0), data) &&
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), data, 12);
}
static uint32 bridge32_GpiQueryBitmapInfoHeader(uint8 *a)
{
    uint8 data[64] = {0};
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), data, 4) &&
           GpiQueryBitmapInfoHeader(gpi_word(a, 0), data) &&
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), data, 64);
}
static uint32 bridge32_GpiQueryDeviceBitmapFormats(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    LxGuestBorrow out = {0};
    if (count < 0 || count > 65536 ||
        (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2), (size_t)count * 4,
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
    if ((count != 3 && count != 4) || !lx_execution_read(GLoaderState.execution, gpi_word(a, 3),
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
    uint8 header[64] = {0};
    if (!lx_execution_read(GLoaderState.execution, address, header, 4))
        return NULL;
    uint32 fixed = lx_guest_load_u32(header);
    if ((fixed != 12 && fixed != 64) ||
        !lx_execution_read(GLoaderState.execution, address, header, fixed))
        return NULL;
    unsigned width = fixed == 12 ? lx_guest_load_u16(header + 4) : lx_guest_load_u32(header + 4);
    unsigned height = fixed == 12 ? lx_guest_load_u16(header + 6) : lx_guest_load_u32(header + 8);
    unsigned planes = lx_guest_load_u16(header + (fixed == 12 ? 8 : 12));
    unsigned depth = lx_guest_load_u16(header + (fixed == 12 ? 10 : 14));
    if (!width || !height || width > 16384 || height > 16384 || (uint64)width * height > 16777216 ||
        planes != 1 || (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
        return NULL;
    unsigned colors = depth <= 8 ? 1u << depth : 0;
    if (fixed == 64 && lx_guest_load_u32(header + 32))
        colors = lx_guest_load_u32(header + 32);
    if (colors > 256)
        return NULL;
    size_t size = fixed + (palette ? colors * (fixed == 12 ? 3 : 4) : 0);
    uint8 *info = malloc(size);
    if (!info || !lx_execution_read(GLoaderState.execution, address, info, size))
    {
        free(info);
        return NULL;
    }
    *bytes = ((size_t)width * depth + 31) / 32 * 4 * height;
    return info;
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
    if (gpi_word(a, 2) & 4)
    {
        info = bitmap_bridge_info(gpi_word(a, 4), &initial_bytes, TRUE);
        if (!info || !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3), initial_bytes,
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
    if (count < 0 || count > 255)
        return 0;
    if (!count || (lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2), (size_t)count * 4,
                                       LX_GUEST_WRITE, &types) &&
                   lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3), (size_t)count * 8,
                                       LX_GUEST_WRITE, &names) &&
                   lx_execution_borrow(GLoaderState.execution, gpi_word(a, 4), (size_t)count * 4,
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
    if (count < 0 || count > 256 ||
        (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3), (size_t)count * 4,
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
    if (count < 0 || count > 65536 ||
        (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2), (size_t)count,
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
    if (count < 0 || count > 65536 ||
        !lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &point, sizeof point) ||
        (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3), (size_t)count,
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
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), &p, sizeof p);
}

static uint32 bridge32_GpiSetCurrentPosition(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &p, sizeof p) &&
           GpiSetCurrentPosition(gpi_word(a, 0), &p);
}

static uint32 bridge32_GpiCreateRegion(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    LxGuestBorrow rects = {0};
    if (count < 0 || count > 4096 ||
        (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2),
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
    if (count < 0 || count > 4096 ||
        (count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3),
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
        !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2), 4, LX_GUEST_WRITE, &out))
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
    return result && lx_execution_write(GLoaderState.execution, gpi_word(a, 2), &rect, sizeof rect)
               ? result
               : 0;
}
static uint32 bridge32_GpiQueryClipBox(uint8 *a)
{
    RECTL rect = {0};
    uint32 result = GpiQueryClipBox(gpi_word(a, 0), &rect);
    return result && lx_execution_write(GLoaderState.execution, gpi_word(a, 1), &rect, sizeof rect)
               ? result
               : 0;
}
static uint32 bridge32_GpiRectVisible(uint8 *a)
{
    RECTL rect;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &rect, sizeof rect)
               ? GpiRectVisible(gpi_word(a, 0), &rect)
               : 0;
}
static uint32 bridge32_GpiIntersectClipRectangle(uint8 *a)
{
    RECTL rect;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &rect, sizeof rect)
               ? GpiIntersectClipRectangle(gpi_word(a, 0), &rect)
               : 0;
}
static uint32 bridge32_GpiOffsetRegion(uint8 *a)
{
    POINTL point;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 2), &point, sizeof point)
               ? GpiOffsetRegion(gpi_word(a, 0), gpi_word(a, 1), &point)
               : 0;
}
static uint32 bridge32_GpiOffsetClipRegion(uint8 *a)
{
    POINTL point;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &point, sizeof point)
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
    if (count < 0 || count > 65536 ||
        (at && !lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &point, sizeof point)) ||
        (gpi_word(a, 1 + offset) &&
         !lx_execution_read(GLoaderState.execution, gpi_word(a, 1 + offset), &rect, sizeof rect)))
        return 0;
    if ((count && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 4 + offset),
                                       (size_t)count, LX_GUEST_READ, &text)) ||
        ((flags & 2) && count &&
         !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 5 + offset), (size_t)count * 4,
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
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 2), &point, sizeof point)
               ? GpiBox(gpi_word(a, 0), (LONG)gpi_word(a, 1), &point, (LONG)gpi_word(a, 3),
                        (LONG)gpi_word(a, 4))
               : 0;
}

static uint32 bridge32_DevQueryHardcopyCaps(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 2);
    if (count < 0 || (count && !gpi_word(a, 3)))
        return (uint32)-1;
    return (uint32)DevQueryHardcopyCaps(gpi_word(a, 0), (LONG)gpi_word(a, 1), count,
                                        (PVOID)(uintptr_t)gpi_word(a, 3));
}

static uint32 bridge32_GpiQueryColor(uint8 *a)
{
    return (uint32)GpiQueryColor(gpi_word(a, 0));
}
static uint32 bridge32_GpiQueryLogColorTable(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 3);
    if (count < 0)
        return UINT32_MAX;
    size_t copied = count > 512 ? 512 : (size_t)count;
    LxGuestBorrow output = {0};
    if (copied && !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 4), copied * 4,
                                       LX_GUEST_WRITE, &output))
        return UINT32_MAX;
    LONG values[512];
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
    if (count < 0 || count > 65536)
        return 0;
    size_t bytes = (size_t)count * sizeof(POINTL);
    POINTL *points = count ? malloc(bytes) : NULL;
    if (count &&
        (!points || !lx_execution_read(GLoaderState.execution, gpi_word(a, 4), points, bytes)))
    {
        free(points);
        return 0;
    }
    BOOL result =
        GpiConvert(gpi_word(a, 0), (LONG)gpi_word(a, 1), (LONG)gpi_word(a, 2), count, points);
    if (result && count)
        result = lx_execution_write(GLoaderState.execution, gpi_word(a, 4), points, bytes);
    free(points);
    return result;
}

static uint32 bridge32_GpiSetPatternRefPoint(uint8 *a)
{
    POINTL p;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &p, sizeof p)
               ? GpiSetPatternRefPoint(gpi_word(a, 0), &p)
               : 0;
}
static uint32 bridge32_GpiQueryPatternRefPoint(uint8 *a)
{
    POINTL p;
    return GpiQueryPatternRefPoint(gpi_word(a, 0), &p) &&
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), &p, sizeof p);
}

static uint32 bridge32_GpiPolyLine(uint8 *a)
{
    LONG count = (LONG)gpi_word(a, 1);
    if (count < 0 || count > 65536)
        return 0;
    POINTL *points = count ? malloc((size_t)count * sizeof *points) : NULL;
    if (count && (!points || !lx_execution_read(GLoaderState.execution, gpi_word(a, 2), points,
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
    if (!lx_execution_read(GLoaderState.execution, gpi_word(a, 3), &control, sizeof control) ||
        (gpi_word(a, 4) && control.crc > 65536))
        return 0;
    if (gpi_word(a, 2) &&
        !lx_execution_read(GLoaderState.execution, gpi_word(a, 2), &bounds, sizeof bounds))
        return 0;
    LxGuestBorrow control_output = {0}, rectangles = {0};
    if (!lx_execution_borrow(GLoaderState.execution, gpi_word(a, 3), sizeof control, LX_GUEST_WRITE,
                             &control_output))
        return 0;
    if (gpi_word(a, 4) &&
        (!control.crc ||
         !lx_execution_borrow(GLoaderState.execution, gpi_word(a, 4),
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
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), &angle, sizeof angle);
}

static uint32 bridge32_GpiQueryCharShear(uint8 *a)
{
    POINTL shear;
    return GpiQueryCharShear(gpi_word(a, 0), &shear) &&
           lx_execution_write(GLoaderState.execution, gpi_word(a, 1), &shear, sizeof shear);
}

static uint32 bridge32_GpiQueryTextAlignment(uint8 *a)
{
    LONG horizontal, vertical;
    LxGuestBorrow h = {0}, v = {0};
    uint32 result =
        lx_execution_borrow(GLoaderState.execution, gpi_word(a, 1), 4, LX_GUEST_WRITE, &h) &&
        lx_execution_borrow(GLoaderState.execution, gpi_word(a, 2), 4, LX_GUEST_WRITE, &v) &&
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
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &value, sizeof value) &&
           GpiSetCharAngle(gpi_word(a, 0), &value);
}

static uint32 bridge32_GpiSetCharShear(uint8 *a)
{
    POINTL value;
    return lx_execution_read(GLoaderState.execution, gpi_word(a, 1), &value, sizeof value) &&
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
