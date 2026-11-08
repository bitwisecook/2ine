/* Public domain. No rights reserved. Wholly AI-authored. */
/* Everything reachable from a signal handler avoids malloc, stdio and locks.
   A crashed process cannot safely run AppKit or X11, so the dialog belongs to a
   fresh image of this executable. The report is written in order of value: a
   nested fault or the watchdog skips the remaining stages but keeps the dialog. */
#define _GNU_SOURCE 1
#include "host/fatal.h"
#include "host/system.h"
#include "host/process.h"
#include "host/posix/fatal/report_hooks.h"
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef TWOINE_VERSION_TEXT
#define TWOINE_VERSION_TEXT "unknown"
#endif
extern char **environ;

enum
{
    MAX_CONTEXT_WRITERS = 4,
    BACKTRACE_DEPTH = 96,
    /* Holds backtrace symbolication; a guest thread only touches it on a fault. */
    SIGNAL_STACK_BYTES = 256 * 1024,
    /* Bounds a report blocked on a lock the failed thread already held. */
    WATCHDOG_SECONDS = 15
};

#if defined(TWOINE_WITH_UNICORN)
static const int FaultSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
#endif
/* Settings that explain a run. The whole environment is not copied into a file
   the user may share, because host environments can hold credentials. */
static const char *const ReportedEnvironment[] = {"TWOINE_BACKEND",      "TWOINE_BALL",
                                                  "TWOINE_CONFIG",       "TWOINE_DESKTOP_AUXILIARY",
                                                  "TWOINE_DESKTOP_NAME", "TWOINE_DESKTOP_SESSION",
                                                  "TWOINE_LIBPATH",      "IS_2INE",
                                                  "SDL_VIDEO_DRIVER",    "SDL_AUDIO_DRIVER",
                                                  "TRACE_NATIVE",        "TRACE_EVENTS"};

static void (*ContextWriters[MAX_CONTEXT_WRITERS])(void);
static unsigned ContextWriterCount;
static uintptr_t Reporter, Finisher;
static int Installed, Exiting, DialogEnabled, DialogReady, ReportDescriptor = -1;
static char Directory[1024], ProgramName[256];
static char Title[512], Message[3072], ReportPath[1280];
static char *Executable, *CommandLine;
static size_t CommandLength;
static char *DialogArguments[6];
static posix_spawnattr_t DialogAttributes;
static posix_spawn_file_actions_t DialogActions;

static void append(char *buffer, size_t size, const char *text)
{
    size_t used = strlen(buffer);
    while (*text && used + 1 < size)
        buffer[used++] = *text++;
    buffer[used] = '\0';
}

static void format_decimal(uint64_t value, char output[24])
{
    char digits[24];
    unsigned count = 0, used = 0;
    do
    {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count)
        output[used++] = digits[--count];
    output[used] = '\0';
}

static void format_hex(uint64_t value, unsigned digits, char output[20])
{
    static const char alphabet[] = "0123456789abcdef";
    if (digits > 16)
        digits = 16;
    for (unsigned i = 0; i < digits; i++)
        output[i] = alphabet[(value >> (4 * (digits - 1 - i))) & 15];
    output[digits] = '\0';
}

static void emit(const char *text, size_t length)
{
    /* A short or failed write cannot be reported anywhere better. */
    const int descriptors[2] = {STDERR_FILENO, ReportDescriptor};
    for (unsigned i = 0; i < 2; i++)
    {
        size_t written = 0;
        while (descriptors[i] >= 0 && written < length)
        {
            ssize_t result = write(descriptors[i], text + written, length - written);
            if (result < 0 && errno == EINTR)
                continue;
            if (result <= 0)
                break;
            written += (size_t)result;
        }
    }
}

void lx_host_fatal_write_bytes(const char *bytes, size_t size)
{
    emit(bytes, size);
}

void lx_host_fatal_write(const char *text)
{
    if (text)
        emit(text, strlen(text));
}

void lx_host_fatal_write_hex(uint64_t value, unsigned digits)
{
    char text[20];
    format_hex(value, digits, text);
    lx_host_fatal_write(text);
}

void lx_host_fatal_write_decimal(uint64_t value)
{
    char text[24];
    format_decimal(value, text);
    lx_host_fatal_write(text);
}

static void write_field(const char *name, const char *value)
{
    lx_host_fatal_write(name);
    lx_host_fatal_write(": ");
    lx_host_fatal_write(value && *value ? value : "(none)");
    lx_host_fatal_write("\n");
}

