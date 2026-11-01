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
