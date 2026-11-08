/* Public domain. No rights reserved. Wholly AI-authored. */
/* Cmd-` window cycling across the processes of one desktop session. */
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include "host/macos/desktop/internal.h"
#include "host/core/desktop/cycle_registry.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The most windows one process publishes to the session's cycle. */
#define CYCLE_WINDOW_LIMIT 256

static LxDesktopCycle *cycle;
static id cycle_monitor, cycle_focus_observer;
static uint32_t last_windows[CYCLE_WINDOW_LIMIT], last_focus;
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

int lx_macos_window_cycle_refresh(void)
{
    if (!cycle)
        return 0;
    uint32_t windows[CYCLE_WINDOW_LIMIT], focused = 0;
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
        if (count == CYCLE_WINDOW_LIMIT)
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

void lx_macos_window_cycle_receive(void)
{
    if (!lx_macos_window_cycle_refresh() || observed_blocked)
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
    if (!lx_macos_window_cycle_refresh())
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
        lx_macos_window_cycle_receive();
    return nil;
}

int lx_macos_window_cycle_open(const char **error)
{
    cycle = lx_desktop_cycle_open(getenv("TWOINE_IPC_DIR"), getenv("TWOINE_DESKTOP_SESSION"));
    if (!cycle)
    {
        *error = "Cannot initialize desktop window cycling";
        return 0;
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
                                                      lx_macos_window_cycle_refresh();
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
        *error = "Cannot register desktop window cycling events";
        return 0;
    }
    return 1;
}
