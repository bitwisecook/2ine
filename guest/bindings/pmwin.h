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
static uint32 bridge32_WinWindowFromID(uint8 *a)
{
    return WinWindowFromID(pm_word(a, 0), pm_word(a, 1));
}
static uint32 bridge32_WinQueryWindowULong(uint8 *a)
{
    return WinQueryWindowULong(pm_word(a, 0), (LONG)pm_word(a, 1));
}
static uint32 bridge32_WinQueryWindowUShort(uint8 *a)
{
    return WinQueryWindowUShort(pm_word(a, 0), (LONG)pm_word(a, 1));
}
static uint32 bridge32_WinSetWindowUShort(uint8 *a)
{
    return WinSetWindowUShort(pm_word(a, 0), (LONG)pm_word(a, 1), (USHORT)pm_word(a, 2));
}
static uint32 bridge32_WinSetWindowULong(uint8 *a)
{
    return WinSetWindowULong(pm_word(a, 0), (LONG)pm_word(a, 1), pm_word(a, 2));
}
static uint32 bridge32_WinQueryWindowPtr(uint8 *a)
{
    return (uint32)(uintptr_t)WinQueryWindowPtr(pm_word(a, 0), (LONG)pm_word(a, 1));
}
static uint32 bridge32_WinSetWindowPtr(uint8 *a)
{
    return WinSetWindowPtr(pm_word(a, 0), (LONG)pm_word(a, 1), (void *)(uintptr_t)pm_word(a, 2));
}
static uint32 bridge32_WinLoadString(uint8 *a)
{
    const LONG capacity = (LONG)pm_word(a, 3);
    if (capacity <= 0)
        return 0;
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GLoaderState.execution, pm_word(a, 4), (uint32)capacity,
                             LX_GUEST_WRITE, &output))
        return 0;
    uint32 result =
        (uint32)WinLoadString(pm_word(a, 0), pm_word(a, 1), pm_word(a, 2), capacity, output.host);
    lx_execution_release(&output);
    return result;
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

static uint32 bridge32_WinQuerySysColor(uint8 *a)
{
    return (uint32)WinQuerySysColor(pm_word(a, 0), (LONG)pm_word(a, 1), (LONG)pm_word(a, 2));
}
static uint32 bridge32_WinQuerySysValue(uint8 *a)
{
    return (uint32)WinQuerySysValue(pm_word(a, 0), (LONG)pm_word(a, 1));
}

static uint32 bridge32_WinSubclassWindow(uint8 *a)
{
    return (uint32)(uintptr_t)WinSubclassWindow(pm_word(a, 0), (PFNWP)(uintptr_t)pm_word(a, 1));
}

static uint32 bridge32_WinGetPS(uint8 *a)
{
    return WinGetPS(pm_word(a, 0));
}
static uint32 bridge32_WinGetScreenPS(uint8 *a)
{
    return WinGetScreenPS(pm_word(a, 0));
}
static uint32 bridge32_WinReleasePS(uint8 *a)
{
    return WinReleasePS(pm_word(a, 0));
}

