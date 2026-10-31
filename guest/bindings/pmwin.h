/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM parameters and QMSGs have 32-bit guest words even on a 64-bit host. */
static uint32 pm_word(const uint8 *a, unsigned index)
{
    return lx_guest_load_u32(a + index * 4);
}
static MPARAM pm_param(const uint8 *a, unsigned index)
{
    return (MPARAM)(uintptr_t)pm_word(a, index);
}
static char *pm_string(uint32 address)
{
    return address ? lx_execution_copy_string(GLoaderState.execution, address, 65536) : NULL;
}
static void pm_message_in(QMSG *q, const uint8 *p)
{
    q->hwnd = lx_guest_load_u32(p);
    q->msg = lx_guest_load_u32(p + 4);
    q->mp1 = (MPARAM)(uintptr_t)lx_guest_load_u32(p + 8);
    q->mp2 = (MPARAM)(uintptr_t)lx_guest_load_u32(p + 12);
    q->time = lx_guest_load_u32(p + 16);
    q->ptl.x = (LONG)lx_guest_load_u32(p + 20);
    q->ptl.y = (LONG)lx_guest_load_u32(p + 24);
    q->reserved = lx_guest_load_u32(p + 28);
}
static void pm_message_out(uint8 *p, const QMSG *q)
{
    uint32 values[8] = {
        q->hwnd, q->msg,           (uint32)(uintptr_t)q->mp1, (uint32)(uintptr_t)q->mp2,
        q->time, (uint32)q->ptl.x, (uint32)q->ptl.y,          q->reserved};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32(p + i * 4, values[i]);
}
static uint32 bridge32_WinInitialize(uint8 *a)
{
    return WinInitialize(pm_word(a, 0));
}
static uint32 bridge32_WinTerminate(uint8 *a)
{
    return WinTerminate(pm_word(a, 0));
}
static uint32 bridge32_WinGetLastError(uint8 *a)
{
    return WinGetLastError(pm_word(a, 0));
}
static uint32 bridge32_WinCreateMsgQueue(uint8 *a)
{
    return WinCreateMsgQueue(pm_word(a, 0), (LONG)pm_word(a, 1));
}
static uint32 bridge32_WinDestroyMsgQueue(uint8 *a)
{
    return WinDestroyMsgQueue(pm_word(a, 0));
}
static uint32 bridge32_WinDestroyWindow(uint8 *a)
{
    return WinDestroyWindow(pm_word(a, 0));
}
static uint32 bridge32_WinOpenWindowDC(uint8 *a)
{
    return WinOpenWindowDC(pm_word(a, 0));
}
static uint32 bridge32_WinSetWindowPos(uint8 *a)
{
    return WinSetWindowPos(pm_word(a, 0), pm_word(a, 1), (LONG)pm_word(a, 2), (LONG)pm_word(a, 3),
                           (LONG)pm_word(a, 4), (LONG)pm_word(a, 5), pm_word(a, 6));
}
static uint32 bridge32_WinStartTimer(uint8 *a)
{
    return WinStartTimer(pm_word(a, 0), pm_word(a, 1), pm_word(a, 2), pm_word(a, 3));
}
static uint32 bridge32_WinStopTimer(uint8 *a)
{
    return WinStopTimer(pm_word(a, 0), pm_word(a, 1), pm_word(a, 2));
}
static uint32 bridge32_WinRegisterClass(uint8 *a)
{
    char *name = pm_string(pm_word(a, 1));
    uint32 entry = pm_word(a, 2);
    if (!name || !lx_execution_memory(GLoaderState.execution, entry, 1, LX_GUEST_EXECUTE))
    {
        free(name);
        return 0;
    }
    uint32 result = WinRegisterClass(pm_word(a, 0), name, (PFNWP)(uintptr_t)entry, pm_word(a, 3),
                                     pm_word(a, 4));
    free(name);
    return result;
}
static uint32 bridge32_WinCreateStdWindow(uint8 *a)
{
    uint32 flags;
    LxGuestBorrow output = {0};
    if (!lx_execution_read(GLoaderState.execution, pm_word(a, 2), &flags, 4) ||
        (pm_word(a, 8) &&
         !lx_execution_borrow(GLoaderState.execution, pm_word(a, 8), 4, LX_GUEST_WRITE, &output)))
        return 0;
    char *class_name = pm_string(pm_word(a, 3)), *title = pm_string(pm_word(a, 4));
    if ((pm_word(a, 3) && !class_name) || (pm_word(a, 4) && !title))
    {
        free(class_name);
        free(title);
        lx_execution_release(&output);
        return 0;
    }
    HWND client = 0;
    uint32 result = WinCreateStdWindow(pm_word(a, 0), pm_word(a, 1), &flags, class_name, title,
                                       pm_word(a, 5), pm_word(a, 6), pm_word(a, 7), &client);
    if (output.host)
        lx_guest_store_u32(output.host, client);
    lx_execution_release(&output);
    free(class_name);
    free(title);
    return result;
}
static uint32 bridge32_WinGetMsg(uint8 *a)
{
    LxGuestBorrow output;
    if (!lx_execution_borrow(GLoaderState.execution, pm_word(a, 1), 32, LX_GUEST_WRITE, &output))
        return 0;
    QMSG q = {0};
    uint32 result = WinGetMsg(pm_word(a, 0), &q, pm_word(a, 2), pm_word(a, 3), pm_word(a, 4));
    pm_message_out(output.host, &q);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinDispatchMsg(uint8 *a)
{
    uint8 wire[32];
    QMSG q;
    if (!lx_execution_read(GLoaderState.execution, pm_word(a, 1), wire, sizeof wire))
        return 0;
    pm_message_in(&q, wire);
    return (uint32)(uintptr_t)WinDispatchMsg(pm_word(a, 0), &q);
}
static uint32 bridge32_WinDefWindowProc(uint8 *a)
{
    return (uint32)(uintptr_t)WinDefWindowProc(pm_word(a, 0), pm_word(a, 1), pm_param(a, 2),
                                               pm_param(a, 3));
}
static uint32 bridge32_WinSendMsg(uint8 *a)
{
    return (uint32)(uintptr_t)WinSendMsg(pm_word(a, 0), pm_word(a, 1), pm_param(a, 2),
                                         pm_param(a, 3));
}
static uint32 bridge32_WinPostMsg(uint8 *a)
{
    return WinPostMsg(pm_word(a, 0), pm_word(a, 1), pm_param(a, 2), pm_param(a, 3));
}
static uint32 bridge32_WinBeginPaint(uint8 *a)
{
    LxGuestBorrow output = {0};
    RECTL rect;
    if (pm_word(a, 2) &&
        !lx_execution_borrow(GLoaderState.execution, pm_word(a, 2), 16, LX_GUEST_WRITE, &output))
        return 0;
    uint32 result = WinBeginPaint(pm_word(a, 0), pm_word(a, 1), output.host ? &rect : NULL);
    if (result && output.host)
        memcpy(output.host, &rect, 16);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinEndPaint(uint8 *a)
{
    return WinEndPaint(pm_word(a, 0));
}
static uint32 bridge32_WinFillRect(uint8 *a)
{
    RECTL rect;
    if (!lx_execution_read(GLoaderState.execution, pm_word(a, 1), &rect, sizeof rect))
        return 0;
    return WinFillRect(pm_word(a, 0), &rect, (LONG)pm_word(a, 2));
}

static uint32 bridge32_WinQueryWindowPos(uint8 *a)
{
    LxGuestBorrow output;
    SWP value;
    if (!lx_execution_borrow(GLoaderState.execution, pm_word(a, 1), sizeof value, LX_GUEST_WRITE,
                             &output))
        return 0;
    uint32 result = WinQueryWindowPos(pm_word(a, 0), &value);
    if (result)
        memcpy(output.host, &value, sizeof value);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinQueryPointerPos(uint8 *a)
{
    LxGuestBorrow output;
    POINTL value;
    if (!lx_execution_borrow(GLoaderState.execution, pm_word(a, 1), sizeof value, LX_GUEST_WRITE,
                             &output))
        return 0;
    uint32 result = WinQueryPointerPos(pm_word(a, 0), &value);
    if (result)
        memcpy(output.host, &value, sizeof value);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinQueryWindowRect(uint8 *a)
{
    LxGuestBorrow output;
    RECTL value;
    if (!lx_execution_borrow(GLoaderState.execution, pm_word(a, 1), sizeof value, LX_GUEST_WRITE,
                             &output))
        return 0;
    uint32 result = WinQueryWindowRect(pm_word(a, 0), &value);
    if (result)
        memcpy(output.host, &value, sizeof value);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinQuerySysPointer(uint8 *a)
{
    return WinQuerySysPointer(pm_word(a, 0), (LONG)pm_word(a, 1), pm_word(a, 2));
}
