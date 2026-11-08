/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/video_frames.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/include/private/pmp.h"
#include "host/clipboard.h"
#include "os2/ps/pmmerge/pmwin/paint.h"
#include "emulator/abi.h"
#include <ctype.h>
static void cleanupThreadPM(void);
static void (*open32_thread_exit)(void);

void lx_pm_set_open32_thread_exit(void (*callback)(void))
{
    __atomic_store_n(&open32_thread_exit, callback, __ATOMIC_RELEASE);
}

/* OS/2 deduces the anchor from the calling thread; HAB arguments are advisory. */
AnchorBlock *getAnchorBlockNoHAB(void)
{
    return (AnchorBlock *)lxHostThreadState()->anchor_block;
}

AnchorBlock *getAnchorBlock(const HAB hab)
{
    (void)hab;
    return getAnchorBlockNoHAB();
}

ULONG currentSystemTicks(void)
{
    /* PM messages and host events share the multimedia clock. */
    return (uint32)lx_host_video_ticks_ms();
}

HAB WinInitialize(ULONG flOptions)
{
    TRACE_NATIVE("WinInitialize(%u)", (uint)flOptions);

    if (flOptions != 0)
    {
        return NULLHANDLE; // reserved; must be zero.
    }

    LxHostThreadState *posttib = lxHostThreadState();
    if (posttib->anchor_block != NULL)
    {
        return ((AnchorBlock *)posttib->anchor_block)->hab;
    }

    if (!lx_host_video_initialize(&desktop_window.w, &desktop_window.h))
    {
        fprintf(stderr, "Video initialization failed: %s\n", lx_host_video_error());
        return NULLHANDLE;
    }
    desktop_window.hwnd = HWND_DESKTOP;

    AnchorBlock *anchor = calloc(1, sizeof(AnchorBlock));
    if (anchor == NULL)
    {
        lx_host_video_quit();
        return NULLHANDLE;
    }

    anchor->windows_array_len = 16;
    anchor->windows = calloc(anchor->windows_array_len, sizeof *anchor->windows);
    if (!anchor->windows)
    {
        free(anchor);
        lx_host_video_quit();
        return NULLHANDLE;
    }

    anchor->pres_spaces_array_len = 16;
    anchor->pres_spaces =
        (PresentationSpace *)malloc(sizeof(PresentationSpace) * anchor->pres_spaces_array_len);
    if (!anchor->pres_spaces)
    {
        free(anchor->windows);
        free(anchor);
        lx_host_video_quit();
        return NULLHANDLE;
    }
    memset(anchor->pres_spaces, '\0', sizeof(PresentationSpace) * anchor->pres_spaces_array_len);

    const int ihab = lx_host_atomic_increment(&GHABCounter);
    if (ihab <= 0)
    {
        free(anchor->pres_spaces);
        free(anchor->windows);
        free(anchor);
        lx_host_video_quit();
        return NULLHANDLE;
    }

    const HAB hab = (HAB)ihab;
    anchor->hab = hab;
    anchor->object_window.hwnd = HWND_OBJECT;
    anchor->object_window.enabled = TRUE;
    posttib->anchor_block = anchor;
    posttib->pm_cleanup = cleanupThreadPM;
    posttib->pm_error = 0;

    return hab;
}

BOOL WinTerminate(HAB hab)
{
    TRACE_NATIVE("WinTerminate(%u)", (uint)hab);
    /* Warp retains the thread's PM state until thread/process teardown. */
    return TRUE;
}

static void cleanupThreadPM(void)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return;
    }
    void (*thread_exit)(void) = __atomic_load_n(&open32_thread_exit, __ATOMIC_ACQUIRE);
    if (thread_exit)
        thread_exit();
    WinCloseClipbrd(anchor->hab);
    lx_host_clipboard_materialize();

    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        destroyWindow(anchor, anchor->windows[i]);
    }
    retireAnchorMessages(anchor);
    for (size_t i = 0; i < anchor->pres_spaces_array_len; i++)
        destroyPresentationSpace(anchor, &anchor->pres_spaces[i]);
    cleanupErrorInfo();
    destroyAccelerators(anchor);
    destroyPointers(anchor);
    destroyHooks(anchor);
    lx_pm_region_cleanup(anchor);
    lx_pm_bitmap_cleanup(anchor);
    destroyDeviceContexts(anchor);
    destroyWindowEnumerations(anchor);
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        free(anchor->windows[i]);
    free(anchor->pres_spaces);
    free(anchor->windows);
    if (anchor->menu_bridge)
        lx_execution_unmap(GProcessState.execution, anchor->menu_bridge);
    if (anchor->control_bridge)
        lx_execution_unmap(GProcessState.execution, anchor->control_bridge);
    if (anchor->frame_control_bridge)
        lx_execution_unmap(GProcessState.execution, anchor->frame_control_bridge);

    WindowClass *winclassnext = NULL;
    for (WindowClass *i = anchor->registered_classes; i; i = winclassnext)
    {
        winclassnext = i->next;
        free(i->name);
        free(i);
    }

    if (anchor->message_queue.hmq != NULLHANDLE)
    {
        destroyMessageQueue(anchor);
    }

    while (anchor->timers)
    {
        PmTimer *next = anchor->timers->next;
        free(anchor->timers);
        anchor->timers = next;
    }
    lx_pm_dive_cleanup(anchor);
    lx_pm_video_cleanup(anchor);
    free(anchor);
    lxHostThreadState()->anchor_block = NULL;

    lx_host_video_quit();
}

