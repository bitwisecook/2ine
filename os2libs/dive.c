/* Public domain. No rights reserved. Wholly AI-authored. */
/* Indexed DIVE image buffers with checked access and software screen conversion. */
#include "os2libs/os2native.h"
#include "os2libs/dive.h"
#include "os2libs/dive_pixels.h"
#include "os2libs/dive_palette.h"
#include "os2libs/pmwin/graphics.h"
#include "host/window.h"
#include "host/sync.h"
#include "runtime/runtime_features.h"
#define DIVE_MAX_BUFFERS 256
#define DIVE_FIRST_BUFFER 16
#define DIVE_MAX_DIMENSION 16384

typedef struct DiveBuffer
{
    ULONG number, width, height, stride;
    DiveImage image;
    PBYTE pixels;
    BOOL associated, accessed;
    LxGuestMapping *mapping;
    LxGuestBorrow borrow;
} DiveBuffer;

typedef struct DiveInstance
{
    ULONG handle;
    BOOL setup;
    void *target;
    SETUP_BLITTER blitter;
    RECTL *rectangles;
    uint32_t source_palette[4096], destination_palette[4096];
    BOOL non_screen, acquired;
    ULONG transparent, transparent_first, transparent_last;
    DiveBuffer framebuffer;
    RECTL acquired_rect;
    void *framebuffer_target;
    DiveBuffer buffers[DIVE_MAX_BUFFERS];
} DiveInstance;

static DiveInstance *instances[DIVE_MAX_INSTANCES];
static ULONG next_handle = 1;
static DiveInstance *framebuffer_owner;
static LxHostMutex *dive_mutex;
static LxHostOnce dive_once = LX_HOST_ONCE_INIT;

static void initialize_dive_lock(void)
{
    dive_mutex = lx_host_mutex_create(1, 0);
}

static BOOL dive_lock(void)
{
    lx_host_once(&dive_once, initialize_dive_lock);
    return dive_mutex && lx_host_mutex_lock(dive_mutex, UINT32_MAX) == LX_HOST_LOCK_OK;
}

static void dive_unlock(void)
{
    lx_host_mutex_unlock(dive_mutex);
}

static DiveInstance *dive_instance(ULONG handle)
{
    for (unsigned i = 0; i < DIVE_MAX_INSTANCES; i++)
        if (instances[i] && instances[i]->handle == handle)
            return instances[i];
    return NULL;
}

static DiveBuffer *dive_buffer(DiveInstance *instance, ULONG number)
{
    if (number < DIVE_FIRST_BUFFER || number >= DIVE_FIRST_BUFFER + DIVE_MAX_BUFFERS)
        return NULL;
    DiveBuffer *buffer = &instance->buffers[number - DIVE_FIRST_BUFFER];
    return buffer->pixels ? buffer : NULL;
}

static void dive_free_buffer(DiveBuffer *buffer)
{
    lx_execution_release(&buffer->borrow);
    if (buffer->mapping)
        lx_execution_unmap(GLoaderState.execution, buffer->mapping);
    else if (!buffer->associated)
        free(buffer->pixels);
    memset(buffer, 0, sizeof *buffer);
}

static BOOL supports_format(const uint32_t *formats, size_t count, ULONG format)
{
    for (size_t i = 0; i < count; i++)
        if (formats[i] == format)
            return TRUE;
    return FALSE;
}

static void default_palette(uint32_t *palette, BOOL physical)
{
    for (unsigned i = 0; i < 4096; i++)
        palette[i] = ((i >> 8) & 15) * 17 << 16 | ((i >> 4) & 15) * 17 << 8 | (i & 15) * 17;
    memcpy(palette, DiveDefaultPalette, sizeof DiveDefaultPalette);
    if (physical)
    {
        HPS ps = WinGetScreenPS(HWND_DESKTOP);
        if (ps)
        {
            for (unsigned i = 0; i < 256; i++)
            {
                LONG color = lx_pm_resolve_color(ps, i);
                if (color >= 0)
                    palette[i] = (uint32_t)color;
            }
            WinReleasePS(ps);
        }
    }
}

