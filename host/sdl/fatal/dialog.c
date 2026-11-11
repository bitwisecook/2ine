/* Public domain. No rights reserved. Wholly AI-authored. */
/* A native dialog helper presents the failure where the host has one (the
   GNOME 2ine-dialog on Linux); SDL's message box is the fallback. */
#include "host/core/fatal/dialog_hooks.h"
#include "host/dialog.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

enum
{
    BUTTON_CLOSE,
    BUTTON_SHOW_REPORT
};

static void open_report(const char *path)
{
    static const char unreserved[] = "-._~/";
    char url[4096] = "file://";
    size_t used = strlen(url);
    for (const unsigned char *p = (const unsigned char *)path; *p && used + 4 < sizeof url; p++)
    {
        if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            strchr(unreserved, *p))
            url[used++] = (char)*p;
        else
            used += (size_t)snprintf(url + used, sizeof url - used, "%%%02X", *p);
    }
    url[used] = '\0';
    SDL_OpenURL(url);
}

int lx_host_fatal_present(const char *title, const char *message, const char *report)
{
    const char *arguments[] = {"fatal", title, message, report, NULL};
    if (lx_host_dialog_run(arguments, NULL) != LX_HOST_SAVE_ERROR)
        return 0;
    SDL_MessageBoxButtonData buttons[2] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
         BUTTON_CLOSE, "Close"},
        {0, BUTTON_SHOW_REPORT, "Show Report"}};
    SDL_MessageBoxData box = {SDL_MESSAGEBOX_ERROR, NULL,    title, message,
                              *report ? 2 : 1,      buttons, NULL};
    int choice = BUTTON_CLOSE;
    if (!SDL_ShowMessageBox(&box, &choice))
    {
        fprintf(stderr, "2ine: cannot show the failure dialog: %s\n", SDL_GetError());
        return 1;
    }
    if (choice == BUTTON_SHOW_REPORT)
        open_report(report);
    return 0;
}