ERRORID WinGetLastError(HAB hab)
{
    TRACE_NATIVE("WinGetLastError(%u)", (uint)hab);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
    {
        return PMERR_INVALID_HAB;
    }
    const ERRORID retval = lxHostThreadState()->pm_error;
    SET_WIN_ERROR_AND_RETURN(anchor, NO_ERROR, retval);
}

ERRORID WinSetErrorInfo(ERRORID error, ULONG flags, ...)
{
    TRACE_NATIVE("WinSetErrorInfo(%X, %X)", error, flags);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return PMERR_INVALID_HAB;
    ERRORID previous = lxHostThreadState()->pm_error;
    lxHostThreadState()->pm_error = error;
    return previous;
}

ULONG WinQueryCp(HMQ queue)
{
    TRACE_NATIVE("WinQueryCp(%u)", queue);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (!queue || anchor->message_queue.hmq != queue)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HMQ, 0);
    return anchor->message_queue.codepage ? anchor->message_queue.codepage : 437;
}

typedef struct ErrorAllocation
{
    LxGuestMapping *mapping;
    struct ErrorAllocation *next;
} ErrorAllocation;

static _Thread_local ErrorAllocation *error_allocations;

PVOID WinGetErrorInfo(HAB hab)
{
    AnchorBlock *a = getAnchorBlock(hab);
    if (!a || !lxHostThreadState()->pm_error)
        return NULL;
    ErrorAllocation *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->mapping =
        lx_execution_map(GProcessState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!p->mapping)
    {
        free(p);
        return NULL;
    }
    uint8 *out = lx_execution_mapping_host(p->mapping);
    /* ERRINFO, one message offset at 24, no binary data at 20, and the message at 40. The
       severity is the id's high word; PM sites that record a bare code are errors. */
    memset(out, 0, 40);
    ULONG error = WinGetLastError(hab), code = error & 0xffff;
    ULONG severity = error >> 16 ? error >> 16 : 8; /* SEVERITY_ERROR */
    char prefix[16], letters[32], text[256] = {0};
    if (!loadStringTable((HMODULE)-1, 5, 0, sizeof prefix, prefix))
        snprintf(prefix, sizeof prefix, "PMWIN");
    size_t known = (size_t)loadStringTable((HMODULE)-1, 5, 0x1021, sizeof letters, letters);
    char letter = known            ? letters[severity < known ? severity : known - 1]
                  : severity == 4  ? 'W'
                  : severity == 12 ? 'S'
                  : severity == 16 ? 'U'
                                   : 'E';
    if (!loadStringTable((HMODULE)-1, 10, code, sizeof text, text))
        loadStringTable((HMODULE)-1, 5, code, sizeof text, text);
    lx_guest_store_u32(out, 20);
    lx_guest_store_u32(out + 4, severity << 16 | code);
    lx_guest_store_u32(out + 8, 1);
    lx_guest_store_u32(out + 12, 24);
    lx_guest_store_u32(out + 16, 20);
    lx_guest_store_u32(out + 24, 40);
    snprintf((char *)out + 40, 4096 - 40, "%s%06X%c %s\r\n", prefix, (unsigned)code, letter, text);
    p->next = error_allocations;
    error_allocations = p;
    return (PVOID)(uintptr_t)lx_execution_mapping_address(p->mapping);
}

BOOL WinFreeErrorInfo(PVOID address)
{
    ErrorAllocation **link = &error_allocations;
    while (*link && (uintptr_t)address != lx_execution_mapping_address((*link)->mapping))
        link = &(*link)->next;
    if (!*link)
        return FALSE;
    ErrorAllocation *p = *link;
    *link = p->next;
    lx_execution_unmap(GProcessState.execution, p->mapping);
    free(p);
    return TRUE;
}

void cleanupErrorInfo(void)
{
    while (error_allocations)
        WinFreeErrorInfo(
            (PVOID)(uintptr_t)lx_execution_mapping_address(error_allocations->mapping));
}
