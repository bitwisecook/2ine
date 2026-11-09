/* Public domain. No rights reserved. Wholly AI-authored. */
/* KERNEL32 process-wide services: error state, versions, timing, debug output and the speaker. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/process.h"
#include "host/clock.h"
#include "host/window.h"
#include <stdio.h>

static _Thread_local ULONG last_error;
/* GetCommandLine's guest copy, made once per process. */
static ULONG command_line;

void open32SystemCleanup(void)
{
    command_line = 0;
}

BOOL Beep(ULONG frequency, ULONG duration)
{
    typedef APIRET(OS2API * Fn)(ULONG, ULONG);
    Fn call = (Fn)open32DosService("DosBeep");
    ULONG error = frequency == UINT32_MAX ? 50 : call ? call(frequency, duration) : 120;
    if (error == 395) /* ERROR_INVALID_FREQUENCY */
        error = 87;
    if (error)
        open32SetError(error);
    return !error;
}

VOID OutputDebugString(PSZ text)
{
    if (text)
        fprintf(stderr, "Open32: %s", text);
}

LONG MulDiv(LONG number, LONG numerator, LONG denominator)
{
    if (!denominator)
        return -1;
    int64_t product = (int64_t)number * numerator;
    const int negative = (product < 0) != (denominator < 0);
    if (product < 0)
        product = -product;
    int64_t divisor = denominator;
    if (divisor < 0)
        divisor = -divisor;
    int64_t result = (product + divisor / 2) / divisor;
    if (negative)
        result = -result;
    return result < INT32_MIN || result > INT32_MAX ? -1 : (LONG)result;
}

ULONG GetTickCount(void)
{
    /* GetMessageTime and native PM packets use this same millisecond clock. */
    return (ULONG)lx_host_video_ticks_ms();
}

ULONG timeGetTime(void)
{
    return GetTickCount();
}

ULONG GetCurrentTime(void)
{
    return GetTickCount();
}

VOID Sleep(ULONG milliseconds)
{
    lx_host_sleep_ms(milliseconds);
}

void open32SetError(ULONG error)
{
    TRACE_NATIVE("Open32 error %u", error);
    last_error = error;
}

ULONG GetLastError(void)
{
    return last_error;
}

VOID SetLastError(ULONG error)
{
    last_error = error;
}

ULONG GetVersion(void)
{
    /* Open32 reports Windows NT 3.51, with the major version in the low byte. */
    return 0x00003303;
}

ULONG GetCommandLine(void)
{
    ULONG result = __atomic_load_n(&command_line, __ATOMIC_ACQUIRE);
    if (result)
        return result;
    const char *program = GProcessState.pib.command;
    const char *tail = program ? program + strlen(program) + 1 : "";
    if (!program)
        program = "";
    size_t length = strlen(program) + strlen(tail) + 4;
    if (length > UINT32_MAX)
        return 0;
    ULONG address = LocalAlloc(0, (ULONG)length);
    if (!address)
        return 0;
    char *text = lx_execution_memory(GProcessState.execution, address, length,
                                     LX_GUEST_READ | LX_GUEST_WRITE);
    if (!text)
    {
        LocalFree(address);
        return 0;
    }
    snprintf(text, length, "\"%s\"%s%s", program, *tail ? " " : "", tail);
    if (__atomic_compare_exchange_n(&command_line, &result, address, 0, __ATOMIC_RELEASE,
                                    __ATOMIC_ACQUIRE))
        return address;
    LocalFree(address);
    return result;
}