static ULONG impl_DiveQueryCaps(PDIVE_CAPS caps, ULONG plane)
{
    TRACE_NATIVE("DiveQueryCaps(%p, %u)", caps, (uint)plane);
    if (!caps || caps->ulStructLen < sizeof *caps)
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    if (plane != DIVE_BUFFER_SCREEN)
        return DIVE_ERR_DESTINATION_FORMAT;
    ULONG length = caps->ulFormatLength;
    PVOID formats = caps->pFormatData;
    int width = 0, height = 0;
    if (!lx_host_video_initialize(&width, &height))
        return DIVE_ERR_NO_DIRECT_ACCESS;
    caps->ulPlaneCount = 1;
    caps->fScreenDirect = TRUE;
    caps->fBankSwitched = FALSE;
    caps->ulDepth = 32;
    caps->ulHorizontalResolution = width;
    caps->ulVerticalResolution = height;
    caps->ulScanLineBytes = (ULONG)width * 4;
    caps->fccColorEncoding = DIVE_FOURCC_BGR4;
    caps->ulApertureSize = (ULONG)width * height * 4;
    caps->ulInputFormats = DiveInputFormatCount;
    caps->ulOutputFormats = DiveOutputFormatCount;
    caps->ulFormatLength = (DiveInputFormatCount + DiveOutputFormatCount) * 4;
    lx_host_video_quit();
    if (length < caps->ulFormatLength || !formats)
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    memcpy(formats, DiveInputFormats, DiveInputFormatCount * 4);
    memcpy((uint8_t *)formats + DiveInputFormatCount * 4, DiveOutputFormats,
           DiveOutputFormatCount * 4);
    return DIVE_SUCCESS;
}

