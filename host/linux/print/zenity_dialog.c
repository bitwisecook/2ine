/* Public domain. No rights reserved. Wholly AI-authored. */
/* GNOME's Zenity chooser presents save destinations and configured printers.
   All external commands receive literal argv, never shell-interpreted text. */
#define _POSIX_C_SOURCE 200809L
#include "host/print.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* The shell's exit status when a command cannot be executed. */
#define EXEC_FAILED_STATUS 127

static LxHostSaveResult dialog(char *const arguments[], char **output)
{
    *output = NULL;
    int descriptors[2];
    if (pipe(descriptors))
        return LX_HOST_SAVE_ERROR;
    pid_t child = fork();
    if (!child)
    {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0)
            _exit(EXEC_FAILED_STATUS);
        close(descriptors[1]);
        execvp(arguments[0], arguments);
        _exit(EXEC_FAILED_STATUS);
    }
    close(descriptors[1]);
    const size_t capacity = 65536;
    char *buffer = malloc(capacity);
    size_t length = 0;
    ssize_t n = 0;
    if (child > 0 && buffer)
        while (length < capacity - 1 &&
               (n = read(descriptors[0], buffer + length, capacity - 1 - length)) != 0)
        {
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            length += (size_t)n;
        }
    close(descriptors[0]);
    int status = 0;
    pid_t waited;
    do
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    while (waited < 0 && errno == EINTR);
    if (child < 0 || waited != child || !WIFEXITED(status) || n < 0 || !buffer ||
        length == capacity - 1)
    {
        free(buffer);
        return LX_HOST_SAVE_ERROR;
    }
    if (WEXITSTATUS(status) == 1)
    {
        free(buffer);
        return LX_HOST_SAVE_CANCEL;
    }
    if (WEXITSTATUS(status) != 0)
    {
        fprintf(
            stderr,
            "2ine: Cannot open the GNOME print dialog. Install zenity and run in a desktop session.\n");
        free(buffer);
        return LX_HOST_SAVE_ERROR;
    }
    if (length && buffer[length - 1] == '\n')
        length--;
    if (!length || memchr(buffer, 0, length))
    {
        free(buffer);
        return LX_HOST_SAVE_ERROR;
    }
    buffer[length] = 0;
    *output = strdup(buffer);
    free(buffer);
    return *output ? LX_HOST_SAVE_OK : LX_HOST_SAVE_ERROR;
}

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
    size_t size = strlen(title ? title : "Print Job") + strlen(extension) + 32;
    char *name = malloc(size), *filter = malloc(strlen(extension) + 40);
    if (!name || !filter)
    {
        free(name);
        free(filter);
        return LX_HOST_SAVE_ERROR;
    }
    snprintf(name, size, "--filename=%s.%s", title && *title ? title : "Print Job", extension);
    for (char *p = name + strlen("--filename="); *p; p++)
        if (*p == '/' || *p == '\\' || (unsigned char)*p < 32)
            *p = '-';
    snprintf(filter, strlen(extension) + 40, "--file-filter=Print files | *.%s", extension);
    char *arguments[] = {"zenity",
                         "--file-selection",
                         "--save",
                         "--confirm-overwrite",
                         "--title=Save Print Output",
                         name,
                         filter,
                         NULL};
    LxHostSaveResult result = dialog(arguments, path);
    free(name);
    free(filter);
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
    char **arguments = calloc(13 + count * 2, sizeof *arguments);
    if (!arguments)
    {
        for (size_t i = 0; i < count; i++)
            free(printers[i]);
        free(printers);
        return LX_HOST_SAVE_ERROR;
    }
    unsigned at = 0;
    arguments[at++] = "zenity";
    arguments[at++] = "--list";
    arguments[at++] = "--title=PCL Printer";
    arguments[at++] = "--text=Choose a PCL 5-compatible system printer, or save a PCL file.";
    arguments[at++] = "--column=Destination";
    arguments[at++] = "--column=Description";
    arguments[at++] = "--hide-column=1";
    arguments[at++] = "--print-column=1";
    arguments[at++] = "@save";
    arguments[at++] = "Save PCL File…";
    for (size_t i = 0; i < count; i++)
    {
        arguments[at++] = printers[i];
        arguments[at++] = printers[i];
    }
    char *choice = NULL;
    LxHostSaveResult result = dialog(arguments, &choice);
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
    char *arguments[] = {
        "zenity",
        "--list",
        "--title=Print Destination",
        "--text=PDF saves a document. PCL prints to a compatible printer or saves a PCL file.",
        "--column=Device",
        "--column=Description",
        "--hide-column=1",
        "--print-column=1",
        "pdf",
        "Save as PDF",
        "pcl",
        "PCL Printer",
        NULL};
    char *choice = NULL;
    LxHostSaveResult result = dialog(arguments, &choice);
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
