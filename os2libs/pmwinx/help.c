/* Public domain. No rights reserved. Wholly AI-authored. */
/* Help cleanup releases no viewer when the worker has not opened one. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
BOOL WinHelp(ULONG window, PSZ filename, ULONG command, ULONG data)
{
    TRACE_NATIVE("WinHelp(%u, '%s', %u, %u)", (uint)window, filename ? filename : "", (uint)command,
                 (uint)data);
    if (command == 2) /* HELP_QUIT */
        return filename != NULL;
    open32SetError(50);
    return FALSE;
}
