/* Public domain. No rights reserved. Wholly AI-authored. */
/* Drawing into a host window's render target, readback and video frames. */
#include "host/sdl/window/internal.h"
#include "host/video_frame.h"

/* Bounds that keep one request's temporary surface and texture reasonable. */
#define MASK_MAX_EXTENT 4096
#define VIDEO_FRAME_MAX_EXTENT 16384
#define VIDEO_FRAME_MAX_CLIPS 4096
#define RGBA_BYTES 4
#define OPAQUE 255

int lx_host_window_mask(LxHostWindow *window, const LxHostRect *clip, int x, int y, int width,
                        int height, const uint8_t *coverage, uint8_t red, uint8_t green,
                        uint8_t blue)
{
    lx_sdl_prepare_renderer();
    if (!window || !clip || width < 0 || height < 0 || width > MASK_MAX_EXTENT ||
        height > MASK_MAX_EXTENT || (width && height && !coverage))
        return 0;
    if (!width || !height)
        return 1;
    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return 0;
    for (int row = 0; row < height; row++)
    {
        uint8_t *pixels = (uint8_t *)surface->pixels + row * surface->pitch;
        for (int column = 0; column < width; column++)
        {
            pixels[column * RGBA_BYTES] = red;
            pixels[column * RGBA_BYTES + 1] = green;
            pixels[column * RGBA_BYTES + 2] = blue;
            pixels[column * RGBA_BYTES + 3] = coverage[(size_t)row * width + column];
        }
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(window->renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture)
        return 0;
    SDL_Rect clipping = {clip->x, clip->y, clip->w, clip->h};
    SDL_FRect destination = {(float)x, (float)y, (float)width, (float)height};
    int ok = SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) &&
             SDL_SetRenderClipRect(window->renderer, &clipping) &&
             SDL_RenderTexture(window->renderer, texture, NULL, &destination);
    SDL_DestroyTexture(texture);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}

int lx_host_window_present(LxHostWindow *window)
{
    lx_sdl_prepare_renderer();
    if (!window->dirty)
        return 1;
    if (!SDL_SetRenderTarget(window->renderer, NULL))
        return 0;
    int ok = SDL_RenderTexture(window->renderer, window->texture, NULL, NULL) &&
             SDL_RenderPresent(window->renderer);
    int restored = SDL_SetRenderTarget(window->renderer, window->texture);
    if (ok && restored)
        window->dirty = 0;
    return restored && ok;
}

int lx_host_window_fill(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                        uint8_t red, uint8_t green, uint8_t blue)
{
    lx_sdl_prepare_renderer();
    SDL_Rect clipping = {clip->x, clip->y, clip->w, clip->h};
    SDL_FRect area = {(float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h};
    int ok = SDL_SetRenderClipRect(window->renderer, &clipping) &&
             SDL_SetRenderDrawColor(window->renderer, red, green, blue, OPAQUE) &&
             SDL_RenderFillRect(window->renderer, &area);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}

int lx_host_window_pixel(LxHostWindow *window, int x, int y, uint8_t *red, uint8_t *green,
                         uint8_t *blue)
{
    lx_sdl_prepare_renderer();
    SDL_Surface *surface = SDL_RenderReadPixels(window->renderer, NULL);
    if (!surface)
        return 0;
    Uint8 alpha;
    int ok = SDL_ReadSurfacePixel(surface, x, y, red, green, blue, &alpha);
    SDL_DestroySurface(surface);
    return ok;
}

int lx_host_window_line(LxHostWindow *window, const LxHostRect *clip, int x1, int y1, int x2,
                        int y2, uint8_t r, uint8_t g, uint8_t b)
{
    lx_sdl_prepare_renderer();
    SDL_Rect bounds = {clip->x, clip->y, clip->w, clip->h};
    int ok = SDL_SetRenderClipRect(window->renderer, &bounds) &&
             SDL_SetRenderDrawColor(window->renderer, r, g, b, OPAQUE) &&
             SDL_RenderLine(window->renderer, (float)x1, (float)y1, (float)x2, (float)y2);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}

int lx_host_window_read(LxHostWindow *window, const LxHostRect *rect, uint32_t *rgb)
{
    lx_sdl_prepare_renderer();
    if (!window || !rect || !rgb || rect->w < 0 || rect->h < 0 || rect->x < 0 || rect->y < 0 ||
        rect->w > window->width - rect->x || rect->h > window->height - rect->y)
        return 0;
    SDL_Rect area = {rect->x, rect->y, rect->w, rect->h};
    SDL_Surface *surface = SDL_RenderReadPixels(window->renderer, &area);
    if (!surface)
        return 0;
    int ok = 1;
    for (int y = 0; ok && y < rect->h; y++)
        for (int x = 0; x < rect->w; x++)
        {
            Uint8 r, g, b, a;
            if (!SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a))
            {
                ok = 0;
                break;
            }
            rgb[(size_t)y * rect->w + x] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
        }
    SDL_DestroySurface(surface);
    return ok;
}

