/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Private base OS interfaces shared by 2ine's DLLs: the private companion of
   bsedos.h, as the DDK's bsep.h is for IBM's base OS. */

#ifndef _INCL_BSEP_H_
#define _INCL_BSEP_H_

#include "os2/include/bsedos.h"
#include "host/process.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* DOSCALLS entries absent from the Toolkit headers, and shared-memory and
       queue services for other 2ine DLLs. */

    /* Undocumented DOSCALLS header query used by Java runtimes. */
    enum
    {
        QHINF_EXEINFO = 1,
        QHINF_READRSRCTBL,
        QHINF_READFILE,
        QHINF_LIBPATHLENGTH,
        QHINF_LIBPATH,
        QHINF_FIXENTRY,
        QHINF_STE,
        QHINF_MAPSEL
    };

    OS2EXPORT APIRET OS2API DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer,
                                               ULONG cbBuffer, ULONG ulSubFunction);

    /* Undocumented DOSCALLS entry used by Java runtimes. */
    OS2EXPORT APIRET OS2API DosR3ExitAddr(void);

    OS2EXPORT ULONG OS2API DosFlatToSel(VOID);

    OS2EXPORT BOOL lx_dos_shared_address(ULONG address, ULONG size);

    OS2EXPORT APIRET DosGetSharedMemAddress(ULONG address, ULONG flags);

    OS2EXPORT ULONG OS2API DosSelToFlat(VOID);

    /* Register-driven entry; see os2/baseos/doscalls/doscall1/thunk.c. */
    OS2EXPORT APIRET OS2API THK_UNITHUNK(VOID);

    OS2EXPORT APIRET OS2API lx_queue_open_private(PPID owner, PHQUEUE output, const char *name);

    OS2EXPORT APIRET OS2API lx_queue_write_copy(HQUEUE handle, ULONG request, ULONG length,
                                                PVOID data, ULONG priority);

    /* Child launch with per-launch state. The caller's current directory and
   environment are never changed, so concurrent launches do not interact. */

    enum
    {
        LX_DOS_LAUNCH_SUSPENDED = 1,       /* Hold the child until lx_host_child_resume. */
        LX_DOS_LAUNCH_SEARCH_DIRECTORY = 2 /* Prefer a relative program in `directory`. */
    };

    typedef struct LxDosLaunch
    {
        PSZ program;     /* OS/2 path, or a bare name searched in the current directory and PATH. */
        PSZ arguments;   /* DosExecPgm block: program NUL arguments NUL NUL. May be NULL. */
        PSZ environment; /* Double-NUL block. NULL inherits the caller's environment. */
        PSZ directory;   /* OS/2 startup directory. NULL inherits the caller's. */
        const char *record; /* Shared LxLaunchRecord name for the child, or NULL. */
        ULONG flags;
    } LxDosLaunch;

    /* The observer runs on a thread leaving through DosExit, before it ends. */
    void lx_doscalls_observe_thread_exit(void (*observer)(ULONG code));
    /* Releases a DOS worker thread's join ownership; Open32 reaches it by name. */
    OS2EXPORT APIRET OS2API lx_doscalls_detach_thread(TID id);

    /* The caller owns the returned child, which DosWaitChild does not see. */
    APIRET lx_doscalls_launch(const LxDosLaunch *launch, LxHostChild **child);
    /* The child joins the DosWaitChild and DosKillProcess set; only its PID returns. */
    APIRET lx_doscalls_launch_tracked(const LxDosLaunch *launch, PID *pid);

    /* DosStartSession with launch state that STARTDATA cannot carry. */
    /* Runs on the session's wait thread, which has no PM anchor block. */
    typedef void (*LxSessionEnded)(void *context, ULONG result);

    typedef struct LxSessionStart
    {
        PSTARTDATA data;
        PSZ directory; /* OS/2 startup directory, or NULL to inherit the caller's. */
        LxSessionEnded ended;
        void *context;
    } LxSessionStart;

    APIRET lx_session_start(const LxSessionStart *start, PULONG session_id, PPID pid);
    /* This session's title: from Dos16SMSetTitle, else from its launcher, else empty. */
    void lx_session_title(char *output, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
