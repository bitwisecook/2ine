/* Public domain. No rights reserved. Wholly AI-authored. */
/* DIVE pointer fields are 32-bit guest addresses even on a 64-bit host. */
static uint32 dive_word(const uint8 *a, unsigned word)
{
    return lx_guest_load_u32(a + word * 4);
}
static uint32 bridge32_DiveQueryCaps(uint8 *a)
{
    uint8 raw[56];
    if (!lx_execution_read(GLoaderState.execution, dive_word(a, 0), raw, sizeof raw))
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    if (lx_guest_load_u32(raw) < sizeof raw)
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    DIVE_CAPS caps = {0};
    memcpy(&caps, raw, 52);
    caps.ulStructLen = sizeof caps;
    LxGuestBorrow formats = {0};
    uint32 address = lx_guest_load_u32(raw + 52);
    if (caps.ulFormatLength && caps.ulFormatLength <= 4096 && address &&
        lx_execution_borrow(GLoaderState.execution, address, caps.ulFormatLength, LX_GUEST_WRITE,
                            &formats))
        caps.pFormatData = formats.host;
    ULONG result = DiveQueryCaps(&caps, dive_word(a, 1));
    memcpy(raw + 4, (uint8 *)&caps + 4, 48);
    if (!lx_execution_write(GLoaderState.execution, dive_word(a, 0), raw, sizeof raw))
        result = DIVE_ERR_INSUFFICIENT_LENGTH;
    lx_execution_release(&formats);
    return result;
}
static uint32 bridge32_DiveOpen(uint8 *a)
{
    LxGuestBorrow output = {0}, framebuffer = {0};
    if (!lx_execution_borrow(GLoaderState.execution, dive_word(a, 0), 4, LX_GUEST_WRITE, &output))
        return DIVE_ERR_INVALID_INSTANCE;
    if (dive_word(a, 2) && !lx_execution_borrow(GLoaderState.execution, dive_word(a, 2), 4,
                                                LX_GUEST_WRITE, &framebuffer))
    {
        lx_execution_release(&output);
        return DIVE_ERR_INVALID_INSTANCE;
    }
    ULONG handle = 0;
    ULONG result = DiveOpen(&handle, dive_word(a, 1), NULL);
    if (!result)
    {
        lx_guest_store_u32(output.host, handle);
        if (framebuffer.host)
            lx_guest_store_u32(framebuffer.host, 0);
    }
    lx_execution_release(&output);
    lx_execution_release(&framebuffer);
    return result;
}
static uint32 bridge32_DiveClose(uint8 *a)
{
    return DiveClose(dive_word(a, 0));
}
static uint32 bridge32_DiveSetupBlitter(uint8 *a)
{
    if (!dive_word(a, 1))
        return DiveSetupBlitter(dive_word(a, 0), NULL);
    uint8 raw[68];
    if (!lx_execution_read(GLoaderState.execution, dive_word(a, 1), raw, sizeof raw) ||
        lx_guest_load_u32(raw) < sizeof raw)
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    SETUP_BLITTER setup = {0};
    memcpy(&setup, raw, 64);
    setup.ulStructLen = sizeof setup;
    LxGuestBorrow rectangles = {0};
    if (setup.ulNumDstRects && setup.ulNumDstRects != DIVE_FULLY_VISIBLE)
    {
        if (setup.ulNumDstRects > 65536 ||
            !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(raw + 64),
                                 (size_t)setup.ulNumDstRects * sizeof(RECTL), LX_GUEST_READ,
                                 &rectangles))
            return DIVE_ERR_INVALID_DESTINATION_RECTL;
        setup.pVisDstRects = rectangles.host;
    }
    ULONG result = DiveSetupBlitter(dive_word(a, 0), &setup);
    lx_execution_release(&rectangles);
    return result;
}
static uint32 bridge32_DiveBlitImage(uint8 *a)
{
    return DiveBlitImage(dive_word(a, 0), dive_word(a, 1), dive_word(a, 2));
}
static uint32 dive_palette_bridge(uint8 *a, int source)
{
    uint32 address = dive_word(a, 3), count = dive_word(a, 2);
    LxGuestBorrow entries = {0};
    PBYTE pointer = (PBYTE)(uintptr_t)address;
    if (address && address != UINT32_MAX)
    {
        if (count > 256 || !lx_execution_borrow(GLoaderState.execution, address, count * 4,
                                                LX_GUEST_READ, &entries))
            return DIVE_ERR_INVALID_PALETTE;
        pointer = entries.host;
    }
    ULONG result =
        source ? DiveSetSourcePalette(dive_word(a, 0), dive_word(a, 1), count, pointer)
               : DiveSetDestinationPalette(dive_word(a, 0), dive_word(a, 1), count, pointer);
    lx_execution_release(&entries);
    return result;
}
static uint32 bridge32_DiveSetSourcePalette(uint8 *a)
{
    return dive_palette_bridge(a, 1);
}
static uint32 bridge32_DiveSetDestinationPalette(uint8 *a)
{
    return dive_palette_bridge(a, 0);
}
static uint32 bridge32_DiveAllocImageBuffer(uint8 *a)
{
    LxGuestBorrow number = {0}, pixels = {0};
    if (!lx_execution_borrow(GLoaderState.execution, dive_word(a, 1), 4,
                             LX_GUEST_READ | LX_GUEST_WRITE, &number))
        return DIVE_ERR_INVALID_BUFFER_NUMBER;
    ULONG value = lx_guest_load_u32(number.host);
    uint32 width = dive_word(a, 3), height = dive_word(a, 4), stride = dive_word(a, 5);
    uint64 bytes = (uint64)(stride ? stride : (width + 3ULL) & ~3ULL) * height;
    if (dive_word(a, 6) &&
        (bytes > UINT32_MAX || !bytes ||
         !lx_execution_borrow(GLoaderState.execution, dive_word(a, 6), (size_t)bytes,
                              LX_GUEST_READ | LX_GUEST_WRITE, &pixels)))
    {
        lx_execution_release(&number);
        return DIVE_ERR_INVALID_LINESIZE;
    }
    if (!dive_lock())
    {
        lx_execution_release(&pixels);
        lx_execution_release(&number);
        return DIVE_ERR_ALLOCATION_ERROR;
    }
    ULONG result = DiveAllocImageBuffer(dive_word(a, 0), &value, dive_word(a, 2), width, height,
                                        stride, pixels.host);
    if (!result)
    {
        lx_guest_store_u32(number.host, value);
        if (pixels.host)
        {
            DiveBuffer *buffer = dive_buffer(dive_instance(dive_word(a, 0)), value);
            buffer->borrow = pixels;
            memset(&pixels, 0, sizeof pixels);
        }
    }
    dive_unlock();
    lx_execution_release(&pixels);
    lx_execution_release(&number);
    return result;
}
static uint32 bridge32_DiveFreeImageBuffer(uint8 *a)
{
    return DiveFreeImageBuffer(dive_word(a, 0), dive_word(a, 1));
}
static uint32 bridge32_DiveBeginImageBufferAccess(uint8 *a)
{
    LxGuestBorrow outputs[3] = {{0}};
    for (unsigned i = 0; i < 3; i++)
        if (!lx_execution_borrow(GLoaderState.execution, dive_word(a, i + 2), 4, LX_GUEST_WRITE,
                                 &outputs[i]))
        {
            for (unsigned j = 0; j < 3; j++)
                lx_execution_release(&outputs[j]);
            return DIVE_ERR_INVALID_BUFFER_NUMBER;
        }
    PBYTE pixels = NULL;
    ULONG stride = 0, lines = 0;
    ULONG result =
        DiveBeginImageBufferAccess(dive_word(a, 0), dive_word(a, 1), &pixels, &stride, &lines);
    uint32 address = 0;
    if (!result && !lx_execution_address(GLoaderState.execution, pixels, (size_t)stride * lines,
                                         LX_GUEST_READ | LX_GUEST_WRITE, &address))
    {
        DiveEndImageBufferAccess(dive_word(a, 0), dive_word(a, 1));
        result = DIVE_ERR_INVALID_BUFFER_NUMBER;
    }
    if (!result)
    {
        lx_guest_store_u32(outputs[0].host, address);
        lx_guest_store_u32(outputs[1].host, stride);
        lx_guest_store_u32(outputs[2].host, lines);
    }
    for (unsigned i = 0; i < 3; i++)
        lx_execution_release(&outputs[i]);
    return result;
}
static uint32 bridge32_DiveEndImageBufferAccess(uint8 *a)
{
    return DiveEndImageBufferAccess(dive_word(a, 0), dive_word(a, 1));
}

static uint32 bridge32_DiveFullScreenInit(uint8 *a)
{
    DiveFullScreenInit((PVOID)(uintptr_t)dive_word(a, 0), (PVOID)(uintptr_t)dive_word(a, 1));
    return 0;
}
static uint32 bridge32_DiveFullScreenTerm(uint8 *a)
{
    (void)a;
    DiveFullScreenTerm();
    return 0;
}
