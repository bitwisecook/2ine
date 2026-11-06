/* Public domain. No rights reserved. Wholly AI-authored. */
/* Child launch with per-launch state. The caller's current directory and
   environment are never changed, so concurrent launches do not interact. */
#ifndef TWOINE_OS2LIBS_DOSCALLS_LAUNCH_H
#define TWOINE_OS2LIBS_DOSCALLS_LAUNCH_H
#include "os2libs/os2types.h"
#include "hal/host_process.h"
enum
{
    LX_DOS_LAUNCH_SUSPENDED = 1,        /* Hold the child until lx_host_child_resume. */
    LX_DOS_LAUNCH_SEARCH_DIRECTORY = 2  /* Prefer a relative program in `directory`. */
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
/* The caller owns the returned child, which DosWaitChild does not see. */
APIRET lx_doscalls_launch(const LxDosLaunch *launch, LxHostChild **child);
/* The child joins the DosWaitChild and DosKillProcess set; only its PID returns. */
APIRET lx_doscalls_launch_tracked(const LxDosLaunch *launch, PID *pid);
#endif
