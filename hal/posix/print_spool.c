/* Public domain. No rights reserved. Wholly AI-authored. */
/* CUPS command-line adapters pass printer names and titles as literal argv.
   PCL jobs go through raw submission, preserving the generated printer data. */
#define _POSIX_C_SOURCE 200809L
#include "hal/print.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void lx_host_print_destination_clear(LxHostPrintDestination *destination)
{
    if (destination)
    {
        free(destination->path);
        free(destination->printer);
        memset(destination, 0, sizeof *destination);
    }
}

static int printer_name(const char *name)
{
    if (!name || !*name || *name == '-' || strlen(name) > 1024)
        return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '/' || *p == '#')
            return 0;
    return 1;
}

static int wait_child(pid_t child)
{
    if (child <= 0)
        return 0;
    int status;
    pid_t result;
    do
        result = waitpid(child, &status, 0);
    while (result < 0 && errno == EINTR);
    return result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static char *cups_program(const char *name)
{
    const char *directory = getenv("TWOINE_CUPS_BINDIR");
    if (!directory || !*directory)
        directory = "/usr/bin";
    if (*directory != '/')
        return NULL;
    size_t size = strlen(directory) + strlen(name) + 2;
    char *path = malloc(size);
    if (path)
        snprintf(path, size, "%s/%s", directory, name);
    return path;
}

char **lx_host_print_printers(size_t *count)
{
    if (!count)
        return NULL;
    *count = 0;
    int descriptors[2];
    if (pipe(descriptors))
        return NULL;
    char *program = cups_program("lpstat");
    if (!program)
    {
        close(descriptors[0]);
        close(descriptors[1]);
        return NULL;
    }
    pid_t child = fork();
    if (!child)
    {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(descriptors[1]);
        execl(program, "lpstat", "-a", (char *)NULL);
        _exit(127);
    }
    free(program);
    close(descriptors[1]);
    const size_t capacity = 65536;
    char *output = malloc(capacity);
    size_t used = 0;
    int ok = child > 0 && output;
    while (ok && used < capacity - 1)
    {
        ssize_t got = read(descriptors[0], output + used, capacity - used - 1);
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0)
            ok = 0;
        if (got <= 0)
            break;
        used += (size_t)got;
    }
    close(descriptors[0]);
    ok = wait_child(child) && ok && used < capacity - 1;
    if (!ok)
    {
        free(output);
        return NULL;
    }
    output[used] = 0;
    char **printers = NULL;
    for (char *line = output; *line;)
    {
        char *end = strchr(line, '\n');
        if (end)
            *end = 0;
        char *space = strpbrk(line, " \t");
        if (space)
            *space = 0;
        if (printer_name(line))
        {
            char **next = realloc(printers, (*count + 1) * sizeof *next);
            if (!next)
                break;
            printers = next;
            printers[*count] = strdup(line);
            if (!printers[*count])
                break;
            (*count)++;
        }
        if (!end)
            break;
        line = end + 1;
    }
    free(output);
    return printers;
}

int lx_host_print_submit_helper(const char *program, const char *printer, const char *title,
                                unsigned copies, FILE *source)
{
    if (!printer_name(printer) || !source || !copies || copies > 999 || fflush(source) ||
        fseek(source, 0, SEEK_SET))
        return 0;
    char number[16];
    snprintf(number, sizeof number, "%u", copies);
    if (!program || *program != '/')
        return 0;
    pid_t child = fork();
    if (!child)
    {
        if (dup2(fileno(source), STDIN_FILENO) < 0)
            _exit(127);
        if (fileno(source) > STDERR_FILENO)
            close(fileno(source));
        execl(program, program, "-d", printer, "-n", number, "-t",
              title && *title ? title : "2ine Print Job", "-o", "raw", "--", "-", (char *)NULL);
        _exit(127);
    }
    int ok = wait_child(child);
    if (!ok)
        fprintf(
            stderr,
            "2ine: The system spooler did not accept the PCL print job. Check the printer and CUPS service.\n");
    return ok;
}

int lx_host_print_submit_pcl(const char *printer, const char *title, unsigned copies, FILE *source)
{
    char *program = cups_program("lp");
    int ok = program && lx_host_print_submit_helper(program, printer, title, copies, source);
    free(program);
    return ok;
}
