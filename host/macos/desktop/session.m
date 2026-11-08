/* Public domain. No rights reserved. Wholly AI-authored. */
/* Public AppKit policy changes are confined to the SDL main thread. */
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include "host/core/desktop/session_hooks.h"
#include "host/core/desktop/group.h"
#include "host/macos/desktop/internal.h"
#include <stdlib.h>
#include <string.h>
static LxDesktopGroup *group;
static int enabled, initialized, auxiliary, standalone;

int lx_desktop_native_main_thread(void)
{
    return NSThread.isMainThread;
}

static int apply_policy(void *context, int regular)
{
    (void)context;
    NSApplicationActivationPolicy policy =
        regular ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory;
    return NSApp.activationPolicy == policy || [NSApp setActivationPolicy:policy];
}

/* A standalone session carries its native identity in its own launch bundle,
   so a lost lease or window cycle only costs task switching, never windows. */
static int degrade(void)
{
    if (NSApp.activationPolicy != NSApplicationActivationPolicyRegular &&
        ![NSApp setActivationPolicy:NSApplicationActivationPolicyRegular])
        return 0;
    initialized = 1;
    return 1;
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
    const char *alone = getenv("TWOINE_DESKTOP_STANDALONE");
    standalone = alone && *alone && strcmp(alone, "0");
    group = lx_desktop_group_open(getenv("TWOINE_IPC_DIR"), getenv("TWOINE_DESKTOP_SESSION"),
                                  auxiliary, apply_policy, NULL);
    if (!group)
    {
        if (standalone && degrade())
            return 1;
        return SDL_SetError(
            "Cannot initialize desktop session activation policy or leadership lease");
    }
    NSImage *icon = lx_macos_app_icon_read(getenv("TWOINE_DESKTOP_ICON"));
    if (icon)
        NSApp.applicationIconImage = icon;
    if (!auxiliary)
    {
        const char *error = NULL;
        if (!lx_macos_window_cycle_open(&error))
        {
            lx_desktop_group_close(group);
            group = NULL;
            if (standalone && degrade())
                return 1;
            return SDL_SetError("%s", error);
        }
    }
    initialized = 1;
    return 1;
}

int lx_desktop_grouped(void)
{
    return initialized;
}

void lx_desktop_poll(void)
{
    if (initialized && SDL_IsMainThread())
    {
        lx_desktop_group_poll(group);
        lx_macos_window_cycle_receive();
    }
}

void lx_desktop_visible(void *window, int visible)
{
    if (initialized && window && SDL_IsMainThread())
    {
        lx_desktop_group_visible(group, window, visible);
        lx_macos_window_cycle_refresh();
    }
}

void lx_desktop_forget(void *window)
{
    lx_desktop_visible(window, 0);
}
