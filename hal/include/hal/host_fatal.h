/* Public domain. No rights reserved. Wholly AI-authored. */
/* Last-chance reporting for a worker that cannot continue. A report is written
   to stderr and to a file, a separate process presents the failure to the user,
   and the worker then ends with LX_FATAL_EXIT_STATUS instead of a host crash. */
#ifndef TWOINE_HOST_FATAL_H
#define TWOINE_HOST_FATAL_H
#include <stdint.h>

#define LX_FATAL_EXIT_STATUS 1
#define LX_FATAL_DIALOG_ARGUMENT "--fatal-dialog"

typedef enum LxFatalKind
{
    LX_FATAL_MISSING_IMPORT, /* The program needs an API that 2ine does not provide. */
    LX_FATAL_GUEST_FAULT,    /* The guest program itself faulted. */
    LX_FATAL_LOAD_FAILURE,   /* The program or one of its modules could not be loaded. */
    LX_FATAL_INTERNAL,       /* A 2ine invariant failed. */
    LX_FATAL_HOST_FAULT      /* The host process received a fatal signal. */
} LxFatalKind;

/* Installs the host-fault handlers for an emulated worker. Native i386 workers
   run guest code with guest segment registers, so they keep their own handler. */
void lx_host_fatal_install(void);
/* Gives a host thread the alternate signal stack a stack overflow needs. */
void *lx_host_fatal_thread_begin(void);
void lx_host_fatal_thread_end(void *stack);
/* A writer runs after the report's essentials and may call only the
   lx_host_fatal_write functions. It must take no locks and allocate nothing:
   it can run in a signal handler. A fault inside it ends that stage. */
void lx_host_fatal_add_context(void (*writer)(void));
void lx_host_fatal_write(const char *text);
void lx_host_fatal_write_hex(uint64_t value, unsigned digits);
void lx_host_fatal_write_decimal(uint64_t value);
/* Failures during an exit the program already requested are recorded without
   a dialog: the session is ending and the user has nothing left to act on. */
void lx_host_fatal_mark_exiting(void);
#if defined(_MSC_VER)
__declspec(noreturn)
#else
__attribute__((noreturn))
#endif
void lx_host_fatal(LxFatalKind kind, const char *summary, const char *detail);
/* True in the helper process started to present a report. */
int lx_host_fatal_is_dialog(void);
/* Presents title, message and report path from argv[0..2]. */
int lx_host_fatal_dialog(int argc, char **argv);
#endif
