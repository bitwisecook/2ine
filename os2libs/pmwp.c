/* Public domain. No rights reserved. Wholly AI-authored. */
/* No Workplace Shell objects exist in the current desktop provider. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwp.h"
#include "os2libs/generated/pmwp-lx.h"
BOOL WinSetObjectData(ULONG object, PSZ settings)
{
    (void)object;
    (void)settings;
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    if (anchor)
        anchor->last_error = 0x1709; /* WPERR_INVALID_OBJECT */
    return FALSE;
}
