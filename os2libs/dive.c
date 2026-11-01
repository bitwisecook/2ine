/* Public domain. No rights reserved. Wholly AI-authored. */
/* Indexed DIVE image buffers with checked access and software screen conversion. */
#include "os2native.h"
#include "dive.h"
#include "os2libs/pmwin/graphics.h"
#include "hal/video.h"
#include "hal/host_sync.h"
#define DIVE_MAX_BUFFERS 256
#define DIVE_FIRST_BUFFER 16
#define DIVE_MAX_DIMENSION 16384

typedef struct DiveBuffer
{
    ULONG number, width, height, stride;
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
    uint32_t palette[256];
    DiveBuffer buffers[DIVE_MAX_BUFFERS];
} DiveInstance;
static DiveInstance *instances[DIVE_MAX_INSTANCES];
static ULONG next_handle = 1;
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
    caps->fScreenDirect = FALSE;
    caps->fBankSwitched = FALSE;
    caps->ulDepth = 32;
    caps->ulHorizontalResolution = width;
    caps->ulVerticalResolution = height;
    caps->ulScanLineBytes = (ULONG)width * 4;
    caps->fccColorEncoding = DIVE_FOURCC_BGR4;
    caps->ulApertureSize = 0;
    caps->ulInputFormats = 1;
    caps->ulOutputFormats = 1;
    caps->ulFormatLength = 8;
    if (length < 8 || !formats)
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    ULONG data[2] = {DIVE_FOURCC_LUT8, DIVE_FOURCC_BGR4};
    memcpy(formats, data, sizeof data);
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
            instance->handle = next_handle++;
            for (unsigned j = 0; j < 256; j++)
                instance->palette[j] = j * 0x010101UL;
            instances[i] = instance;
            *handle = instance->handle;
            if (framebuffer)
                *(PVOID *)framebuffer = NULL;
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
            lx_pm_dive_release(instance->target);
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
        lx_pm_dive_release(instance->target);
        instance->target = NULL;
        free(instance->rectangles);
        instance->rectangles = NULL;
        return DIVE_SUCCESS;
    }
    if (setup->ulStructLen < sizeof *setup)
        return DIVE_ERR_INSUFFICIENT_LENGTH;
    if (setup->fccSrcColorFormat != DIVE_FOURCC_LUT8)
        return DIVE_ERR_SOURCE_FORMAT;
    if (setup->fccDstColorFormat != 0 && setup->fccDstColorFormat != DIVE_FOURCC_BGR4)
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
    void *target = lx_pm_dive_target(setup->lScreenPosX + setup->lDstPosX,
                                     setup->lScreenPosY + setup->lDstPosY, setup->ulDstWidth,
                                     setup->ulDstHeight);
    if (!target)
    {
        free(rects);
        return DIVE_ERR_INVALID_DESTINATION_RECTL;
    }
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
    if (format != DIVE_FOURCC_LUT8)
        return DIVE_ERR_SOURCE_FORMAT;
    if (!stride)
        stride = (width + 3) & ~3UL;
    if (stride < width || (uint64_t)stride * height > UINT32_MAX)
        return DIVE_ERR_INVALID_LINESIZE;
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
        mapping = lx_execution_map(GLoaderState.execution, 0,
                                   ((size_t)stride * height + 4095) & ~(size_t)4095,
                                   LX_GUEST_READ | LX_GUEST_WRITE, 0);
        pixels = mapping ? lx_execution_mapping_host(mapping) : NULL;
    }
    else if (!pixels)
        pixels = calloc(height, stride);
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
static ULONG impl_DiveSetSourcePalette(ULONG handle, ULONG start, ULONG count, PBYTE entries)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    if (start >= 256 || count > 256 - start)
        return DIVE_ERR_INVALID_PALETTE;
    if (entries && entries != (PBYTE)(uintptr_t)UINT32_MAX)
        for (ULONG i = 0; i < count; i++)
            instance->palette[start + i] =
                (ULONG)entries[4 * i + 2] << 16 | (ULONG)entries[4 * i + 1] << 8 | entries[4 * i];
    else
        for (ULONG i = start; i < start + (count ? count : 256 - start); i++)
            instance->palette[i] = i * 0x010101UL;
    return DIVE_SUCCESS;
}
static ULONG impl_DiveSetDestinationPalette(ULONG handle, ULONG start, ULONG count, PBYTE entries)
{
    (void)entries;
    if (!dive_instance(handle))
        return DIVE_ERR_INVALID_INSTANCE;
    return start < 256 && count <= 256 - start ? DIVE_SUCCESS : DIVE_ERR_INVALID_PALETTE;
}
static ULONG impl_DiveBlitImage(ULONG handle, ULONG source, ULONG destination)
{
    DiveInstance *instance = dive_instance(handle);
    if (!instance)
        return DIVE_ERR_INVALID_INSTANCE;
    if (!instance->setup)
        return DIVE_ERR_BLITTER_NOT_SETUP;
    DiveBuffer *buffer = dive_buffer(instance, source);
    if (!buffer)
        return DIVE_ERR_INVALID_BUFFER_NUMBER;
    if (buffer->accessed)
        return DIVE_ERR_BUFFER_ALREADY_ACCESSED;
    if (destination != DIVE_BUFFER_SCREEN)
        return DIVE_ERR_DESTINATION_FORMAT;
    SETUP_BLITTER *setup = &instance->blitter;
    if (setup->ulSrcPosX > buffer->width || setup->ulSrcWidth > buffer->width - setup->ulSrcPosX ||
        setup->ulSrcPosY > buffer->height || setup->ulSrcHeight > buffer->height - setup->ulSrcPosY)
        return DIVE_ERR_OUT_OF_RANGE;
    ULONG width = setup->ulDstWidth, height = setup->ulDstHeight;
    uint32_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels)
        return DIVE_ERR_ALLOCATION_ERROR;
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
            pixels[(size_t)y * width + x] =
                instance
                    ->palette[buffer->pixels[(size_t)sy * buffer->stride + sx + setup->ulSrcPosX]];
        }
    }
    BOOL ok = lx_pm_dive_blit(instance->target, setup->lScreenPosX + setup->lDstPosX,
                              setup->lScreenPosY + setup->lDstPosY, width, height, pixels,
                              setup->ulNumDstRects, setup->pVisDstRects);
    free(pixels);
    return ok ? DIVE_SUCCESS : DIVE_ERR_INVALID_DESTINATION_RECTL;
}
/* Serialize instance/buffer ownership with setup and worker submissions. */
ULONG DiveQueryCaps(PDIVE_CAPS caps, ULONG plane)
{
    if (!dive_lock())
        return DIVE_ERR_ALLOCATION_ERROR;
    ULONG result = impl_DiveQueryCaps(caps, plane);
    dive_unlock();
    return result;
}
ULONG DiveOpen(PULONG handle, ULONG non_screen, PVOID framebuffer)
{
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
#include "os2libs/generated/dive-lx.h"
