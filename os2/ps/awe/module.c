/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "host/clock.h"
#include "host/window.h"
#include "os2/include/pmshl.h"
#include "os2/include/private/pmp.h"
#include "os2/include/private/pmwinp.h"
static void open32Shutdown(void);
#define LX_NATIVE_MODULE_SHUTDOWN() open32Shutdown()
#include "os2/ps/awe/generated/pmwinx-lx.h"

static uint32 references;
/* The calling thread created its anchor block and queue for Open32 and destroys them. */
static _Thread_local BOOL owns_context;

static void open32Shutdown(void)
{
    lx_pm_set_open32_thread_exit(NULL);
    lx_pm_set_pointer_clip(NULL);
    open32TimersCleanup();
    open32FilesCleanup();
    open32SyncCleanup();
    open32DdeCleanup();
    open32DeferredCleanup();
    open32DialogsCleanup();
    open32QueueCleanup();
    open32CaretCleanup();
    open32HooksCleanup();
    open32WindowsCleanup();
    open32NativeControlsPrune();
    open32MenusCleanup();
    open32AcceleratorsCleanup();
    open32MetafilesCleanup();
    open32ClipboardCleanup();
    open32DrawingCleanup();
    open32ClassesCleanup();
    open32IconsCleanup();
    open32CriticalCleanup();
    open32TlsCleanup();
    open32ContextCleanup();
    open32MemoryCleanup();
    open32SystemCleanup();
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
    if (native.msg == LX_PM_OPEN32_POST)
        SendMessage(native.hwnd, native.reserved, (ULONG)(uintptr_t)native.mp1,
                    (ULONG)(uintptr_t)native.mp2);
    else
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

LONG WinCallWinMain(LONG argc, PSZ *argv, PFNWINMAIN entry, LONG show)
{
    TRACE_NATIVE("WinCallWinMain(%d, %p, %p, %d)", (int)argc, argv, entry, (int)show);
    (void)argc;
    (void)argv;
    if (!entry)
        return -1;
    char *command = GProcessState.pib.command;
    if (command)
        command += strlen(command) + 1;
    else
        command = "";
    if (GProcessState.execution)
    {
        uint32 address;
        if ((uintptr_t)entry > UINT32_MAX ||
            !lx_execution_memory(GProcessState.execution, (uint32)(uintptr_t)entry, 1,
                                 LX_GUEST_EXECUTE) ||
            !lx_execution_address(GProcessState.execution, command, strlen(command) + 1,
                                  LX_GUEST_READ, &address))
            return -1;
        return (LONG)lx_execution_callback32_4(GProcessState.execution, (uint32)(uintptr_t)entry,
                                               UINT32_MAX, 0, address, (uint32)show);
    }
    return entry(UINT32_MAX, 0, command, show);
}

ULONG open32Presentation(void)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    if (!anchor)
    {
        if (!WinInitialize(0))
            return 0;
        owns_context = TRUE;
        anchor = lxHostThreadState()->anchor_block;
    }
    if (!anchor->message_queue.hmq && !WinCreateMsgQueue(anchor->hab, 0))
        return 0;
    open32RegisterThreadQueue();
    return anchor->hab;
}

void open32ContextCleanup(void)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    open32UnregisterThreadQueue();
    if (owns_context && anchor)
    {
        if (anchor->message_queue.hmq)
            WinDestroyMsgQueue(anchor->message_queue.hmq);
        WinTerminate(anchor->hab);
    }
    owns_context = FALSE;
}
