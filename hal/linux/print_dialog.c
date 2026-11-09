/* Public domain. No rights reserved. Wholly AI-authored. */
/* The native 2ine-dialog helper presents save destinations and configured
   printers. All arguments are passed literally, never shell-interpreted. */
#define _POSIX_C_SOURCE 200809L
#include "hal/print.h"
#include "host_dialog.h"
#include <stdio.h>
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
    const char *arguments[] = {"save", "Save Print Output", title && *title ? title : "Print Job",
                               extension, NULL};
    LxHostSaveResult result = lx_host_dialog_run(arguments, path);
    if (result == LX_HOST_SAVE_OK && **path != '/')
    {
        free(*path);
        *path = NULL;
        return LX_HOST_SAVE_ERROR;
    }
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
    const char **arguments = calloc(count + 2, sizeof *arguments);
    if (!arguments)
    {
        for (size_t i = 0; i < count; i++)
            free(printers[i]);
        free(printers);
        return LX_HOST_SAVE_ERROR;
    }
    unsigned at = 0;
    arguments[at++] = "print-pcl";
    for (size_t i = 0; i < count; i++)
        arguments[at++] = printers[i];
    char *choice = NULL;
    LxHostSaveResult result = lx_host_dialog_run(arguments, &choice);
    if (result == LX_HOST_SAVE_OK)
    {
        if (!strcmp(choice, "@save"))
            result = lx_host_print_save_path(title, "pcl", &destination->path);
        else
        {
            int found = 0;
            for (size_t i = 0; i < count; i++)
                found |= !strcmp(choice, printers[i]);
            if (found)
            {
                destination->printer = choice;
                choice = NULL;
            }
            else
                result = LX_HOST_SAVE_ERROR;
        }
    }
    free(choice);
    free(arguments);
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
    const char *arguments[] = {"print-device", NULL};
    char *choice = NULL;
    LxHostSaveResult result = lx_host_dialog_run(arguments, &choice);
    if (result == LX_HOST_SAVE_OK)
    {
        if (strcmp(choice, "pdf") && strcmp(choice, "pcl"))
            result = LX_HOST_SAVE_ERROR;
        else
            *kind = !strcmp(choice, "pcl") ? 2 : 1;
    }
    free(choice);
    return result;
}
