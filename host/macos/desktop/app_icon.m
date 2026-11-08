/* Public domain. No rights reserved. Wholly AI-authored. */
/* The application icon a launcher hands a desktop session. */
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include "host/macos/desktop/internal.h"
#include <string.h>

/* Icon files are read whole; larger ones are rejected. */
#define ICON_FILE_MAX (16 * 1024 * 1024)
/* A 2INERGBA icon: the signature, then little-endian 32-bit width and height,
   then width x height non-premultiplied RGBA pixels. */
#define RGBA_SIGNATURE "2INERGBA"
#define RGBA_SIGNATURE_BYTES 8
#define RGBA_WIDTH_OFFSET 8
#define RGBA_HEIGHT_OFFSET 12
#define RGBA_HEADER_BYTES 16
#define RGBA_MAX_EXTENT 1024
#define RGBA_BYTES 4
/* Other formats are decoded by ImageIO within these bounds and scaled down. */
#define IMAGE_MAX_EXTENT 16384
#define IMAGE_MAX_PIXELS (64 * 1024 * 1024)
#define ICON_THUMBNAIL_PIXELS 256

NSImage *lx_macos_app_icon_read(const char *path)
{
    if (!path)
        return nil;
    NSString *name = [NSString stringWithUTF8String:path];
    NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:name
                                                                                error:nil];
    if (!attributes || [attributes fileSize] > ICON_FILE_MAX)
        return nil;
    NSData *data = [NSData dataWithContentsOfFile:name];
    if (data.length >= RGBA_HEADER_BYTES &&
        !memcmp(data.bytes, RGBA_SIGNATURE, RGBA_SIGNATURE_BYTES))
    {
        const unsigned char *bytes = data.bytes;
        uint32_t width = 0, height = 0;
        for (unsigned i = 0; i < sizeof width; i++)
        {
            width |= (uint32_t)bytes[RGBA_WIDTH_OFFSET + i] << (i * 8);
            height |= (uint32_t)bytes[RGBA_HEIGHT_OFFSET + i] << (i * 8);
        }
        if (!width || !height || width > RGBA_MAX_EXTENT || height > RGBA_MAX_EXTENT ||
            data.length != RGBA_HEADER_BYTES + (size_t)width * height * RGBA_BYTES)
            return nil;
        NSBitmapImageRep *bitmap =
            [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                    pixelsWide:width
                                                    pixelsHigh:height
                                                 bitsPerSample:8
                                               samplesPerPixel:4
                                                      hasAlpha:YES
                                                      isPlanar:NO
                                                colorSpaceName:NSDeviceRGBColorSpace
                                                  bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                                                   bytesPerRow:width * RGBA_BYTES
                                                  bitsPerPixel:32];
        if (!bitmap.bitmapData)
            return nil;
        memcpy(bitmap.bitmapData, bytes + RGBA_HEADER_BYTES, data.length - RGBA_HEADER_BYTES);
        NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
        [image addRepresentation:bitmap];
        return image;
    }
    CGImageSourceRef source =
        data ? CGImageSourceCreateWithData((__bridge CFDataRef)data, NULL) : NULL;
    NSDictionary *properties =
        source ? CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, NULL)) : nil;
    NSUInteger width =
        [properties[(__bridge NSString *)kCGImagePropertyPixelWidth] unsignedIntegerValue];
    NSUInteger height =
        [properties[(__bridge NSString *)kCGImagePropertyPixelHeight] unsignedIntegerValue];
    CGImageRef thumbnail = NULL;
    if (width && height && width <= IMAGE_MAX_EXTENT && height <= IMAGE_MAX_EXTENT &&
        (uint64_t)width * height <= IMAGE_MAX_PIXELS)
        thumbnail = CGImageSourceCreateThumbnailAtIndex(
            source, 0, (__bridge CFDictionaryRef) @{
                (__bridge NSString *)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
                (__bridge NSString *)kCGImageSourceThumbnailMaxPixelSize : @(ICON_THUMBNAIL_PIXELS)
            });
    if (source)
        CFRelease(source);
    NSImage *image = thumbnail ? [[NSImage alloc] initWithCGImage:thumbnail size:NSZeroSize] : nil;
    if (thumbnail)
        CGImageRelease(thumbnail);
    return image;
}
