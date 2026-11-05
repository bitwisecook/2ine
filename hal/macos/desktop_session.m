/* Public domain. No rights reserved. Wholly AI-authored. */
/* Public AppKit policy changes are confined to the SDL main thread. */
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include <SDL3/SDL.h>
#include "hal/sdl/desktop_session.h"
#include "hal/common/desktop_group.h"
#include "hal/common/desktop_cycle.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static LxDesktopGroup *group;
static int enabled, initialized, auxiliary;
int lx_desktop_native_main_thread(void) { return NSThread.isMainThread; }
static LxDesktopCycle *cycle;
static id cycle_monitor, cycle_focus_observer;
static uint32_t last_windows[256], last_focus;
static int last_count = -1, last_blocked, observed_blocked, key_consumed;
static NSWindow *native_window(SDL_Window *window)
{
    return (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                       SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
}
static int id_order(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return a < b ? -1 : a != b;
}
static int refresh_cycle(void)
{
    if (!cycle)
        return 0;
    uint32_t windows[256], focused = 0;
    int count = 0, total = 0, blocked = NSApp.modalWindow != nil;
    observed_blocked = blocked;
    SDL_Window **items = SDL_GetWindows(&total);
    if (!items)
        return 0;
    for (int i = 0; i < total; i++)
    {
        SDL_Window *window = items[i];
        NSWindow *native = native_window(window);
        SDL_WindowFlags flags = SDL_GetWindowFlags(window);
        if ((flags & SDL_WINDOW_HIDDEN) || !native.visible)
            continue;
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        blocked |= native.attachedSheet != nil || native.sheetParent != nil ||
                   SDL_GetBooleanProperty(properties, "2ine.cycle.modal", false);
        observed_blocked = blocked;
        if ((flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_TOOLTIP | SDL_WINDOW_POPUP_MENU)) ||
            [native isKindOfClass:NSPanel.class] || !native.canBecomeKeyWindow ||
            !SDL_GetBooleanProperty(properties, "2ine.cycle.eligible", true))
            continue;
        if (count == 256)
        {
            SDL_free(items);
            return 0;
        }
        uint32_t identity = SDL_GetWindowID(window);
        windows[count++] = identity;
        if (NSApp.active && NSApp.keyWindow == native)
            focused = identity;
    }
    SDL_free(items);
    qsort(windows, count, sizeof *windows, id_order);
    if (last_count != count || last_blocked != blocked ||
        memcmp(windows, last_windows, (size_t)count * sizeof *windows))
    {
        if (!lx_desktop_cycle_publish(cycle, windows, count, blocked))
            return 0;
        memcpy(last_windows, windows, (size_t)count * sizeof *windows);
        last_count = count;
        last_blocked = blocked;
    }
    if (focused != last_focus && focused && !lx_desktop_cycle_focus(cycle, focused))
        return 0;
    last_focus = focused;
    return 1;
}
static void receive_cycle(void)
{
    if (!refresh_cycle() || observed_blocked)
    {
        if (cycle && observed_blocked)
            lx_desktop_cycle_receive(cycle);
        return;
    }
    uint32_t identity = lx_desktop_cycle_receive(cycle);
    if (!identity)
        return;
    SDL_Window *window = SDL_GetWindowFromID(identity);
    NSWindow *native = window ? native_window(window) : nil;
    if (!native || !native.visible ||
        !SDL_GetBooleanProperty(SDL_GetWindowProperties(window), "2ine.cycle.eligible", true))
        return;
    if (@available(macOS 14.0, *))
        [NSApp activate];
    [native makeKeyAndOrderFront:nil];
}
static NSEvent *cycle_key(NSEvent *event)
{
    NSString *characters = event.charactersIgnoringModifiers;
    BOOL grave = [characters isEqualToString:@"`"] || [characters isEqualToString:@"~"];
    if (!grave || !cycle)
        return event;
    if (event.type == NSEventTypeKeyUp)
    {
        int intercepted = lx_desktop_cycle_keyup(cycle) || key_consumed;
        key_consumed = 0;
        return intercepted ? nil : event;
    }
    key_consumed = 0;
    NSEventModifierFlags modifiers =
        event.modifierFlags &
        (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption);
    if (modifiers != NSEventModifierFlagCommand)
        return event;
    if (!refresh_cycle())
    {
        key_consumed = 1;
        return nil;
    }
    uint32_t target_pid = 0;
    int handled = lx_desktop_cycle_request(
        cycle, last_focus, (event.modifierFlags & NSEventModifierFlagShift) != 0, &target_pid);
    key_consumed = 1;
    if (handled != 1)
        return nil;
    if (target_pid != (uint32_t)getpid())
    {
        NSRunningApplication *target =
            [NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t)target_pid];
        if (@available(macOS 14.0, *))
        {
            if (target && [target.bundleURL isEqual:NSBundle.mainBundle.bundleURL])
            {
                [NSApp yieldActivationToApplication:target];
                [target activateFromApplication:NSRunningApplication.currentApplication options:0];
            }
        }
    }
    else
        receive_cycle();
    return nil;
}
static int apply_policy(void *context, int regular)
{
    (void)context;
    NSApplicationActivationPolicy policy =
        regular ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory;
    return NSApp.activationPolicy == policy || [NSApp setActivationPolicy:policy];
}
static NSImage *read_icon(const char *path)
{
    if (!path)
        return nil;
    NSString *name = [NSString stringWithUTF8String:path];
    NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:name
                                                                                error:nil];
    if (!attributes || [attributes fileSize] > 16 * 1024 * 1024)
        return nil;
    NSData *data = [NSData dataWithContentsOfFile:name];
    if (data.length >= 16 && !memcmp(data.bytes, "2INERGBA", 8))
    {
        const unsigned char *bytes = data.bytes;
        uint32_t width = 0, height = 0;
        for (unsigned i = 0; i < 4; i++)
        {
            width |= (uint32_t)bytes[8 + i] << (i * 8);
            height |= (uint32_t)bytes[12 + i] << (i * 8);
        }
        if (!width || !height || width > 1024 || height > 1024 ||
            data.length != 16 + (size_t)width * height * 4)
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
                                                   bytesPerRow:width * 4
                                                  bitsPerPixel:32];
        if (!bitmap.bitmapData)
            return nil;
        memcpy(bitmap.bitmapData, bytes + 16, data.length - 16);
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
    if (width && height && width <= 16384 && height <= 16384 &&
        (uint64_t)width * height <= 64 * 1024 * 1024)
        thumbnail = CGImageSourceCreateThumbnailAtIndex(
            source, 0, (__bridge CFDictionaryRef) @{
                (__bridge NSString *)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
                (__bridge NSString *)kCGImageSourceThumbnailMaxPixelSize : @256
            });
    if (source)
        CFRelease(source);
    NSImage *image = thumbnail ? [[NSImage alloc] initWithCGImage:thumbnail size:NSZeroSize] : nil;
    if (thumbnail)
        CGImageRelease(thumbnail);
    return image;
}
void lx_desktop_prepare(void)
{
    const char *session = getenv("TWOINE_DESKTOP_SESSION");
    const char *helper = getenv("TWOINE_DESKTOP_AUXILIARY");
    auxiliary = helper && *helper && strcmp(helper, "0");
    enabled = (session && *session) || auxiliary;
    if (!enabled)
        return;
    SDL_SetHintWithPriority(SDL_HINT_MAC_BACKGROUND_APP, "1", SDL_HINT_OVERRIDE);
    const char *name = getenv("TWOINE_DESKTOP_NAME");
    if (name && *name)
        SDL_SetAppMetadata(name, "1", "io.github.bitwisecook.2ine.guest");
}
int lx_desktop_initialized(void)
{
    if (!enabled || initialized)
        return 1;
    const char *driver = SDL_GetCurrentVideoDriver();
    if (!driver || strcmp(driver, "cocoa"))
        return 1;
    if (!SDL_IsMainThread())
        return SDL_SetError("Desktop session activation requires the SDL main thread");
    group = lx_desktop_group_open(getenv("TWOINE_IPC_DIR"), getenv("TWOINE_DESKTOP_SESSION"),
                                  auxiliary, apply_policy, NULL);
    if (!group)
        return SDL_SetError(
            "Cannot initialize desktop session activation policy or leadership lease");
    NSImage *icon = read_icon(getenv("TWOINE_DESKTOP_ICON"));
    if (icon)
        NSApp.applicationIconImage = icon;
    if (!auxiliary)
    {
        cycle = lx_desktop_cycle_open(getenv("TWOINE_IPC_DIR"), getenv("TWOINE_DESKTOP_SESSION"));
        if (!cycle)
        {
            lx_desktop_group_close(group);
            group = NULL;
            return SDL_SetError("Cannot initialize desktop window cycling");
        }
        last_count = -1;
        last_focus = 0;
        cycle_monitor =
            [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp
                                                  handler:^NSEvent *(NSEvent *event) {
                                                    return cycle_key(event);
                                                  }];
        cycle_focus_observer =
            [NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidBecomeKeyNotification
                                                            object:nil
                                                             queue:nil
                                                        usingBlock:^(NSNotification *notification) {
                                                          (void)notification;
                                                          last_focus = 0;
                                                          refresh_cycle();
                                                        }];
        if (!cycle_monitor || !cycle_focus_observer)
        {
            if (cycle_monitor)
                [NSEvent removeMonitor:cycle_monitor];
            if (cycle_focus_observer)
                [NSNotificationCenter.defaultCenter removeObserver:cycle_focus_observer];
            cycle_monitor = cycle_focus_observer = nil;
            lx_desktop_cycle_close(cycle);
            cycle = NULL;
            lx_desktop_group_close(group);
            group = NULL;
            return SDL_SetError("Cannot register desktop window cycling events");
        }
    }
    initialized = 1;
    return 1;
}
int lx_desktop_grouped(void) { return initialized; }
void lx_desktop_poll(void)
{
    if (initialized && SDL_IsMainThread())
    {
        lx_desktop_group_poll(group);
        receive_cycle();
    }
}
void lx_desktop_visible(void *window, int visible)
{
    if (initialized && window && SDL_IsMainThread())
    {
        lx_desktop_group_visible(group, window, visible);
        refresh_cycle();
    }
}
void lx_desktop_forget(void *window) { lx_desktop_visible(window, 0); }
