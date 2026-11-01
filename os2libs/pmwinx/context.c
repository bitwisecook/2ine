/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 creates a PM anchor and queue lazily, reusing an existing thread context. */
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
static _Thread_local BOOL owns_context;
ULONG open32Presentation(void)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    if (!anchor)
    {
        if (!WinInitialize(0))
            return 0;
        owns_context = TRUE;
        anchor = lxHostThreadState()->anchor_block;
    }
    if (!anchor->message_queue.hmq && !WinCreateMsgQueue(anchor->hab, 0))
        return 0;
    return anchor->hab;
}
void open32ContextCleanup(void)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    if (owns_context && anchor)
    {
        if (anchor->message_queue.hmq)
            WinDestroyMsgQueue(anchor->message_queue.hmq);
        WinTerminate(anchor->hab);
    }
    owns_context = FALSE;
}
