/* Public domain. No rights reserved. Wholly AI-authored. */
/* DosStartSession with launch state that STARTDATA cannot carry. */
#ifndef TWOINE_OS2LIBS_SESMGR_START_H
#define TWOINE_OS2LIBS_SESMGR_START_H
#include "os2libs/sesmgr.h"
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
#endif
