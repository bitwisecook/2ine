/* Public domain. No rights reserved. Wholly AI-authored. */
/* Queue input hooks run newest first and can consume or modify returned messages. */
#include "os2libs/pmwin/internal.h"
typedef struct PmHook
{
    struct PmHook *next;
    HMQ queue;
    PFN procedure;
    HMODULE module;
} PmHook;
BOOL WinSetHook(HAB hab, HMQ queue, LONG type, PFN procedure, HMODULE module)
{
    TRACE_NATIVE("WinSetHook(%u, %u, %d, %p, %u)", (uint)hab, (uint)queue, (int)type, procedure,
                 (uint)module);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !procedure || type != 1 ||
        (queue != 0 && queue != (HMQ)1 && queue != anchor->message_queue.hmq) ||
        (!queue && !module))
        return FALSE;
    PmHook *hook = malloc(sizeof *hook);
    if (!hook)
        return FALSE;
    *hook = (PmHook){anchor->hooks, anchor->message_queue.hmq, procedure, module};
    anchor->hooks = hook;
    return TRUE;
}
BOOL WinReleaseHook(HAB hab, HMQ queue, LONG type, PFN procedure, HMODULE module)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || type != 1 ||
        (queue != 0 && queue != (HMQ)1 && queue != anchor->message_queue.hmq))
        return FALSE;
    PmHook **link = &anchor->hooks;
    while (*link && ((*link)->procedure != procedure || (*link)->module != module))
        link = &(*link)->next;
    if (!*link)
        return FALSE;
    PmHook *hook = *link;
    *link = hook->next;
    free(hook);
    return TRUE;
}
void destroyHooks(AnchorBlock *anchor)
{
    while (anchor->hooks)
    {
        PmHook *hook = anchor->hooks;
        anchor->hooks = hook->next;
        free(hook);
    }
}
BOOL filterInputMessage(AnchorBlock *anchor, QMSG *message, ULONG flags)
{
    size_t count = 0;
    for (PmHook *h = anchor->hooks; h; h = h->next)
        count++;
    if (!count)
        return FALSE;
    PmHook *hooks = malloc(count * sizeof *hooks);
    if (!hooks)
        return FALSE;
    size_t i = 0;
    for (PmHook *h = anchor->hooks; h; h = h->next)
        hooks[i++] = *h;
    BOOL consumed = FALSE;
    for (i = 0; i < count && !consumed; i++)
    {
        LxExecution *execution = GLoaderState.execution;
        uint32 address = (uint32)(uintptr_t)hooks[i].procedure;
        if (execution && (uintptr_t)hooks[i].procedure <= UINT32_MAX &&
            lx_execution_memory(execution, address, 1, LX_GUEST_EXECUTE))
        {
            LxGuestMapping *mapping =
                lx_execution_map(execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
            if (!mapping)
                break;
            uint8 *wire = lx_execution_mapping_host(mapping);
            uint32 words[] = {message->hwnd,
                              message->msg,
                              (uint32)(uintptr_t)message->mp1,
                              (uint32)(uintptr_t)message->mp2,
                              message->time,
                              (uint32)message->ptl.x,
                              (uint32)message->ptl.y,
                              message->reserved};
            for (unsigned n = 0; n < 8; n++)
                lx_guest_store_u32(wire + n * 4, words[n]);
            consumed = lx_execution_callback32_4(execution, address, anchor->hab,
                                                 lx_execution_mapping_address(mapping), flags, 0);
            for (unsigned n = 0; n < 8; n++)
                words[n] = lx_guest_load_u32(wire + n * 4);
            *message = (QMSG){words[0],
                              words[1],
                              (MPARAM)(uintptr_t)words[2],
                              (MPARAM)(uintptr_t)words[3],
                              words[4],
                              {(LONG)words[5], (LONG)words[6]},
                              words[7]};
            lx_execution_unmap(execution, mapping);
        }
        else
        {
            typedef BOOL(OS2API * InputHook)(HAB, PQMSG, ULONG);
            consumed = ((InputHook)hooks[i].procedure)(anchor->hab, message, flags);
        }
    }
    free(hooks);
    return consumed;
}
