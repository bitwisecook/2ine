/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
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
        return NULLHANDLE;
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
        return NULLHANDLE;
    }

    anchor->windows_array_len = 16;
    anchor->windows = calloc(anchor->windows_array_len, sizeof *anchor->windows);
    if (!anchor->windows)
    {
        free(anchor);
        return NULLHANDLE;
    }

    anchor->pres_spaces_array_len = 16;
    anchor->pres_spaces =
        (PresentationSpace *)malloc(sizeof(PresentationSpace) * anchor->pres_spaces_array_len);
    if (!anchor->pres_spaces)
    {
        free(anchor->windows);
        free(anchor);
        return NULLHANDLE;
    }
    memset(anchor->pres_spaces, '\0', sizeof(PresentationSpace) * anchor->pres_spaces_array_len);

    const int ihab = lx_host_atomic_increment(&GHABCounter);
    if (ihab <= 0)
    {
        free(anchor->pres_spaces);
        destroyWindowEnumerations(anchor);
        free(anchor->windows);
        free(anchor);
        return NULLHANDLE;
    }

    const HAB hab = (HAB)ihab;
    anchor->hab = hab;
    posttib->anchor_block = anchor;

    return hab;
}

BOOL WinTerminate(HAB hab)
{
    TRACE_NATIVE("WinTerminate(%u)", (uint)hab);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
    {
        return FALSE;
    }

    for (size_t i = 0; i < anchor->pres_spaces_array_len; i++)
    {
        destroyPresentationSpace(anchor, &anchor->pres_spaces[i]);
    }
    free(anchor->pres_spaces);

    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        destroyWindow(anchor, anchor->windows[i]);
        free(anchor->windows[i]);
    }
    free(anchor->windows);

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
    free(anchor);
    lxHostThreadState()->anchor_block = NULL;

    lx_host_video_quit();

    return TRUE;
}

ERRORID WinGetLastError(HAB hab)
{
    TRACE_NATIVE("WinGetLastError(%u)", (uint)hab);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
    {
        return PMERR_INVALID_HAB;
    }
    const ERRORID retval = anchor->last_error;
    SET_WIN_ERROR_AND_RETURN(anchor, NO_ERROR, retval);
}
