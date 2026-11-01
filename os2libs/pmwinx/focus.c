/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 focus and activation use the current PM thread's actual window state. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin.h"
ULONG GetActiveWindow(void)
{
    return open32Presentation() ? WinQueryActiveWindow(HWND_DESKTOP) : 0;
}
ULONG SetActiveWindow(ULONG window)
{
    ULONG previous = GetActiveWindow();
    if (!WinSetActiveWindow(HWND_DESKTOP, window))
    {
        open32SetError(1400);
        return 0;
    }
    return previous;
}
ULONG GetFocus(void)
{
    return open32Presentation() ? WinQueryFocus(HWND_DESKTOP) : 0;
}
ULONG SetFocus(ULONG window)
{
    ULONG previous = GetFocus();
    if (!WinSetFocus(HWND_DESKTOP, window))
    {
        open32SetError(1400);
        return 0;
    }
    return previous;
}
