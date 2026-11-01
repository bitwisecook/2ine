/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/input.h"
#include "hal/host.h"
#include "os2libs/pmshapi.h"
static void open32Shutdown(void);
#define LX_NATIVE_MODULE_SHUTDOWN() open32Shutdown()
#include "os2libs/generated/pmwinx-lx.h"

static uint32 references;
static void open32Shutdown(void)
{
    open32QueueCleanup();
    open32CaretCleanup();
    open32HooksCleanup();
    open32WindowsCleanup();
    open32MenusCleanup();
    open32ClipboardCleanup();
    open32DrawingCleanup();
    open32ClassesCleanup();
    open32IconsCleanup();
    open32CriticalCleanup();
    open32ContextCleanup();
    open32MemoryCleanup();
    references = 0;
}
BOOL WinXProcessMsg(PVOID message)
{
    const uint8 *wire = message;
    ULONG handle = wire ? lx_guest_load_u32(wire) : 0;
    if (!open32FindWindow(handle))
        return FALSE;
    QMSG native = {handle,
                   lx_guest_load_u32(wire + 4),
                   (MPARAM)(uintptr_t)lx_guest_load_u32(wire + 8),
                   (MPARAM)(uintptr_t)lx_guest_load_u32(wire + 12),
                   lx_guest_load_u32(wire + 16),
                   {(LONG)lx_guest_load_u32(wire + 20), (LONG)lx_guest_load_u32(wire + 24)},
                   lx_guest_load_u32(wire + 28)};
    WinDispatchMsg(open32Presentation(), &native);
    return TRUE;
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
