/* Public domain. No rights reserved. Wholly AI-authored. */
/* Entry point of the helper process, which starts with clean window-system state. */
#include "host/fatal.h"
#include "host/core/fatal/dialog_hooks.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void leave_quietly(int number)
{
    (void)number;
    _exit(LX_FATAL_EXIT_STATUS);
}

static int capture(const char *path, char **argv)
{
    FILE *file = fopen(path, "w");
    if (!file)
        return 1;
    fprintf(file, "title=%s\nreport=%s\nmessage=%s\n", argv[0], argv[2], argv[1]);
    return fclose(file) != 0;
}

int lx_host_fatal_dialog(int argc, char **argv)
{
    static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
    /* The presenter's own failure must not produce the crash dump it replaces. */
    for (size_t i = 0; i < sizeof signals / sizeof *signals; i++)
        signal(signals[i], leave_quietly);
    if (argc < 3)
        return 1;
    const char *mode = getenv("TWOINE_FATAL_DIALOG");
    if (mode && !strncmp(mode, "capture:", 8))
        return capture(mode + 8, argv);
    return lx_host_fatal_present(argv[0], argv[1], argv[2]);
}