static ULONG impl_DiveOpen(PULONG handle, ULONG non_screen, PVOID framebuffer)
{
    TRACE_NATIVE("DiveOpen(%p, %u, %p)", handle, (uint)non_screen, framebuffer);
    if (!handle)
        return DIVE_ERR_INVALID_INSTANCE;
    for (unsigned i = 0; i < DIVE_MAX_INSTANCES; i++)
        if (!instances[i])
        {
            DiveInstance *instance = calloc(1, sizeof *instance);
            if (!instance)
                return DIVE_ERR_ALLOCATION_ERROR;
            if (!next_handle)
            {
                free(instance);
                return DIVE_ERR_TOO_MANY_INSTANCES;
            }
            instance->non_screen = non_screen != 0;
            default_palette(instance->source_palette, FALSE);
            memcpy(instance->destination_palette, instance->source_palette,
                   sizeof instance->source_palette);
            if (!non_screen)
            {
                int width = 0, height = 0;
                if (!lx_host_video_initialize(&width, &height))
                {
                    free(instance);
                    return DIVE_ERR_NO_DIRECT_ACCESS;
                }
                lx_host_video_quit();
                DiveBuffer *fb = &instance->framebuffer;
                if (!dive_image_layout(&fb->image, DIVE_FOURCC_BGR4, width, height, 0))
                {
                    free(instance);
                    return DIVE_ERR_ALLOCATION_ERROR;
                }
                fb->width = width;
                fb->height = height;
                fb->stride = fb->image.stride;
                if (GLoaderState.execution)
                {
                    fb->mapping = lx_execution_map(GLoaderState.execution, 0,
                                                   (fb->image.bytes + 4095) & ~(size_t)4095,
                                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
                    fb->pixels = fb->mapping ? lx_execution_mapping_host(fb->mapping) : NULL;
                }
                else
                    fb->pixels = calloc(1, fb->image.bytes);
                if (!fb->pixels)
                {
                    free(instance);
                    return DIVE_ERR_ALLOCATION_ERROR;
                }
                fb->image.pixels = fb->pixels;
            }
            instance->handle = next_handle++;
            instances[i] = instance;
            *handle = instance->handle;
            if (framebuffer)
                *(PVOID *)framebuffer = instance->framebuffer.pixels;
            return DIVE_SUCCESS;
        }
    return DIVE_ERR_TOO_MANY_INSTANCES;
}

static ULONG impl_DiveClose(ULONG handle)
{
    TRACE_NATIVE("DiveClose(%u)", (uint)handle);
    for (unsigned i = 0; i < DIVE_MAX_INSTANCES; i++)
        if (instances[i] && instances[i]->handle == handle)
        {
            DiveInstance *instance = instances[i];
            for (unsigned j = 0; j < DIVE_MAX_BUFFERS; j++)
                if (instance->buffers[j].pixels)
                    dive_free_buffer(&instance->buffers[j]);
            if (instance->target)
                lx_pm_dive_flush(instance->target);
            if (instance->framebuffer_target)
                lx_pm_dive_flush(instance->framebuffer_target);
            lx_pm_dive_release(instance->target);
            lx_pm_dive_release(instance->framebuffer_target);
            if (framebuffer_owner == instance)
                framebuffer_owner = NULL;
            dive_free_buffer(&instance->framebuffer);
            free(instance->rectangles);
            free(instance);
            instances[i] = NULL;
            return DIVE_SUCCESS;
        }
    return DIVE_ERR_INVALID_INSTANCE;
}

static ULONG impl_DiveSetupBlitter(ULONG handle, PSETUP_BLITTER setup)
{
    TRACE_NATIVE("DiveSetupBlitter(%u, %p)", (uint)handle, setup);
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    if (!setup)
    {
        instance->setup = FALSE;
        if (instance->target)
            lx_pm_dive_flush(instance->target);
        lx_pm_dive_release(instance->target);
        instance->target = NULL;
        free(instance->rectangles);
        instance->rectangles = NULL;
        return DIVE_SUCCESS;
    }
    ULONG length = setup->ulStructLen;
    if (length != 8 && length != 28 && length != 32 && length != 52 && length != 60 &&
        length != 68 && length != sizeof *setup)
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    SETUP_BLITTER next = instance->setup ? instance->blitter : (SETUP_BLITTER){0};
    next.pVisDstRects = NULL;
    memcpy(&next, setup, length);
    next.ulStructLen = sizeof next;
    setup = &next;
    if (!setup->fccSrcColorFormat)
        setup->fccSrcColorFormat = DIVE_FOURCC_BGR4;
    if (length < 52 && !instance->setup)
    {
        setup->ulDstWidth = setup->ulSrcWidth;
        setup->ulDstHeight = setup->ulSrcHeight;
    }
    if (length < 68)
    {
        setup->ulNumDstRects = DIVE_FULLY_VISIBLE;
        setup->pVisDstRects = NULL;
    }
    if (!supports_format(DiveInputFormats, DiveInputFormatCount, setup->fccSrcColorFormat))
        return DIVE_ERR_SOURCE_FORMAT;
    if (setup->fccDstColorFormat &&
        !supports_format(DiveOutputFormats, DiveOutputFormatCount, setup->fccDstColorFormat))
        return DIVE_ERR_DESTINATION_FORMAT;
    if (!setup->ulSrcWidth || !setup->ulSrcHeight || !setup->ulDstWidth || !setup->ulDstHeight ||
        setup->ulDstWidth > DIVE_MAX_DIMENSION || setup->ulDstHeight > DIVE_MAX_DIMENSION)
        return DIVE_ERR_OUT_OF_RANGE;
    RECTL *rects = NULL;
    if (setup->ulNumDstRects && setup->ulNumDstRects != DIVE_FULLY_VISIBLE)
    {
        if (setup->ulNumDstRects > 65536 || !setup->pVisDstRects)
            return DIVE_ERR_INVALID_DESTINATION_RECTL;
        rects = malloc((size_t)setup->ulNumDstRects * sizeof *rects);
        if (!rects)
            return DIVE_ERR_ALLOCATION_ERROR;
        memcpy(rects, setup->pVisDstRects, (size_t)setup->ulNumDstRects * sizeof *rects);
    }
    int64_t x = (int64_t)setup->lScreenPosX + setup->lDstPosX;
    int64_t y = (int64_t)setup->lScreenPosY + setup->lDstPosY;
    void *target = !instance->non_screen && x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN &&
                           y <= INT32_MAX
                       ? lx_pm_dive_target((LONG)x, (LONG)y, setup->ulDstWidth, setup->ulDstHeight)
                       : NULL;
    if (instance->target)
        lx_pm_dive_flush(instance->target);
    lx_pm_dive_release(instance->target);
    instance->target = target;
    free(instance->rectangles);
    instance->rectangles = rects;
    instance->blitter = *setup;
    instance->blitter.pVisDstRects = rects;
    instance->setup = TRUE;
    return DIVE_SUCCESS;
}

static ULONG impl_DiveAllocImageBuffer(ULONG handle, PULONG number, ULONG format, ULONG width,
                                       ULONG height, ULONG stride, PBYTE pixels)
{
    TRACE_NATIVE("DiveAllocImageBuffer(%u, %p, %x, %u, %u, %u, %p)", (uint)handle, number,
                 (uint)format, (uint)width, (uint)height, (uint)stride, pixels);
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    if (!number || !width || !height || width > DIVE_MAX_DIMENSION || height > DIVE_MAX_DIMENSION)
        return DIVE_ERR_OUT_OF_RANGE;
    if (!format)
        format = DIVE_FOURCC_BGR4;
    if (!supports_format(DiveInputFormats, DiveInputFormatCount, format))
        return DIVE_ERR_SOURCE_FORMAT;
    DiveImage image;
    if (!dive_image_layout(&image, format, width, height, stride))
        return DIVE_ERR_INVALID_LINESIZE;
    stride = image.stride;
    DiveBuffer *buffer = pixels && *number ? dive_buffer(instance, *number) : NULL;
    if (pixels && *number && !buffer)
        return DIVE_ERR_INVALID_BUFFER_NUMBER;
    if (buffer && buffer->accessed)
        return DIVE_ERR_BUFFER_ALREADY_ACCESSED;
    if (!buffer)
        for (unsigned i = 0; i < DIVE_MAX_BUFFERS; i++)
            if (!instance->buffers[i].pixels)
            {
                buffer = &instance->buffers[i];
                *number = i + DIVE_FIRST_BUFFER;
                break;
            }
    if (!buffer)
        return DIVE_ERR_TOO_MANY_BUFFERS;
    BOOL associated = pixels != NULL;
    LxGuestMapping *mapping = NULL;
    if (!pixels && GLoaderState.execution)
    {
        mapping = lx_execution_map(GLoaderState.execution, 0, (image.bytes + 4095) & ~(size_t)4095,
                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
        pixels = mapping ? lx_execution_mapping_host(mapping) : NULL;
    }
    else if (!pixels)
        pixels = calloc(1, image.bytes);
    if (!pixels)
        return DIVE_ERR_ALLOCATION_ERROR;
    /* Existing associations are replaced only after allocation has succeeded. */
    if (buffer->pixels)
        dive_free_buffer(buffer);
    buffer->number = *number;
    buffer->width = width;
    buffer->height = height;
    buffer->stride = stride;
    buffer->pixels = pixels;
    buffer->mapping = mapping;
    buffer->associated = associated;
    buffer->image = image;
    buffer->image.pixels = pixels;
    return DIVE_SUCCESS;
}

static ULONG impl_DiveFreeImageBuffer(ULONG handle, ULONG number)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    DiveBuffer *buffer = dive_buffer(instance, number);
    if (!buffer)
        return DIVE_ERR_INVALID_BUFFER_NUMBER;
    if (buffer->accessed)
        return DIVE_ERR_BUFFER_ALREADY_ACCESSED;
    dive_free_buffer(buffer);
    return DIVE_SUCCESS;
}

static ULONG impl_DiveBeginImageBufferAccess(ULONG handle, ULONG number, PVOID output,
                                             PULONG stride, PULONG lines)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    DiveBuffer *buffer = dive_buffer(instance, number);
    if (!buffer || !output || !stride || !lines)
        return DIVE_ERR_INVALID_BUFFER_NUMBER;
    if (buffer->accessed)
        return DIVE_ERR_BUFFER_ALREADY_ACCESSED;
    buffer->accessed = TRUE;
    *(PBYTE *)output = buffer->pixels;
    *stride = buffer->stride;
    *lines = buffer->height;
    return DIVE_SUCCESS;
}

