/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native window controls stay outside the guest canvas and input queue. */
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include "hal/sdl/window_chrome.h"
#include <stdlib.h>

@interface TwoineCaptureControl : NSObject
@property(nonatomic, weak) NSWindow *window;
@property(nonatomic, strong) NSTitlebarAccessoryViewController *accessory;
@property(nonatomic, strong) NSButton *button;
@property(nonatomic) void (*toggle)(void *);
@property(nonatomic) void *userdata;
- (void)toggleCapture:(id)sender;
@end

@implementation TwoineCaptureControl
- (void)toggleCapture:(id)sender
{
    (void)sender;
    self.toggle(self.userdata);
}
@end

struct LxWindowChrome
{
    void *control;
};
LxWindowChrome *lx_window_chrome_create(void *opaque, void (*toggle)(void *), void *userdata)
{
    SDL_Window *sdl = opaque;
    NSWindow *window = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!window)
        return NULL;
    LxWindowChrome *chrome = calloc(1, sizeof *chrome);
    if (!chrome)
        return NULL;
    TwoineCaptureControl *control = [TwoineCaptureControl new];
    control.window = window;
    control.toggle = toggle;
    control.userdata = userdata;
    control.button = [NSButton buttonWithTitle:@"Capture Mouse ⌃⌥"
                                        target:control action:@selector(toggleCapture:)];
    control.button.bezelStyle = NSBezelStyleRounded;
    control.button.controlSize = NSControlSizeSmall;
    control.button.refusesFirstResponder = YES;
    control.button.frame = NSMakeRect(0, 3, 180, 24);
    control.button.image = [NSImage imageWithSystemSymbolName:@"cursorarrow"
                                   accessibilityDescription:@"Mouse capture"];
    control.button.imagePosition = NSImageLeft;
    control.accessory = [NSTitlebarAccessoryViewController new];
    control.accessory.layoutAttribute = NSLayoutAttributeRight;
    control.accessory.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 188, 30)];
    [control.accessory.view addSubview:control.button];
    [window addTitlebarAccessoryViewController:control.accessory];
    chrome->control = (__bridge_retained void *)control;
    lx_window_chrome_capture(chrome, 0);
    return chrome;
}
void lx_window_chrome_capture(LxWindowChrome *chrome, int captured)
{
    if (!chrome)
        return;
    TwoineCaptureControl *control = (__bridge TwoineCaptureControl *)chrome->control;
    control.button.title = captured ? @"Release Mouse ⌃⌥" : @"Capture Mouse ⌃⌥";
    control.button.accessibilityLabel = captured ? @"Release Mouse" : @"Capture Mouse";
    control.button.toolTip = captured ? @"Release the mouse (Control–Option)"
                                     : @"Capture the mouse (Control–Option)";
}
void lx_window_chrome_destroy(LxWindowChrome *chrome)
{
    if (!chrome)
        return;
    TwoineCaptureControl *control = (__bridge_transfer TwoineCaptureControl *)chrome->control;
    NSUInteger index = [control.window.titlebarAccessoryViewControllers
        indexOfObjectIdenticalTo:control.accessory];
    if (index != NSNotFound && control.window)
        [control.window removeTitlebarAccessoryViewControllerAtIndex:index];
    free(chrome);
}
