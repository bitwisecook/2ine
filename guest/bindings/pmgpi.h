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