static __attribute__((noreturn)) void park(void)
{
    /* Another thread owns the report and ends this process. */
    for (;;)
        pause();
}

static void show_dialog(void)
{
    if (!DialogReady || !DialogEnabled || __atomic_load_n(&Exiting, __ATOMIC_ACQUIRE))
        return;
    pid_t child;
    if (posix_spawn(&child, Executable, &DialogActions, &DialogAttributes, DialogArguments,
                    environ))
        return;
    /* This worker stays alive behind the dialog: launchers treat its exit as the
       end of the session and may remove the application bundle it runs from. */
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
    {
    }
}

static __attribute__((noreturn)) void finish(void)
{
    uintptr_t self = (uintptr_t)pthread_self(), owner = 0;
    if (!__atomic_compare_exchange_n(&Finisher, &owner, self, 0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
    {
        if (owner == self)
            lx_host_process_exit(LX_FATAL_EXIT_STATUS);
        park();
    }
    alarm(0);
    int descriptor = ReportDescriptor;
    ReportDescriptor = -1;
    if (descriptor >= 0)
        close(descriptor);
    show_dialog();
    lx_host_process_exit(LX_FATAL_EXIT_STATUS);
}

static void on_watchdog(int number)
{
    (void)number;
    static const char text[] = "\n2ine fatal report: watchdog ended this report early\n";
    emit(text, sizeof text - 1);
    finish();
}

static void enter_report(void)
{
    uintptr_t self = (uintptr_t)pthread_self(), owner = 0;
    if (!__atomic_compare_exchange_n(&Reporter, &owner, self, 0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
    {
        if (owner == self)
            finish();
        park();
    }
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = on_watchdog;
    action.sa_flags = SA_ONSTACK | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, NULL);
    alarm(WATCHDOG_SECONDS);
}

static void civil_time(time_t seconds, int fields[6])
{
    /* Gregorian calendar from a day count; gmtime is not async-signal-safe. */
    int64_t days = (int64_t)seconds / 86400, remainder = (int64_t)seconds % 86400;
    int64_t shifted = days + 719468, era = shifted / 146097;
    int64_t day_of_era = shifted - era * 146097;
    int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int64_t month_index = (5 * day_of_year + 2) / 153;
    int month = (int)(month_index < 10 ? month_index + 3 : month_index - 9);
    fields[0] = (int)(year_of_era + era * 400 + (month <= 2));
    fields[1] = month;
    fields[2] = (int)(day_of_year - (153 * month_index + 2) / 5 + 1);
    fields[3] = (int)(remainder / 3600);
    fields[4] = (int)(remainder / 60 % 60);
    fields[5] = (int)(remainder % 60);
}

static void append_padded(char *buffer, size_t size, int value, unsigned width)
{
    char text[24];
    format_decimal((uint64_t)value, text);
    for (size_t length = strlen(text); length < width; length++)
        append(buffer, size, "0");
    append(buffer, size, text);
}

static void format_time(char *output, size_t size, const char *date_mark, const char *time_mark)
{
    int fields[6];
    civil_time(time(NULL), fields);
    output[0] = '\0';
    append_padded(output, size, fields[0], 4);
    append(output, size, date_mark);
    append_padded(output, size, fields[1], 2);
    append(output, size, date_mark);
    append_padded(output, size, fields[2], 2);
    append(output, size, *date_mark ? " " : "-");
    append_padded(output, size, fields[3], 2);
    append(output, size, time_mark);
    append_padded(output, size, fields[4], 2);
    append(output, size, time_mark);
    append_padded(output, size, fields[5], 2);
}

static void open_report(void)
{
    if (!Directory[0])
        return;
    char stamp[32], process[24];
    format_time(stamp, sizeof stamp, "", "");
    format_decimal((uint64_t)getpid(), process);
    mkdir(Directory, 0700);
    ReportPath[0] = '\0';
    append(ReportPath, sizeof ReportPath, Directory);
    append(ReportPath, sizeof ReportPath, "/fatal-");
    append(ReportPath, sizeof ReportPath, stamp);
    append(ReportPath, sizeof ReportPath, "-");
    append(ReportPath, sizeof ReportPath, process);
    append(ReportPath, sizeof ReportPath, ".txt");
    ReportDescriptor = open(ReportPath, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (ReportDescriptor < 0)
        ReportPath[0] = '\0';
}

static const char *kind_name(LxFatalKind kind)
{
    switch (kind)
    {
        case LX_FATAL_MISSING_IMPORT:
            return "missing-import";
        case LX_FATAL_GUEST_FAULT:
            return "guest-fault";
        case LX_FATAL_LOAD_FAILURE:
            return "load-failure";
        case LX_FATAL_INTERNAL:
            return "internal-error";
        default:
            return "host-fault";
    }
}

static const char *kind_explanation(LxFatalKind kind)
{
    switch (kind)
    {
        case LX_FATAL_MISSING_IMPORT:
            return "This program called an OS/2 function that 2ine does not provide yet, so it "
                   "cannot continue.";
        case LX_FATAL_GUEST_FAULT:
            return "This program performed an invalid operation and cannot continue. The fault may "
                   "be in the program or in how 2ine emulates OS/2.";
        case LX_FATAL_LOAD_FAILURE:
            return "2ine could not load this program or one of the modules it needs.";
        case LX_FATAL_INTERNAL:
            return "2ine detected an internal error and stopped this program. This is a bug in "
                   "2ine.";
        default:
            return "2ine crashed while running this program. This is a bug in 2ine.";
    }
}

static void compose_dialog(LxFatalKind kind, const char *summary)
{
    Title[0] = Message[0] = '\0';
    append(Title, sizeof Title, ProgramName);
    append(Title, sizeof Title,
           kind == LX_FATAL_LOAD_FAILURE ? " could not start" : " stopped unexpectedly");
    append(Message, sizeof Message, kind_explanation(kind));
    append(Message, sizeof Message, "\n\n");
    append(Message, sizeof Message, summary);
    if (kind != LX_FATAL_LOAD_FAILURE)
        append(Message, sizeof Message, "\n\nUnsaved work in this program has been lost.");
    if (ReportPath[0])
    {
        append(Message, sizeof Message, "\n\nA full report was saved to:\n");
        append(Message, sizeof Message, ReportPath);
    }
    else
        append(Message, sizeof Message, "\n\nThe details are in this session's log.");
}

static void write_identity(LxFatalKind kind, const char *summary)
{
    char text[32];
    lx_host_fatal_write("2ine execution: ");
    lx_host_fatal_write(summary);
    lx_host_fatal_write("\n\n-- 2ine fatal report --\n");
    write_field("kind", kind_name(kind));
    write_field("explanation", kind_explanation(kind));
    format_time(text, sizeof text, "-", ":");
    append(text, sizeof text, " UTC");
    write_field("time", text);
    write_field("program", ProgramName);
    write_field("2ine version", TWOINE_VERSION_TEXT);
    lx_host_fatal_write("host: ");
    lx_host_fatal_write(lx_host_os());
    lx_host_fatal_write(" ");
    lx_host_fatal_write(lx_host_arch());
    lx_host_fatal_write("\nprocess: ");
    lx_host_fatal_write_decimal((uint64_t)getpid());
    lx_host_fatal_write(" (parent ");
    lx_host_fatal_write_decimal((uint64_t)getppid());
    lx_host_fatal_write(")\n");
    write_field("executable", Executable);
    lx_host_fatal_write("command:");
    for (size_t i = 0; CommandLine && i < CommandLength; i += strlen(CommandLine + i) + 1)
    {
        lx_host_fatal_write(" [");
        lx_host_fatal_write(CommandLine + i);
        lx_host_fatal_write("]");
    }
    lx_host_fatal_write("\n");
    if (__atomic_load_n(&Exiting, __ATOMIC_ACQUIRE))
        lx_host_fatal_write("phase: the program had already requested exit\n");
}

static void write_environment(void)
{
    lx_host_fatal_write("\n-- Environment --\n");
    size_t count = sizeof ReportedEnvironment / sizeof *ReportedEnvironment;
    for (size_t i = 0; i < count; i++)
    {
        const char *value = getenv(ReportedEnvironment[i]);
        if (value)
            write_field(ReportedEnvironment[i], value);
    }
}

#if defined(TWOINE_WITH_UNICORN)
static const char *signal_name(int number)
{
    switch (number)
    {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGBUS:
            return "SIGBUS";
        case SIGILL:
            return "SIGILL";
        case SIGFPE:
            return "SIGFPE";
        case SIGABRT:
            return "SIGABRT";
        case SIGTRAP:
            return "SIGTRAP";
        case SIGSYS:
            return "SIGSYS";
        default:
            return "signal";
    }
}

static const char *signal_meaning(int number)
{
    switch (number)
    {
        case SIGSEGV:
        case SIGBUS:
            return "invalid memory access";
        case SIGILL:
            return "illegal host instruction";
        case SIGFPE:
            return "arithmetic fault";
        case SIGABRT:
            return "internal check failed";
        case SIGTRAP:
            return "trap instruction";
        default:
            return "bad system call";
    }
}
#endif
static void write_backtrace(void)
{
    void *frames[BACKTRACE_DEPTH];
    int count = backtrace(frames, BACKTRACE_DEPTH);
    lx_host_fatal_write("\n-- Host backtrace --\n");
    /* Raw addresses come first: symbolication can take the dynamic loader's lock. */
    for (int i = 0; i < count; i++)
    {
        lx_host_fatal_write_hex((uint64_t)(uintptr_t)frames[i], 16);
        lx_host_fatal_write("\n");
    }
    lx_host_fatal_write("symbols:\n");
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    if (ReportDescriptor >= 0)
        backtrace_symbols_fd(frames, count, ReportDescriptor);
}

static void write_report(LxFatalKind kind, const char *summary, const char *detail,
                         const void *context)
{
    open_report();
    compose_dialog(kind, summary);
    write_identity(kind, summary);
    if (detail && *detail)
    {
        lx_host_fatal_write("\n-- Details --\n");
        lx_host_fatal_write(detail);
        lx_host_fatal_write("\n");
    }
#if defined(TWOINE_WITH_UNICORN)
    if (context)
    {
        lx_host_fatal_write("\nhost registers: ");
        lx_host_fatal_write_registers(context);
        lx_host_fatal_write("\n");
    }
#else
    (void)context;
#endif
    write_environment();
    for (unsigned i = 0; i < ContextWriterCount; i++)
        ContextWriters[i]();
    write_backtrace();
    lx_host_fatal_write("\n-- Host images --\n");
    lx_host_fatal_write_images();
    lx_host_fatal_write("\n-- End of 2ine fatal report --\n");
}

void lx_host_fatal(LxFatalKind kind, const char *summary, const char *detail)
{
    enter_report();
    write_report(kind, summary, detail, NULL);
    finish();
}

#if defined(TWOINE_WITH_UNICORN)
static void on_signal(int number, siginfo_t *information, void *context)
{
    enter_report();
    static char summary[160];
    summary[0] = '\0';
    append(summary, sizeof summary, signal_name(number));
    append(summary, sizeof summary, " (");
    append(summary, sizeof summary, signal_meaning(number));
    append(summary, sizeof summary, ")");
    if (number != SIGABRT && information)
    {
        char address[20];
        format_hex((uint64_t)(uintptr_t)information->si_addr, 16, address);
        append(summary, sizeof summary, " at 0x");
        append(summary, sizeof summary, address);
    }
    write_report(number == SIGABRT ? LX_FATAL_INTERNAL : LX_FATAL_HOST_FAULT, summary, NULL,
                 context);
    finish();
}
#endif
void lx_host_fatal_add_context(void (*writer)(void))
{
    if (ContextWriterCount < MAX_CONTEXT_WRITERS)
        ContextWriters[ContextWriterCount++] = writer;
}

void lx_host_fatal_mark_exiting(void)
{
    __atomic_store_n(&Exiting, 1, __ATOMIC_RELEASE);
}

static const char *base_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    return name;
}

static void choose_program_name(void)
{
    const char *name = getenv("TWOINE_DESKTOP_NAME");
    if (!name || !*name)
        name = getenv("IS_2INE");
    if ((!name || !*name) && CommandLine)
    {
        const char *argument = CommandLine + strlen(CommandLine) + 1;
        if (argument < CommandLine + CommandLength && !strcmp(argument, "--operation"))
            argument += strlen(argument) + 1;
        if (argument < CommandLine + CommandLength)
            name = argument;
    }
    append(ProgramName, sizeof ProgramName, name && *name ? base_name(name) : "OS/2 program");
}

static int video_is_headless(const char *name)
{
    const char *driver = getenv(name);
    return driver && (!strcmp(driver, "dummy") || !strcmp(driver, "offscreen"));
}

static int dialog_wanted(void)
{
    /* Only ball launches default to a dialog: a direct lx_loader run reports on
       its terminal, and must never block an unattended caller on a modal alert. */
    const char *mode = getenv("TWOINE_FATAL_DIALOG");
    if (mode && *mode)
        return strcmp(mode, "0") != 0;
    if (!getenv("TWOINE_BALL") || video_is_headless("SDL_VIDEO_DRIVER") ||
        video_is_headless("SDL_VIDEODRIVER"))
        return 0;
    return lx_host_fatal_display_available();
}

static void prepare_dialog(void)
{
    DialogEnabled = dialog_wanted();
    Executable = lx_host_executable_path();
    if (!Executable || !DialogEnabled)
        return;
    sigset_t none, all;
    sigemptyset(&none);
    sigfillset(&all);
    /* All allocation happens now: the spawn itself may run in a signal handler. */
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (posix_spawnattr_init(&DialogAttributes))
        return;
    if (posix_spawn_file_actions_init(&DialogActions))
    {
        posix_spawnattr_destroy(&DialogAttributes);
        return;
    }
    flags |= lx_host_fatal_prepare_spawn(&DialogActions);
    posix_spawnattr_setflags(&DialogAttributes, flags);
    posix_spawnattr_setsigmask(&DialogAttributes, &none);
    posix_spawnattr_setsigdefault(&DialogAttributes, &all);
    DialogArguments[0] = Executable;
    DialogArguments[1] = (char *)LX_FATAL_DIALOG_ARGUMENT;
    DialogArguments[2] = Title;
    DialogArguments[3] = Message;
    DialogArguments[4] = ReportPath;
    DialogArguments[5] = NULL;
    DialogReady = 1;
}

static void choose_directory(void)
{
    const char *directory = getenv("TWOINE_FATAL_DIR");
    if (directory && *directory)
        append(Directory, sizeof Directory, directory);
    else if ((directory = getenv("TWOINE_BALL")) && *directory)
    {
        append(Directory, sizeof Directory, directory);
        append(Directory, sizeof Directory, "/logs");
    }
}

#if defined(TWOINE_WITH_UNICORN)
static void install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_signal;
    /* SA_NODEFER lets a fault in the handler reach the guard in enter_report
       instead of the kernel killing the process and dumping core. */
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof FaultSignals / sizeof *FaultSignals; i++)
        sigaction(FaultSignals[i], &action, NULL);
    /* Covers a death no handler can see, such as overflowing a thread that
       has no alternate stack. */
    struct rlimit limit;
    if (!getrlimit(RLIMIT_CORE, &limit))
    {
        limit.rlim_cur = 0;
        setrlimit(RLIMIT_CORE, &limit);
    }
    /* The first backtrace can load the unwinder, which a fault must not do. */
    void *frame;
    backtrace(&frame, 1);
}
#endif
void lx_host_fatal_install(void)
{
    if (Installed)
        return;
    Installed = 1;
    CommandLine = lx_host_command_line(&CommandLength);
    choose_directory();
    choose_program_name();
    prepare_dialog();
#if defined(TWOINE_WITH_UNICORN)
    const char *handler = getenv("TWOINE_FATAL_HANDLER");
    if (handler && !strcmp(handler, "0"))
        return;
    lx_host_fatal_thread_begin();
    install_signal_handlers();
#endif
}

void *lx_host_fatal_thread_begin(void)
{
#if defined(TWOINE_WITH_UNICORN)
    if (!Installed)
        return NULL;
    void *memory =
        mmap(NULL, SIGNAL_STACK_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED)
        return NULL;
    stack_t stack;
    memset(&stack, 0, sizeof stack);
    stack.ss_sp = memory;
    stack.ss_size = SIGNAL_STACK_BYTES;
    if (sigaltstack(&stack, NULL))
    {
        munmap(memory, SIGNAL_STACK_BYTES);
        return NULL;
    }
    return memory;
#else
    return NULL;
#endif
}

void lx_host_fatal_thread_end(void *memory)
{
    if (!memory)
        return;
    stack_t stack;
    memset(&stack, 0, sizeof stack);
    stack.ss_flags = SS_DISABLE;
    sigaltstack(&stack, NULL);
    munmap(memory, SIGNAL_STACK_BYTES);
}

int lx_host_fatal_is_dialog(void)
{
    size_t size = 0;
    char *command = lx_host_command_line(&size);
    int result = 0;
    if (command)
    {
        size_t first = strlen(command) + 1;
        result = first < size && !strcmp(command + first, LX_FATAL_DIALOG_ARGUMENT);
        free(command);
    }
    return result;
}