static ULONG impl_DiveEndImageBufferAccess(ULONG handle, ULONG number)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    DiveBuffer *buffer = dive_buffer(instance, number);
    if (!buffer)
        return DIVE_ERR_INVALID_BUFFER_NUMBER;
    if (!buffer->accessed)
        return DIVE_ERR_BUFFER_NOT_ACCESSED;
    buffer->accessed = FALSE;
    return DIVE_SUCCESS;
}

static ULONG set_palette(ULONG handle, ULONG start, ULONG count, PBYTE entries, BOOL source)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    unsigned limit = 256;
    if (instance->setup && (source ? instance->blitter.fccSrcColorFormat
                                   : instance->blitter.fccDstColorFormat) == DIVE_FOURCC_LT12)
        limit = 4096;
    if (start >= limit || count > limit - start)
        return DIVE_ERR_INVALID_PALETTE;
    if (!count)
        count = limit - start;
    uint32_t *palette = source ? instance->source_palette : instance->destination_palette;
    if (entries && entries != (PBYTE)(uintptr_t)UINT32_MAX)
        for (ULONG i = 0; i < count; i++)
            palette[start + i] =
                (ULONG)entries[4 * i + 2] << 16 | (ULONG)entries[4 * i + 1] << 8 | entries[4 * i];
    else
    {
        uint32_t defaults[4096];
        default_palette(defaults, entries == NULL);
        memcpy(palette + start, defaults + start, (size_t)count * 4);
    }
    return DIVE_SUCCESS;
}

