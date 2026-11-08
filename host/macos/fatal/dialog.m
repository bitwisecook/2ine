/* Public domain. No rights reserved. Wholly AI-authored. */
/* A session bundle is LSUIElement and this helper never reaches a normal app
   launch, so it must take the foreground itself. A plain alert opens on another
   Space when a full-screen application is active, where nobody would see it. */
#import <AppKit/AppKit.h>
#include "host/core/fatal/dialog_hooks.h"

int lx_host_fatal_present(const char *title, const char *message, const char *report)
{
    @autoreleasepool
    {
        NSApplication *application = [NSApplication sharedApplication];
        /* Only an accessory application can join a full-screen Space. */
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [application finishLaunching];
        NSAlert *alert = [[NSAlert alloc] init];
        alert.alertStyle = NSAlertStyleCritical;
        alert.messageText = [NSString stringWithUTF8String:title] ?: @"Program stopped";
        alert.informativeText = [NSString stringWithUTF8String:message] ?: @"";
        [alert addButtonWithTitle:@"Close"];
        if (*report)
            [alert addButtonWithTitle:@"Show Report"];
        [alert layout];
        alert.window.level = NSModalPanelWindowLevel;
        alert.window.collectionBehavior = NSWindowCollectionBehaviorMoveToActiveSpace |
                                          NSWindowCollectionBehaviorFullScreenAuxiliary;
        [application activate];
        NSModalResponse choice = [alert runModal];
        if (choice == NSAlertSecondButtonReturn)
        {
            NSString *path = [NSString stringWithUTF8String:report];
            if (path)
                [[NSWorkspace sharedWorkspace]
                    activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:path] ]];
        }
    }
    return 0;
}
