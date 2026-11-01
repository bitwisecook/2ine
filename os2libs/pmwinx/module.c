/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host.h"
#include "os2libs/pmshapi.h"
static void open32Shutdown(void);
#define LX_NATIVE_MODULE_SHUTDOWN() open32Shutdown()
#include "os2libs/generated/pmwinx-lx.h"

static uint32 references;
static void open32Shutdown(void)
{
    open32WindowsCleanup();
    open32DrawingCleanup();
    open32ClassesCleanup();
    open32IconsCleanup();
    open32CriticalCleanup();
    open32ContextCleanup();
    references = 0;
}
BOOL WinXProcessMsg(PVOID message)
{
    (void)message;
    /* This worker creates OS/2 PM windows; no Open32 window owns their messages. */
    return FALSE;
}

VOID WinInitializeOpen32(ULONG reserved)
{
    TRACE_NATIVE("WinInitializeOpen32(%u)", (uint)reserved);
    __atomic_add_fetch(&references, 1, __ATOMIC_RELAXED);
}

VOID WinTerminateOpen32(ULONG reserved)
{
    TRACE_NATIVE("WinTerminateOpen32(%u)", (uint)reserved);
    uint32 count = __atomic_load_n(&references, __ATOMIC_RELAXED);
    while (count && !__atomic_compare_exchange_n(&references, &count, count - 1, 0,
                                                 __ATOMIC_RELAXED, __ATOMIC_RELAXED))
    {
    }
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

LONG WinCallWinMain(LONG argc, PSZ *argv, PFNWINMAIN entry, LONG show)
{
    TRACE_NATIVE("WinCallWinMain(%d, %p, %p, %d)", (int)argc, argv, entry, (int)show);
    (void)argc;
    (void)argv;
    if (!entry)
        return -1;
    char *command = GLoaderState.pib.pib_pchcmd;
    if (command)
        command += strlen(command) + 1;
    else
        command = "";
    if (GLoaderState.execution)
    {
        uint32 address;
        if ((uintptr_t)entry > UINT32_MAX ||
            !lx_execution_memory(GLoaderState.execution, (uint32)(uintptr_t)entry, 1,
                                 LX_GUEST_EXECUTE) ||
            !lx_execution_address(GLoaderState.execution, command, strlen(command) + 1,
                                  LX_GUEST_READ, &address))
            return -1;
        return (LONG)lx_execution_callback32_4(GLoaderState.execution, (uint32)(uintptr_t)entry,
                                               UINT32_MAX, 0, address, (uint32)show);
    }
    return entry(UINT32_MAX, 0, command, show);
}

ULONG GetTickCount(void)
{
    return (ULONG)(lx_host_monotonic_us() / 1000);
}

static _Thread_local ULONG last_error;
void open32SetError(ULONG error)
{
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
VOID Sleep(ULONG milliseconds)
{
    lx_host_sleep_ms(milliseconds);
}

ULONG GetUserDefaultLCID(void)
{
    /* The current NLS provider exposes United States country and English locale. */
    return 0x0409;
}

ULONG GetOEMCP(void)
{
    /* The NLS provider currently exposes CP437 as the process OEM code page. */
    return 437;
}

VOID GetSystemTime(PVOID output)
{
    uint64_t now = lx_host_realtime_us();
    LxHostCalendar value;
    if (!output || !lx_host_calendar((int64_t)(now / 1000000), 1, &value))
    {
        open32SetError(87);
        return;
    }
    uint16 fields[] = {(uint16)value.year,    (uint16)value.month,
                       (uint16)value.weekday, (uint16)value.day,
                       (uint16)value.hour,    (uint16)value.minute,
                       (uint16)value.second,  (uint16)((now % 1000000) / 1000)};
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u16((uint8 *)output + 2 * i, fields[i]);
}
