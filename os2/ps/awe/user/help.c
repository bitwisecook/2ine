/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinHelp opens no help viewer; only HELP_QUIT, which needs none, succeeds. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"

BOOL WinHelp(ULONG window, PSZ filename, ULONG command, ULONG data)
{
    TRACE_NATIVE("WinHelp(%u, '%s', %u, %u)", (uint)window, filename ? filename : "", (uint)command,
                 (uint)data);
    if (command == WIN32_HELP_QUIT)
        return filename != NULL;
    open32SetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