int lx_host_window_image(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                         const uint32_t *rgb)
{
    lx_sdl_prepare_renderer();
    if (!window || !clip || !rect || !rgb || rect->w <= 0 || rect->h <= 0)
        return 0;
    SDL_Surface *surface = SDL_CreateSurface(rect->w, rect->h, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return 0;
    for (int y = 0; y < rect->h; y++)
    {
        uint8_t *row = (uint8_t *)surface->pixels + y * surface->pitch;
        for (int x = 0; x < rect->w; x++)
        {
            uint32_t p = rgb[(size_t)y * rect->w + x];
            row[RGBA_BYTES * x] = p >> 16;
            row[RGBA_BYTES * x + 1] = p >> 8;
            row[RGBA_BYTES * x + 2] = p;
            row[RGBA_BYTES * x + 3] = OPAQUE;
        }
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(window->renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture)
        return 0;
    SDL_Rect clipping = {clip->x, clip->y, clip->w, clip->h};
    SDL_FRect dest = {(float)rect->x, (float)rect->y, (float)rect->w, (float)rect->h};
    int ok = SDL_SetRenderClipRect(window->renderer, &clipping) &&
             SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) &&
             SDL_RenderTexture(window->renderer, texture, NULL, &dest);
    SDL_DestroyTexture(texture);
    if (ok)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}

static int video_plane(const LxHostVideoFrame *frame, unsigned plane, size_t width, size_t height)
{
    return frame->planes[plane] && height && frame->strides[plane] >= width &&
           frame->strides[plane] <= INT32_MAX &&
           height - 1 <= (SIZE_MAX - width) / frame->strides[plane] &&
           (height - 1) * frame->strides[plane] + width <= frame->lengths[plane];
}

int lx_host_window_video_frame(LxHostWindow *window, const LxHostVideoFrame *frame,
                               const LxHostRect *destination, const LxHostRect *clips, size_t count)
{
    if (!window || !frame || !destination || (count && !clips) || count > VIDEO_FRAME_MAX_CLIPS ||
        !frame->width || !frame->height || frame->width > VIDEO_FRAME_MAX_EXTENT ||
        frame->height > VIDEO_FRAME_MAX_EXTENT || destination->w <= 0 || destination->h <= 0 ||
        (unsigned)frame->colors > LX_HOST_FRAME_BT709_FULL)
        return 0;
    unsigned chroma_width = (frame->width + 1) / 2, chroma_height = (frame->height + 1) / 2;
    SDL_PixelFormat format;
    if (frame->format == LX_HOST_FRAME_BGRA)
    {
        format = SDL_PIXELFORMAT_BGRA32;
        if (!video_plane(frame, 0, (size_t)frame->width * RGBA_BYTES, frame->height))
            return 0;
    }
    else if (frame->format == LX_HOST_FRAME_YUV420P)
    {
        format = SDL_PIXELFORMAT_IYUV;
        if (!video_plane(frame, 0, frame->width, frame->height) ||
            !video_plane(frame, 1, chroma_width, chroma_height) ||
            !video_plane(frame, 2, chroma_width, chroma_height))
            return 0;
    }
    else if (frame->format == LX_HOST_FRAME_NV12)
    {
        format = SDL_PIXELFORMAT_NV12;
        if (!video_plane(frame, 0, frame->width, frame->height) ||
            !video_plane(frame, 1, (size_t)chroma_width * 2, chroma_height))
            return 0;
    }
    else
        return 0;
    lx_sdl_prepare_renderer();
    if (!window->video_texture || window->video_width != frame->width ||
        window->video_height != frame->height || window->video_format != (unsigned)format ||
        window->video_colors != (unsigned)frame->colors)
    {
        SDL_DestroyTexture(window->video_texture);
        window->video_texture = NULL;
        SDL_PropertiesID properties = SDL_CreateProperties();
        if (!properties)
            return 0;
        SDL_Colorspace colors =
            frame->format == LX_HOST_FRAME_BGRA            ? SDL_COLORSPACE_SRGB
            : frame->colors == LX_HOST_FRAME_BT601_FULL    ? SDL_COLORSPACE_BT601_FULL
            : frame->colors == LX_HOST_FRAME_BT709_LIMITED ? SDL_COLORSPACE_BT709_LIMITED
            : frame->colors == LX_HOST_FRAME_BT709_FULL    ? SDL_COLORSPACE_BT709_FULL
                                                           : SDL_COLORSPACE_BT601_LIMITED;
        SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, format);
        SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,
                              SDL_TEXTUREACCESS_STREAMING);
        SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, frame->width);
        SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, frame->height);
        SDL_SetNumberProperty(properties, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, colors);
        window->video_texture = SDL_CreateTextureWithProperties(window->renderer, properties);
        SDL_DestroyProperties(properties);
        if (!window->video_texture)
            return 0;
        window->video_width = frame->width;
        window->video_height = frame->height;
        window->video_format = format;
        window->video_colors = frame->colors;
        SDL_SetTextureBlendMode(window->video_texture, SDL_BLENDMODE_NONE);
    }
    int ok;
    if (frame->format == LX_HOST_FRAME_YUV420P)
        ok = SDL_UpdateYUVTexture(window->video_texture, NULL, frame->planes[0],
                                  (int)frame->strides[0], frame->planes[1], (int)frame->strides[1],
                                  frame->planes[2], (int)frame->strides[2]);
    else if (frame->format == LX_HOST_FRAME_NV12)
        ok = SDL_UpdateNVTexture(window->video_texture, NULL, frame->planes[0],
                                 (int)frame->strides[0], frame->planes[1], (int)frame->strides[1]);
    else
        ok = SDL_UpdateTexture(window->video_texture, NULL, frame->planes[0],
                               (int)frame->strides[0]);
    SDL_FRect target = {(float)destination->x, (float)destination->y, (float)destination->w,
                        (float)destination->h};
    for (size_t i = 0; ok && i < count; i++)
    {
        SDL_Rect clip = {clips[i].x, clips[i].y, clips[i].w, clips[i].h};
        ok = SDL_SetRenderClipRect(window->renderer, &clip) &&
             SDL_RenderTexture(window->renderer, window->video_texture, NULL, &target);
    }
    if (ok && count)
        window->dirty = 1;
    return SDL_SetRenderClipRect(window->renderer, NULL) && ok;
}
