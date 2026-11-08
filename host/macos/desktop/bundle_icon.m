/* Public domain. No rights reserved. Wholly AI-authored. */
/* PNG icons for the per-program application bundles. */
#include "host/macos/desktop/internal.h"
#include "twoine_ball_icon_png.h"
#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <stdlib.h>
#include <string.h>

#define RGBA_BYTES 4
#define BITS_PER_COMPONENT 8
#define BITS_PER_PIXEL 32
/* OS/2 icons have no partial transparency: a masked pixel is either clear or,
   when it inverts the screen, shown at half opacity. */
#define ALPHA_CLEAR 0
#define ALPHA_HALF 128
#define ALPHA_OPAQUE 255

/* Mirrors tb_icon_rgba_base64: top-down rows, and the AND mask decides the
   alpha of pixels the icon itself leaves transparent. */
int lx_macos_bundle_icon_write(const char *path, const TbIconImage *image)
{
    size_t pixels = (size_t)image->width * image->height;
    unsigned char *rgba = malloc(pixels * RGBA_BYTES);
    if (!rgba)
        return 0;
    for (unsigned y = 0; y < image->height; y++)
        for (unsigned x = 0; x < image->width; x++)
        {
            size_t source = (size_t)(image->height - 1 - y) * image->width + x,
                   target = ((size_t)y * image->width + x) * RGBA_BYTES;
            uint32_t rgb = image->pixels[source];
            rgba[target] = (unsigned char)(rgb >> 16);
            rgba[target + 1] = (unsigned char)(rgb >> 8);
            rgba[target + 2] = (unsigned char)rgb;
            rgba[target + 3] =
                image->mask[source] ? (rgb ? ALPHA_HALF : ALPHA_CLEAR) : ALPHA_OPAQUE;
        }
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(NULL, rgba, pixels * RGBA_BYTES, NULL);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGImageRef rendered =
        provider && space
            ? CGImageCreate(image->width, image->height, BITS_PER_COMPONENT, BITS_PER_PIXEL,
                            (size_t)image->width * RGBA_BYTES, space, kCGImageAlphaLast, provider,
                            NULL, false, kCGRenderingIntentDefault)
            : NULL;
    CFURLRef url = rendered ? CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path,
                                                                      strlen(path), false)
                            : NULL;
    CGImageDestinationRef destination =
        url ? CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL) : NULL;
    int ok = 0;
    if (destination)
    {
        CGImageDestinationAddImage(destination, rendered, NULL);
        ok = CGImageDestinationFinalize(destination);
    }
    if (destination)
        CFRelease(destination);
    if (url)
        CFRelease(url);
    if (rendered)
        CGImageRelease(rendered);
    if (space)
        CGColorSpaceRelease(space);
    if (provider)
        CGDataProviderRelease(provider);
    free(rgba);
    return ok;
}

unsigned char *lx_macos_bundle_ball_icon_png(size_t *size)
{
    const char *hex = TWOINE_BALL_ICON_PNG_HEX;
    size_t length = strlen(hex);
    if (length % 2 || length / 2 != TWOINE_BALL_ICON_PNG_SIZE)
        return NULL;
    unsigned char *bytes = malloc(TWOINE_BALL_ICON_PNG_SIZE);
    if (!bytes)
        return NULL;
    for (size_t i = 0; i < TWOINE_BALL_ICON_PNG_SIZE; i++)
    {
        int high = lx_macos_hex_value(hex[2 * i]), low = lx_macos_hex_value(hex[2 * i + 1]);
        if (high < 0 || low < 0)
        {
            free(bytes);
            return NULL;
        }
        bytes[i] = (unsigned char)(high << 4 | low);
    }
    *size = TWOINE_BALL_ICON_PNG_SIZE;
    return bytes;
}