static ULONG impl_DiveSetSourcePalette(ULONG handle, ULONG start, ULONG count, PBYTE entries)
{
    return set_palette(handle, start, count, entries, TRUE);
}

static ULONG impl_DiveSetDestinationPalette(ULONG handle, ULONG start, ULONG count, PBYTE entries)
{
    return set_palette(handle, start, count, entries, FALSE);
}

static BOOL transparent_pixel(DiveInstance *instance, const DiveImage *image, ULONG x, ULONG y,
                              uint32_t rgb)
{
    ULONG mode = instance->transparent;
    if (!mode)
        return FALSE;
    if (mode == DIVE_TBM_EXCLUDE_SOURCE_VALUE)
    {
        ULONG value_mask = image->format == DIVE_FOURCC_LUT8   ? 255
                           : image->format == DIVE_FOURCC_LT12 ? 4095
                           : image->format == DIVE_FOURCC_MONO ? 1
                           : image->format == DIVE_FOURCC_VGA  ? 15
                                                               : 0xffffff;
        return dive_image_value(image, x, y) == (instance->transparent_first & value_mask);
    }
    ULONG value =
        mode >= DIVE_TBM_EXCLUDE_SOURCE_YUV_RANGE ? dive_image_yuv(image, x, y, rgb) : rgb;
    BOOL inside = TRUE;
    for (unsigned shift = 0; shift <= 16; shift += 8)
    {
        ULONG c = (value >> shift) & 255;
        inside = inside && c >= ((instance->transparent_first >> shift) & 255) &&
                 c <= ((instance->transparent_last >> shift) & 255);
    }
    return mode == DIVE_TBM_EXCLUDE_SOURCE_RGB_RANGE || mode == DIVE_TBM_EXCLUDE_SOURCE_YUV_RANGE
               ? inside
               : !inside;
}

