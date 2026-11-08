/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Private Presentation Manager interfaces shared by 2ine's DLLs: the private
   companion of pmwin.h, as the DDK's pmp.h is for IBM's PM. */

#ifndef _INCL_PMP_H_
#define _INCL_PMP_H_

#include "os2/include/pmwin.h"
#include "os2/include/pmshl.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* PMWIN services for other DLLs: window calls, pointers, clipping, updates and the clipboard */

    OS2EXPORT ULONG lx_pm_clipboard_count(void);

    /* A NULL rectangle releases the desktop pointer. Bounds are inclusive PM desktop pixels. */
    OS2EXPORT void lx_pm_set_pointer_clip(const RECTL *bounds);

    /* Returns TRUE when the supplied PM desktop point was moved inside the clip. */
    OS2EXPORT BOOL lx_pm_clip_pointer(LONG *x, LONG *y);

    /* Runs function on the thread owning window, blocking until it finishes or replies. The
       argument block is copied there and back. FALSE when the window is on this thread, is
       unknown, or was destroyed first. WinSendMsg and every other queued send use this. */
    typedef uintptr_t (*LxPmWindowCall)(void *context);

    OS2EXPORT BOOL lx_pm_window_call(HWND window, LxPmWindowCall function, void *context,
                                     size_t size, uintptr_t *result);

    /* Releases the sender of the call running on this thread; FALSE when there is none. */
    OS2EXPORT BOOL lx_pm_reply_message(uintptr_t result);

    OS2EXPORT BOOL lx_pm_in_window_call(void);

    /* Object windows of every thread; returns the count, filling up to capacity. */
    OS2EXPORT size_t lx_pm_object_windows(HWND *output, size_t capacity);

    /* Drawing in the window and its descendants is discarded until the lock is cleared. */
    OS2EXPORT void lx_pm_set_update_lock(HWND window);

    /* A message box that its window frame cannot close. */
    OS2EXPORT ULONG lx_pm_message_box_no_close(HWND owner, PSZ text, PSZ caption, ULONG style);

    /* The callback runs on each exiting PM thread before its windows are destroyed. */
    OS2EXPORT void lx_pm_set_open32_thread_exit(void (*callback)(void));

    /* Shared system resources do not require a caller-owned PM anchor/display. */
    OS2EXPORT HPOINTER lx_pm_system_pointer(LONG id);

    /* A window PS clipped as requested by PSF_CLIPSIBLINGS and PSF_CLIPCHILDREN. */
    OS2EXPORT HPS lx_pm_get_clip_ps(HWND window, ULONG flags);

    /* Copies a window's pending update rectangles in PM coordinates, using its invalid
       rectangle when no damage list is held. The caller frees *rectangles; a nonzero *count
       with NULL *rectangles reports allocation failure. FALSE means no such window here. */
    OS2EXPORT BOOL lx_pm_window_damage(HWND window, RECTL **rectangles, size_t *count,
                                       LONG *height);

    /* Moves pending update rectangles inside area by dx and dy, clipped to area, in PM
       coordinates, as a scroll does. FALSE means no such window here or out of memory. */
    OS2EXPORT BOOL lx_pm_window_scroll_damage(HWND window, const RECTL *area, LONG dx, LONG dy);

    OS2EXPORT uint64_t lx_pm_clipboard_serial(void);

    OS2EXPORT uint64_t lx_pm_clipboard_format_serial(ULONG format);

    OS2EXPORT ULONG lx_pm_clipboard_format_name(ULONG format, char *output, ULONG capacity);

    OS2EXPORT BOOL lx_pm_clipboard_set_bytes(HAB hab, ULONG format, const void *bytes, size_t size);

    OS2EXPORT void *lx_pm_clipboard_copy_bytes(HAB hab, ULONG format, size_t *size);

    OS2EXPORT void lx_pm_clipboard_window_destroyed(HWND window);

    /* PMSHAPI */

    /* WinStartApp for callers that need the PM error without a PM anchor block.
       Returns the HAPP, or zero with the PMERR_ value in *error. */
    ULONG lx_pmshapi_start_app(HWND notify, PPROGDETAILS details, PSZ parameters, ULONG options,
                               ULONG *error);

    /* PMGRE graphics engine entries (no Toolkit header) */

    OS2EXPORT ULONG OS2API Gre32Entry3(ULONG first, ULONG second, ULONG operation);

    OS2EXPORT ULONG OS2API Gre32Entry5(ULONG first, ULONG second, ULONG third, ULONG fourth,
                                       ULONG operation);

    /* PMMERGE entries without a Toolkit declaration */

    OS2EXPORT ULONG OS2API WinQueryCpType(HMQ queue);

    /* PMWP */

    /* Starts the first program object whose ASSOCFILTER matches the file name. */
    BOOL lx_pmwp_open_associated(PSZ path);

#ifdef __cplusplus
}
#endif

#endif