static uint32 bridge32_WinBeginEnumWindows(uint8 *a)
{
    return WinBeginEnumWindows(pm_word(a, 0));
}
static uint32 bridge32_WinEndEnumWindows(uint8 *a)
{
    return WinEndEnumWindows(pm_word(a, 0));
}
static uint32 bridge32_WinGetNextWindow(uint8 *a)
{
    return WinGetNextWindow(pm_word(a, 0));
}
static uint32 bridge32_WinQueryAnchorBlock(uint8 *a)
{
    return WinQueryAnchorBlock(pm_word(a, 0));
}
static uint32 bridge32_WinQueryWindow(uint8 *a)
{
    return WinQueryWindow(pm_word(a, 0), (LONG)pm_word(a, 1));
}
static uint32 bridge32_WinIsWindow(uint8 *a)
{
    return WinIsWindow(pm_word(a, 0), pm_word(a, 1));
}
static uint32 bridge32_WinIsWindowEnabled(uint8 *a)
{
    return WinIsWindowEnabled(pm_word(a, 0));
}
static uint32 bridge32_WinIsWindowVisible(uint8 *a)
{
    return WinIsWindowVisible(pm_word(a, 0));
}
static uint32 bridge32_WinQueryClassName(uint8 *a)
{
    const LONG capacity = (LONG)pm_word(a, 1);
    if (capacity <= 0)
        return 0;
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GLoaderState.execution, pm_word(a, 2), (uint32)capacity,
                             LX_GUEST_WRITE, &output))
        return 0;
    uint32 result = (uint32)WinQueryClassName(pm_word(a, 0), capacity, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_WinSetPresParam(uint8 *a)
{
    uint32 size = pm_word(a, 2);
    if (!size || size > 65536)
        return 0;
    LxGuestBorrow data = {0};
    if (!lx_execution_borrow(GLoaderState.execution, pm_word(a, 3), size, LX_GUEST_READ, &data))
        return 0;
    uint32 result = WinSetPresParam(pm_word(a, 0), pm_word(a, 1), size, data.host);
    lx_execution_release(&data);
    return result;
}
static uint32 bridge32_WinQueryPresParam(uint8 *a)
{
    const uint32 pointer = pm_word(a, 3), capacity = pm_word(a, 4);
    LxGuestBorrow found = {0}, output = {0};
    uint32 result = 0;
    if ((!pointer ||
         lx_execution_borrow(GLoaderState.execution, pointer, 4, LX_GUEST_WRITE, &found)) &&
        (!capacity || lx_execution_borrow(GLoaderState.execution, pm_word(a, 5), capacity,
                                          LX_GUEST_WRITE, &output)))
    {
        ULONG id = 0;
        result = WinQueryPresParam(pm_word(a, 0), pm_word(a, 1), pm_word(a, 2),
                                   pointer ? &id : NULL, capacity, output.host, pm_word(a, 6));
        if (pointer && result)
            lx_guest_store_u32(found.host, id);
    }
    lx_execution_release(&output);
    lx_execution_release(&found);
    return result;
}
static uint32 bridge32_WinRemovePresParam(uint8 *a)
{
    return WinRemovePresParam(pm_word(a, 0), pm_word(a, 1));
}

static uint32 bridge32_WinShowWindow(uint8 *a)
{
    return WinShowWindow(pm_word(a, 0), pm_word(a, 1));
}

static uint32 bridge32_WinEnableWindow(uint8 *a)
{
    return WinEnableWindow(pm_word(a, 0), pm_word(a, 1));
}

static uint32 bridge32_WinCreateWindow(uint8 *a)
{
    uint32 class_pointer = pm_word(a, 1), title_pointer = pm_word(a, 2);
    BOOL predefined = class_pointer >= 0xffff0001 && class_pointer <= 0xffff0070;
    char *class_name = predefined ? NULL : pm_string(class_pointer);
    char *title = pm_string(title_pointer);
    uint32 result = 0;
    if ((predefined || class_name) && (!title_pointer || title))
        result =
            WinCreateWindow(pm_word(a, 0), predefined ? (PSZ)(uintptr_t)class_pointer : class_name,
                            title, pm_word(a, 3), (LONG)pm_word(a, 4), (LONG)pm_word(a, 5),
                            (LONG)pm_word(a, 6), (LONG)pm_word(a, 7), pm_word(a, 8), pm_word(a, 9),
                            pm_word(a, 10), pm_param(a, 11), pm_param(a, 12));
    free(title);
    free(class_name);
    return result;
}

static int pm_procedure(uint32 address)
{
    return !address || lx_execution_memory(GLoaderState.execution, address, 1, LX_GUEST_EXECUTE);
}
static uint32 bridge32_WinCreateDlg(uint8 *a)
{
    uint8 prefix[2];
    LxGuestBorrow data = {0};
    if (!pm_procedure(pm_word(a, 2)) ||
        !lx_execution_read(GLoaderState.execution, pm_word(a, 3), prefix, 2))
        return 0;
    uint32 size = lx_guest_load_u16(prefix);
    if (size < 14 ||
        !lx_execution_borrow(GLoaderState.execution, pm_word(a, 3), size, LX_GUEST_READ, &data))
        return 0;
    uint32 result = WinCreateDlg(pm_word(a, 0), pm_word(a, 1), (PFNWP)(uintptr_t)pm_word(a, 2),
                                 data.host, pm_param(a, 4));
    lx_execution_release(&data);
    return result;
}
static uint32 bridge32_WinLoadDlg(uint8 *a)
{
    if (!pm_procedure(pm_word(a, 2)))
        return 0;
    return WinLoadDlg(pm_word(a, 0), pm_word(a, 1), (PFNWP)(uintptr_t)pm_word(a, 2), pm_word(a, 3),
                      pm_word(a, 4), pm_param(a, 5));
}
static uint32 bridge32_WinDlgBox(uint8 *a)
{
    if (!pm_procedure(pm_word(a, 2)))
        return 0xffff;
    return WinDlgBox(pm_word(a, 0), pm_word(a, 1), (PFNWP)(uintptr_t)pm_word(a, 2), pm_word(a, 3),
                     pm_word(a, 4), pm_param(a, 5));
}
static uint32 bridge32_WinProcessDlg(uint8 *a)
{
    return WinProcessDlg(pm_word(a, 0));
}
static uint32 bridge32_WinPostQueueMsg(uint8 *a)
{
    return WinPostQueueMsg(pm_word(a, 0), pm_word(a, 1), pm_param(a, 2), pm_param(a, 3));
}
static uint32 bridge32_WinDismissDlg(uint8 *a)
{
    return WinDismissDlg(pm_word(a, 0), pm_word(a, 1));
}
static uint32 bridge32_WinDefDlgProc(uint8 *a)
{
    return (uint32)(uintptr_t)WinDefDlgProc(pm_word(a, 0), pm_word(a, 1), pm_param(a, 2),
                                            pm_param(a, 3));
}
static uint32 bridge32_WinSendDlgItemMsg(uint8 *a)
{
    return (uint32)(uintptr_t)WinSendDlgItemMsg(pm_word(a, 0), pm_word(a, 1), pm_word(a, 2),
                                                pm_param(a, 3), pm_param(a, 4));
}
static uint32 bridge32_WinMapDlgPoints(uint8 *a)
{
    uint32 count = pm_word(a, 2);
    if (count > 65536)
        return 0;
    LxGuestBorrow points = {0};
    if (count && !lx_execution_borrow(GLoaderState.execution, pm_word(a, 1), (size_t)count * 8,
                                      LX_GUEST_READ | LX_GUEST_WRITE, &points))
        return 0;
    uint32 result = WinMapDlgPoints(pm_word(a, 0), points.host, count, pm_word(a, 3));
    lx_execution_release(&points);
    return result;
}
static uint32 bridge32_WinSetWindowText(uint8 *a)
{
    char *text = pm_string(pm_word(a, 1));
    uint32 result = text ? WinSetWindowText(pm_word(a, 0), text) : 0;
    free(text);
    return result;
}
static uint32 bridge32_WinSetDlgItemText(uint8 *a)
{
    char *text = pm_string(pm_word(a, 2));
    uint32 result = text ? WinSetDlgItemText(pm_word(a, 0), pm_word(a, 1), text) : 0;
    free(text);
    return result;
}
static uint32 bridge32_WinQueryWindowTextLength(uint8 *a)
{
    return (uint32)WinQueryWindowTextLength(pm_word(a, 0));
}
static uint32 pm_query_text(uint8 *a, int item)
{
    LONG capacity = (LONG)pm_word(a, item ? 2 : 1);
    LxGuestBorrow output = {0};
    if (capacity <= 0 || !lx_execution_borrow(GLoaderState.execution, pm_word(a, item ? 3 : 2),
                                              (uint32)capacity, LX_GUEST_WRITE, &output))
        return 0;
    uint32 result = item ? WinQueryDlgItemText(pm_word(a, 0), pm_word(a, 1), capacity, output.host)
                         : (uint32)WinQueryWindowText(pm_word(a, 0), capacity, output.host);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinQueryWindowText(uint8 *a)
{
    return pm_query_text(a, 0);
}
static uint32 bridge32_WinQueryDlgItemText(uint8 *a)
{
    return pm_query_text(a, 1);
}