static ULONG impl_DiveBlitImageLines(ULONG handle, ULONG source, ULONG destination, PBYTE lines)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    if (!instance->setup)
        return DIVE_ERR_BLITTER_NOT_SETUP;
    DiveBuffer *buffer = source <= DIVE_BUFFER_GRAPHICS_PLANE && !instance->non_screen
                             ? &instance->framebuffer
                             : dive_buffer(instance, source);
    if (!buffer || !buffer->pixels)
        return DIVE_ERR_INVALID_BUFFER_NUMBER;
    if (buffer->accessed || (buffer == &instance->framebuffer && !instance->acquired))
        return DIVE_ERR_BUFFER_ALREADY_ACCESSED;
    BOOL screen = destination <= DIVE_BUFFER_GRAPHICS_PLANE;
    DiveBuffer *output = screen ? NULL : dive_buffer(instance, destination);
    if (screen && instance->non_screen)
        return DIVE_ERR_NO_DIRECT_ACCESS;
    if (!screen && !output)
        return destination == DIVE_BUFFER_ALTERNATE_PLANE ? DIVE_ERR_DESTINATION_FORMAT
                                                          : DIVE_ERR_INVALID_BUFFER_NUMBER;
    if (output && output->accessed)
        return DIVE_ERR_BUFFER_ALREADY_ACCESSED;
    SETUP_BLITTER *setup = &instance->blitter;
    if (setup->fccSrcColorFormat != buffer->image.format)
        return DIVE_ERR_SOURCE_FORMAT;
    if (setup->ulSrcPosX > buffer->width || setup->ulSrcWidth > buffer->width - setup->ulSrcPosX ||
        setup->ulSrcPosY > buffer->height || setup->ulSrcHeight > buffer->height - setup->ulSrcPosY)
        return DIVE_ERR_OUT_OF_RANGE;
    if (output && (setup->fccDstColorFormat && setup->fccDstColorFormat != output->image.format))
        return DIVE_ERR_DESTINATION_FORMAT;
    if (output && !supports_format(DiveOutputFormats, DiveOutputFormatCount, output->image.format))
        return DIVE_ERR_INVALID_CONVERSION;
    if (screen && setup->fccDstColorFormat && setup->fccDstColorFormat != DIVE_FOURCC_BGR4)
        return DIVE_ERR_DESTINATION_FORMAT;
    if (output && (setup->lDstPosX < 0 || setup->lDstPosY < 0 ||
                   (uint64_t)setup->lDstPosX + setup->ulDstWidth > output->width ||
                   (uint64_t)setup->lDstPosY + setup->ulDstHeight > output->height))
        return DIVE_ERR_OUT_OF_RANGE;
    ULONG width = setup->ulDstWidth, height = setup->ulDstHeight;
    if ((uint64_t)width * height > 16777216)
        return DIVE_ERR_OUT_OF_RANGE;
    uint32_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels)
        return DIVE_ERR_ALLOCATION_ERROR;
    /* Read all input before writing, including self-blits with overlapping rectangles. */
    for (ULONG y = 0; y < height; y++)
    {
        ULONG sy = (ULONG)((uint64_t)y * setup->ulSrcHeight / height);
        if (!(setup->fInvert & 1))
            sy = setup->ulSrcHeight - 1 - sy;
        sy += setup->ulSrcPosY;
        for (ULONG x = 0; x < width; x++)
        {
            ULONG sx = (ULONG)((uint64_t)x * setup->ulSrcWidth / width);
            if (setup->fInvert & 2)
                sx = setup->ulSrcWidth - 1 - sx;
            sx += setup->ulSrcPosX;
            uint32_t rgb = dive_image_rgb(&buffer->image, sx, sy, instance->source_palette);
            pixels[(size_t)y * width + x] =
                (lines && !lines[sy]) || transparent_pixel(instance, &buffer->image, sx, sy, rgb)
                    ? 0x01000000u
                    : rgb;
        }
    }
    BOOL ok = TRUE;
    if (screen)
    {
        if (!instance->target)
        {
            int64_t x = (int64_t)setup->lScreenPosX + setup->lDstPosX;
            int64_t y = (int64_t)setup->lScreenPosY + setup->lDstPosY;
            if (x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN && y <= INT32_MAX)
                instance->target = lx_pm_dive_target((LONG)x, (LONG)y, width, height);
        }
        ok = instance->target &&
             lx_pm_dive_blit(instance->target, setup->lScreenPosX + setup->lDstPosX,
                             setup->lScreenPosY + setup->lDstPosY, width, height, pixels,
                             setup->ulNumDstRects, setup->pVisDstRects);
    }
    else
        for (ULONG y = 0; y < height; y++)
            for (ULONG x = 0; x < width; x++)
                if (!(pixels[(size_t)y * width + x] & 0x01000000u))
                    dive_image_store(&output->image, x + setup->lDstPosX, y + setup->lDstPosY,
                                     pixels[(size_t)y * width + x], instance->destination_palette,
                                     setup->ulDitherType == 1);
    free(pixels);
    return ok ? DIVE_SUCCESS : DIVE_ERR_INVALID_DESTINATION_RECTL;
}

static ULONG impl_DiveBlitImage(ULONG handle, ULONG source, ULONG destination)
{
    return impl_DiveBlitImageLines(handle, source, destination, NULL);
}

static ULONG impl_DiveSetTransparentBlitMode(ULONG handle, ULONG mode, ULONG first, ULONG last)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    if (mode > DIVE_TBM_INCLUDE_SOURCE_YUV_RANGE)
        return DIVE_ERR_OUT_OF_RANGE;
    if (mode >= DIVE_TBM_EXCLUDE_SOURCE_RGB_RANGE)
        for (unsigned shift = 0; shift <= 16; shift += 8)
            if (((first >> shift) & 255) > ((last >> shift) & 255))
                return DIVE_ERR_OUT_OF_RANGE;
    instance->transparent = mode;
    instance->transparent_first = first & 0xffffff;
    instance->transparent_last = last & 0xffffff;
    return DIVE_SUCCESS;
}

