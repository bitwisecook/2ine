/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinCancelShutdown: a queue's answer to the WM_QUIT of a system shutdown. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/ps/pmmerge/pmwin/paint.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>

BOOL WinCancelShutdown(HMQ queue, BOOL cancel)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || a->message_queue.hmq != queue)
        return FALSE;
    /* TRUE keeps later shutdowns from this queue; either way pending quits are withdrawn. */
    MessageQueue *q = &a->message_queue;
    q->cancel_shutdown = cancel != 0;
    MessageQueueItem *previous = NULL, *next;
    for (MessageQueueItem *item = q->head; item; item = next)
    {
        next = item->next;
        if (item->qmsg.msg != WM_QUIT)
        {
            previous = item;
            continue;
        }
        if (previous)
            previous->next = next;
        else
            q->head = next;
        if (q->tail == item)
            q->tail = previous;
        item->next = q->free_pool;
        q->free_pool = item;
    }
    return TRUE;
}
