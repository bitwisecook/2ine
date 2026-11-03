/* Public domain. No rights reserved. Wholly AI-authored. */
/* Cocoa owns filename entry, location selection and replacement confirmation. */
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "hal/print.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
LxHostSaveResult lx_host_print_save_path(const char *title, const char *extension, char **path)
{
    if (!path || !extension)
        return LX_HOST_SAVE_ERROR;
    *path = NULL;
    const char *override = getenv("TWOINE_PRINT_OUTPUT");
    if (override)
    {
        if (!*override)
            return LX_HOST_SAVE_CANCEL;
        *path = strdup(override);
        return *path ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
    }
    __block LxHostSaveResult result = LX_HOST_SAVE_CANCEL;
    __block char *selected = NULL;
    void (^show)(void) = ^{
      @autoreleasepool
      {
          [NSApplication sharedApplication];
          NSSavePanel *panel = [NSSavePanel savePanel];
          panel.title = !strcmp(extension, "pdf") ? @"Save Printed PDF" : @"Save PCL Print File";
          panel.message = @"Choose a name and location for this print job.";
          panel.canCreateDirectories = YES;
          NSString *name = title && *title ? [NSString stringWithUTF8String:title] : @"Print Job";
          if (!name)
              name = @"Print Job";
          name = [[name
              componentsSeparatedByCharactersInSet:[NSCharacterSet
                                                       characterSetWithCharactersInString:@"/:\\"]]
              componentsJoinedByString:@"-"];
          NSString *suffix = [NSString stringWithUTF8String:extension];
          panel.nameFieldStringValue = [name stringByAppendingPathExtension:suffix];
          UTType *type = [UTType typeWithFilenameExtension:suffix];
          if (type)
              panel.allowedContentTypes = @[ type ];
          [NSApp activate];
          if ([panel runModal] == NSModalResponseOK && panel.URL)
          {
              selected = strdup(panel.URL.fileSystemRepresentation);
              result = selected ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
          }
      }
    };
    if (pthread_main_np())
        show();
    else
        dispatch_sync(dispatch_get_main_queue(), show);
    *path = selected;
    return result;
}

LxHostSaveResult lx_host_print_choose_pcl(const char *title, LxHostPrintDestination *destination)
{
    if (!destination)
        return LX_HOST_SAVE_ERROR;
    memset(destination, 0, sizeof *destination);
    destination->copies = 1;
    if (getenv("TWOINE_PRINT_OUTPUT"))
        return lx_host_print_save_path(title, "pcl", &destination->path);
    const char *override = getenv("TWOINE_PRINT_PRINTER");
    if (override)
    {
        if (!*override)
            return LX_HOST_SAVE_CANCEL;
        destination->printer = strdup(override);
        return destination->printer ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
    }
    size_t count = 0;
    char **printers = lx_host_print_printers(&count);
    __block LxHostSaveResult result = LX_HOST_SAVE_CANCEL;
    void (^show)(void) = ^{
      @autoreleasepool
      {
          [NSApplication sharedApplication];
          NSAlert *alert = [NSAlert new];
          alert.messageText = @"PCL Printer";
          alert.informativeText =
              count
                  ? @"Choose a PCL 5-compatible system printer, or save the print data as a file."
                  : @"No system printers are available. You can save the PCL print data as a file.";
          [alert addButtonWithTitle:@"Print"];
          [alert addButtonWithTitle:@"Save PCL File…"];
          [alert addButtonWithTitle:@"Cancel"];
          NSPopUpButton *choice = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 360, 28)
                                                             pullsDown:NO];
          for (size_t i = 0; i < count; i++)
          {
              NSString *name = [NSString stringWithUTF8String:printers[i]];
              if (name)
                  [choice addItemWithTitle:name];
          }
          choice.accessibilityLabel = @"System printer";
          alert.accessoryView = choice;
          alert.buttons[0].enabled = choice.numberOfItems != 0;
          [NSApp activate];
          NSModalResponse response = [alert runModal];
          if (response == NSAlertFirstButtonReturn && choice.selectedItem)
          {
              destination->printer = strdup(choice.selectedItem.title.UTF8String);
              result = destination->printer ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
          }
          else if (response == NSAlertSecondButtonReturn)
              result = lx_host_print_save_path(title, "pcl", &destination->path);
      }
    };
    if (pthread_main_np())
        show();
    else
        dispatch_sync(dispatch_get_main_queue(), show);
    for (size_t i = 0; i < count; i++)
        free(printers[i]);
    free(printers);
    return result;
}

LxHostSaveResult lx_host_print_choose_device(unsigned *kind)
{
    if (!kind)
        return LX_HOST_SAVE_ERROR;
    const char *override = getenv("TWOINE_PRINT_DEVICE");
    if (override || getenv("TWOINE_PRINT_OUTPUT") || getenv("TWOINE_PRINT_PRINTER"))
    {
        if (override && !*override)
            return LX_HOST_SAVE_CANCEL;
        if (override && strcmp(override, "pdf") && strcmp(override, "pcl"))
            return LX_HOST_SAVE_ERROR;
        *kind = override                         ? (!strcmp(override, "pcl") ? 2 : 1)
                : getenv("TWOINE_PRINT_PRINTER") ? 2
                                                 : 1;
        return LX_HOST_SAVE_OK;
    }
    __block LxHostSaveResult result = LX_HOST_SAVE_CANCEL;
    void (^show)(void) = ^{
      @autoreleasepool
      {
          [NSApplication sharedApplication];
          NSAlert *alert = [NSAlert new];
          alert.messageText = @"Print Destination";
          alert.informativeText =
              @"PDF saves a document. PCL prints to a compatible system printer or saves a PCL "
              @"file. You’ll choose its location or printer when the job starts.";
          [alert addButtonWithTitle:@"PDF"];
          [alert addButtonWithTitle:@"PCL Printer"];
          [alert addButtonWithTitle:@"Cancel"];
          [NSApp activate];
          NSModalResponse choice = [alert runModal];
          if (choice == NSAlertFirstButtonReturn || choice == NSAlertSecondButtonReturn)
          {
              *kind = choice == NSAlertFirstButtonReturn ? 1 : 2;
              result = LX_HOST_SAVE_OK;
          }
      }
    };
    if (pthread_main_np())
        show();
    else
        dispatch_sync(dispatch_get_main_queue(), show);
    return result;
}