static ULONG framebuffer_rect(DiveInstance *instance, PRECTL rect)
{
    if (instance->non_screen || !instance->framebuffer.pixels)
        return DIVE_ERR_NO_DIRECT_ACCESS;
    if (!rect || rect->xLeft < 0 || rect->yBottom < 0 || rect->xRight <= rect->xLeft ||
        rect->yTop <= rect->yBottom || (ULONG)rect->xRight > instance->framebuffer.width ||
        (ULONG)rect->yTop > instance->framebuffer.height)
        return DIVE_ERR_INVALID_DESTINATION_RECTL;
    return DIVE_SUCCESS;
}

static ULONG impl_DiveAcquireFrameBuffer(ULONG handle, PRECTL rect)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    ULONG result = framebuffer_rect(instance, rect);
    if (result)
        return result;
    if (instance->acquired)
        return DIVE_ERR_FB_ALREADY_ACQUIRED;
    if (framebuffer_owner)
        return DIVE_ERR_ACQUIRE_FAILED;
    if (instance->framebuffer_target)
    {
        if (!lx_pm_dive_flush(instance->framebuffer_target))
            return DIVE_ERR_ACQUIRE_FAILED;
        lx_pm_dive_release(instance->framebuffer_target);
        instance->framebuffer_target = NULL;
    }
    unsigned width = rect->xRight - rect->xLeft, height = rect->yTop - rect->yBottom;
    void *target = lx_pm_dive_target(rect->xLeft, rect->yBottom, width, height);
    uint32_t *pixels = target ? malloc((size_t)width * height * 4) : NULL;
    if (!pixels || !lx_pm_dive_read(target, rect->xLeft, rect->yBottom, width, height, pixels))
    {
        free(pixels);
        lx_pm_dive_release(target);
        return DIVE_ERR_ACQUIRE_FAILED;
    }
    DiveImage *fb = &instance->framebuffer.image;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
            dive_image_store(fb, x + rect->xLeft, fb->height - 1 - (y + rect->yBottom),
                             pixels[(size_t)y * width + x], NULL, 0);
    free(pixels);
    instance->acquired_rect = *rect;
    instance->framebuffer_target = target;
    instance->acquired = TRUE;
    framebuffer_owner = instance;
    return DIVE_SUCCESS;
}

static ULONG impl_DiveDeacquireFrameBuffer(ULONG handle)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    if (!instance->acquired)
        return DIVE_ERR_FB_NOT_ACQUIRED;
    RECTL rect = instance->acquired_rect;
    unsigned width = rect.xRight - rect.xLeft, height = rect.yTop - rect.yBottom;
    uint32_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels)
        return DIVE_ERR_ALLOCATION_ERROR;
    DiveImage *fb = &instance->framebuffer.image;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
            pixels[(size_t)y * width + x] =
                dive_image_rgb(fb, x + rect.xLeft, fb->height - 1 - (y + rect.yBottom), NULL);
    BOOL ok = lx_pm_dive_blit(instance->framebuffer_target, rect.xLeft, rect.yBottom, width, height,
                              pixels, DIVE_FULLY_VISIBLE, NULL);
    free(pixels);
    if (!ok)
        return DIVE_ERR_INVALID_DESTINATION_RECTL;
    /* The PM queue owns its frame until consumed; retain this target for pumping. */
    instance->acquired = FALSE;
    framebuffer_owner = NULL;
    return DIVE_SUCCESS;
}

static ULONG impl_DiveCalcFrameBufferAddress(ULONG handle, PRECTL rect, PVOID output, PULONG bank,
                                             PULONG lines)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    ULONG result = framebuffer_rect(instance, rect);
    if (result)
        return result;
    if (!output || !bank || !lines)
        return DIVE_ERR_OUT_OF_RANGE;
    DiveBuffer *fb = &instance->framebuffer;
    *(PBYTE *)output =
        fb->pixels + (size_t)(fb->height - rect->yTop) * fb->stride + rect->xLeft * 4;
    *bank = 0;
    *lines = rect->yTop - rect->yBottom;
    return DIVE_SUCCESS;
}

/* Serialize instance/buffer ownership with setup and worker submissions. */
ULONG DiveQueryCaps(PDIVE_CAPS caps, ULONG plane)
{
    if (!lxRuntimeFeatureEnabled(LX_FEATURE_DIVE))
        return DIVE_ERR_NO_DIRECT_ACCESS;
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveQueryCaps(caps, plane);
    dive_unlock();
    return result;
}

