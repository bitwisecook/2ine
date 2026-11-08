/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_DrgAddStrHandle(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a), 65536);
    if (!name)
        return 0;
    ULONG result = DrgAddStrHandle(name);
    free(name);
    return result;
}

static uint32 bridge32_DrgDeleteStrHandle(uint8 *a)
{
    return DrgDeleteStrHandle(lx_guest_load_u32(a));
}

static uint32 bridge32_DrgQueryStrNameLen(uint8 *a)
{
    return DrgQueryStrNameLen(lx_guest_load_u32(a));
}

static uint32 bridge32_DrgQueryStrName(uint8 *a)
{
    ULONG capacity = lx_guest_load_u32(a + 4);
    LxGuestBorrow span = {0};
    if (!capacity || !lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 8),
                                          capacity, LX_GUEST_WRITE, &span))
        return 0;
    ULONG result = DrgQueryStrName(lx_guest_load_u32(a), capacity, span.host);
    lx_execution_release(&span);
    return result;
}

static uint32 bridge32_DrgAccessDraginfo(uint8 *a)
{
    return (uint32)(uintptr_t)DrgAccessDraginfo(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgAllocDraginfo(uint8 *a)
{
    return (uint32)(uintptr_t)DrgAllocDraginfo(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgAllocDragtransfer(uint8 *a)
{
    return (uint32)(uintptr_t)DrgAllocDragtransfer(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgDeleteDraginfoStrHandles(uint8 *a)
{
    return (uint32)(uintptr_t)DrgDeleteDraginfoStrHandles(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgDrag(uint8 *a)
{
    return (uint32)(uintptr_t)DrgDrag(lx_guest_load_u32(a + 0), lx_guest_load_u32(a + 4),
                                      lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12),
                                      lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20));
}

static uint32 bridge32_DrgFreeDraginfo(uint8 *a)
{
    return (uint32)(uintptr_t)DrgFreeDraginfo(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgFreeDragtransfer(uint8 *a)
{
    return (uint32)(uintptr_t)DrgFreeDragtransfer(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgGetPS(uint8 *a)
{
    return (uint32)(uintptr_t)DrgGetPS(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgPostTransferMsg(uint8 *a)
{
    return (uint32)(uintptr_t)DrgPostTransferMsg(
        lx_guest_load_u32(a + 0), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8),
        lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20));
}

static uint32 bridge32_DrgQueryDragitemCount(uint8 *a)
{
    return (uint32)(uintptr_t)DrgQueryDragitemCount(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgQueryDragitemPtr(uint8 *a)
{
    return (uint32)(uintptr_t)DrgQueryDragitemPtr(lx_guest_load_u32(a + 0),
                                                  lx_guest_load_u32(a + 4));
}

static uint32 bridge32_DrgReleasePS(uint8 *a)
{
    return (uint32)(uintptr_t)DrgReleasePS(lx_guest_load_u32(a + 0));
}

static uint32 bridge32_DrgSendTransferMsg(uint8 *a)
{
    return (uint32)(uintptr_t)DrgSendTransferMsg(lx_guest_load_u32(a + 0), lx_guest_load_u32(a + 4),
                                                 (MPARAM)(uintptr_t)lx_guest_load_u32(a + 8),
                                                 (MPARAM)(uintptr_t)lx_guest_load_u32(a + 12));
}

static uint32 bridge32_DrgSetDragitem(uint8 *a)
{
    return (uint32)(uintptr_t)DrgSetDragitem(lx_guest_load_u32(a + 0), lx_guest_load_u32(a + 4),
                                             lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_DrgVerifyRMF(uint8 *a)
{
    ULONG mechanism = lx_guest_load_u32(a + 4), format = lx_guest_load_u32(a + 8);
    char *m =
        mechanism ? lx_execution_copy_string(GProcessState.execution, mechanism, 65536) : NULL;
    char *f = format ? lx_execution_copy_string(GProcessState.execution, format, 65536) : NULL;
    BOOL result = (!mechanism || m) && (!format || f) && DrgVerifyRMF(lx_guest_load_u32(a), m, f);
    free(m);
    free(f);
    return result;
}
