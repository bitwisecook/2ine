/* Public domain. No rights reserved. Wholly AI-authored. */
/* Each mode ends this process through one failure path. fatal_report.py checks
   the exit status, report and helper invocation from outside. */
#include "host/fatal.h"
#include "host/thread.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const char *Mode;

static void context_writer(void)
{
    lx_host_fatal_write("test context marker\n");
}

static void faulting_context(void)
{
    /* The stage after this must not run, but the dialog must still appear. */
    *(volatile int *)16 = 1;
}

static int overflow(int depth)
{
    volatile char block[8192];
    block[0] = (char)depth;
    return overflow(depth + 1) + block[0];
}

static void fail(void)
{
    if (!strcmp(Mode, "segv"))
        *(volatile int *)8 = 1;
    else if (!strcmp(Mode, "abort"))
        abort();
    else if (!strcmp(Mode, "trap"))
        __builtin_trap();
    else if (!strcmp(Mode, "raise-ill"))
        raise(SIGILL);
    else if (!strcmp(Mode, "raise-bus"))
        raise(SIGBUS);
    else if (!strcmp(Mode, "raise-fpe"))
        raise(SIGFPE);
    else if (!strcmp(Mode, "overflow"))
        printf("%d\n", overflow(0));
    else if (!strcmp(Mode, "explicit"))
        lx_host_fatal(LX_FATAL_MISSING_IMPORT, "Missing import TEST:ordinal #1", "detail marker");
    else if (!strcmp(Mode, "exiting"))
    {
        lx_host_fatal_mark_exiting();
        lx_host_fatal(LX_FATAL_GUEST_FAULT, "Fault during exit", NULL);
    }
    else if (!strcmp(Mode, "nested"))
    {
        lx_host_fatal_add_context(faulting_context);
        lx_host_fatal(LX_FATAL_INTERNAL, "Nested fault", NULL);
    }
    else if (!strcmp(Mode, "contended"))
    {
        /* The second failing thread parks while the first owns the report. */
        lx_host_fatal(LX_FATAL_INTERNAL, "Contended failure", NULL);
    }
}

static void *thread_main(void *argument)
{
    (void)argument;
    fail();
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], LX_FATAL_DIALOG_ARGUMENT))
    {
        const char *mode = getenv("TWOINE_FATAL_DIALOG");
        FILE *file = argc == 5 && mode ? fopen(mode + 8, "w") : NULL;
        if (!file)
            return 1;
        fprintf(file, "title=%s\nreport=%s\nmessage=%s\n", argv[2], argv[4], argv[3]);
        return fclose(file) != 0;
    }
    if (argc != 3)
        return 2;
    lx_host_fatal_install();
    lx_host_fatal_add_context(context_writer);
    Mode = argv[1];
    if (!strcmp(argv[2], "main"))
    {
        fail();
        return 3;
    }
    if (!strcmp(Mode, "contended"))
    {
        LxHostThread *other = lx_host_thread_create(thread_main, NULL, NULL);
        if (!other)
            return 4;
        fail();
        return 3;
    }
    LxHostThread *thread = lx_host_thread_create(thread_main, NULL, NULL);
    if (!thread)
        return 4;
    /* The failing thread ends the process. */
    sleep(30);
    return 3;
}