ULONG DiveOpen(PULONG handle, ULONG non_screen, PVOID framebuffer)
{
    if (!lxRuntimeFeatureEnabled(LX_FEATURE_DIVE))
        return DIVE_ERR_NO_DIRECT_ACCESS;
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveOpen(handle, non_screen, framebuffer);
    dive_unlock();
    return result;
}

ULONG DiveClose(ULONG handle)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveClose(handle);
    dive_unlock();
    return result;
}

ULONG DiveSetupBlitter(ULONG handle, PSETUP_BLITTER setup)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveSetupBlitter(handle, setup);
    dive_unlock();
    return result;
}

ULONG DiveAllocImageBuffer(ULONG handle, PULONG number, ULONG format, ULONG width, ULONG height,
                           ULONG stride, PBYTE pixels)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveAllocImageBuffer(handle, number, format, width, height, stride, pixels);
    dive_unlock();
    return result;
}

ULONG DiveFreeImageBuffer(ULONG handle, ULONG number)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveFreeImageBuffer(handle, number);
    dive_unlock();
    return result;
}

ULONG DiveBeginImageBufferAccess(ULONG handle, ULONG number, PVOID output, PULONG stride,
                                 PULONG lines)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveBeginImageBufferAccess(handle, number, output, stride, lines);
    dive_unlock();
    return result;
}

ULONG DiveEndImageBufferAccess(ULONG handle, ULONG number)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveEndImageBufferAccess(handle, number);
    dive_unlock();
    return result;
}

ULONG DiveSetSourcePalette(ULONG handle, ULONG start, ULONG count, PBYTE entries)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveSetSourcePalette(handle, start, count, entries);
    dive_unlock();
    return result;
}

ULONG DiveSetDestinationPalette(ULONG handle, ULONG start, ULONG count, PBYTE entries)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveSetDestinationPalette(handle, start, count, entries);
    dive_unlock();
    return result;
}

ULONG DiveBlitImage(ULONG handle, ULONG source, ULONG destination)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveBlitImage(handle, source, destination);
    dive_unlock();
    return result;
}

ULONG DiveBlitImageLines(ULONG handle, ULONG source, ULONG destination, PBYTE lines)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveBlitImageLines(handle, source, destination, lines);
    dive_unlock();
    return result;
}

ULONG DiveSetTransparentBlitMode(ULONG handle, ULONG mode, ULONG first, ULONG last)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveSetTransparentBlitMode(handle, mode, first, last);
    dive_unlock();
    return result;
}

ULONG DiveAcquireFrameBuffer(ULONG handle, PRECTL rect)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveAcquireFrameBuffer(handle, rect);
    dive_unlock();
    return result;
}

ULONG DiveDeacquireFrameBuffer(ULONG handle)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveDeacquireFrameBuffer(handle);
    dive_unlock();
    return result;
}

ULONG DiveSwitchBank(ULONG handle, ULONG bank)
{
    (void)bank;
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    DiveInstance *instance = dive_instance(handle);
    ULONG result = !instance              ? DIVE_ERR_INVALID_INSTANCE
                   : instance->non_screen ? DIVE_ERR_NO_DIRECT_ACCESS
                                          : DIVE_ERR_NOT_BANK_SWITCHED;
    dive_unlock();
    return result;
}

ULONG DiveCalcFrameBufferAddress(ULONG handle, PRECTL rect, PVOID output, PULONG bank, PULONG lines)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveCalcFrameBufferAddress(handle, rect, output, bank, lines);
    dive_unlock();
    return result;
}

/* These notifications accompany an external display-driver mode change. The
   software screen backend has no mapped display aperture to rebind. */
VOID DiveFullScreenInit(PVOID aperture, PVOID framebuffer_info)
{
    TRACE_NATIVE("DiveFullScreenInit(%p, %p): software screen has no hardware aperture", aperture,
                 framebuffer_info);
}

VOID DiveFullScreenTerm(void)
{
    TRACE_NATIVE("DiveFullScreenTerm(): software screen has no hardware aperture");
}

static void shutdown_dive(void)
{
    if (!dive_mutex || !dive_lock())
        return;
    for (unsigned i = 0; i < DIVE_MAX_INSTANCES; i++)
        if (instances[i])
            impl_DiveClose(instances[i]->handle);
    dive_unlock();
    lx_host_mutex_destroy(dive_mutex);
    dive_mutex = NULL;
}

#define LX_NATIVE_MODULE_SHUTDOWN() shutdown_dive()
#include "os2libs/generated/dive-lx.h"
