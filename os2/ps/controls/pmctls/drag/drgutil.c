/* Public domain. No rights reserved. Wholly AI-authored. */
/* Drag string handles retain independent, checked copies of caller strings. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/private/pmp.h"
#include "os2/ps/controls/pmctls/drag/drag.h"

ULONG DrgAddStrHandle(PSZ name)
{
    if (!name)
        return 0;
    char *copy = strdup(name);
    if (!copy)
        return 0;
    ULONG handle = lx_handle_create(LX_HANDLE_PM_DRAG_STRING, copy, free);
    if (!handle)
        free(copy);
    return handle;
}

BOOL DrgDeleteStrHandle(ULONG string)
{
    return lx_handle_close(string, LX_HANDLE_PM_DRAG_STRING);
}

ULONG DrgQueryStrName(ULONG string, ULONG capacity, PSZ output)
{
    if (!output || !capacity)
        return 0;
    *output = 0;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(string, LX_HANDLE_PM_DRAG_STRING, &borrow))
        return 0;
    size_t length = strlen(borrow.resource), copied = length < capacity ? length : capacity - 1;
    memcpy(output, borrow.resource, copied);
    output[copied] = 0;
    lx_handle_release(&borrow);
    return (ULONG)copied;
}

ULONG DrgQueryStrNameLen(ULONG string)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(string, LX_HANDLE_PM_DRAG_STRING, &borrow))
        return 0;
    ULONG length = (ULONG)strlen(borrow.resource);
    lx_handle_release(&borrow);
    return length;
}

/* Drag structures are wire objects. Their public pointers always name mapped
   guest memory, and never expose the host allocation that tracks ownership. */
#include "emulator/abi.h"
#include "host/sync.h"
#include "host/clock.h"

enum
{
    /* DRAGTRANSFER in guest memory. */
    DRAGTRANSFER_FSREPLY_OFFSET = 26,
    DRAGTRANSFER_WIRE_SIZE = 28,
    /* DRAGINFO and DRAGTRANSFER counts are USHORTs. */
    DRAG_COUNT_MAX = UINT16_MAX,
    /* lx_execution_map allocates whole guest pages. */
    GUEST_PAGE_SIZE = 4096
};

/* The record a DragAllocation holds. */
enum
{
    DRAG_ALLOCATION_INFO = 1,
    DRAG_ALLOCATION_TRANSFER = 2
};

typedef struct DragAllocation
{
    struct DragAllocation *next;
    LxGuestMapping *mapping;
    ULONG address, size, kind, references;
    uint8 *shared; /* One flag per transfer element already handed to a receiver. */
} DragAllocation;

static DragAllocation *drag_allocations;
static LxHostOnce drag_once = LX_HOST_ONCE_INIT;
static LxHostMutex *drag_mutex;

static void drag_initialize(void)
{
    drag_mutex = lx_host_mutex_create(0, 0);
}

static ULONG drag_allocate(ULONG count, ULONG item_size, ULONG header, ULONG kind)
{
    if (!count || count > DRAG_COUNT_MAX || count > (UINT32_MAX - header) / item_size)
        return 0;
    ULONG size = header + count * item_size;
    DragAllocation *allocation = calloc(1, sizeof *allocation);
    if (!allocation)
        return 0;
    allocation->mapping = lx_execution_map(GProcessState.execution, 0,
                                           (size + GUEST_PAGE_SIZE - 1u) & ~(GUEST_PAGE_SIZE - 1u),
                                           LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!allocation->mapping)
    {
        free(allocation);
        return 0;
    }
    allocation->address = lx_execution_mapping_address(allocation->mapping);
    allocation->size = size;
    allocation->kind = kind;
    /* Each transfer element is freed separately; draginfo has one owner reference. */
    allocation->references = kind == DRAG_ALLOCATION_TRANSFER ? count : 1;
    if (kind == DRAG_ALLOCATION_TRANSFER && !(allocation->shared = calloc(count, 1)))
    {
        lx_execution_unmap(GProcessState.execution, allocation->mapping);
        free(allocation);
        return 0;
    }
    uint8 *wire = lx_execution_mapping_host(allocation->mapping);
    if (kind == DRAG_ALLOCATION_INFO)
    {
        lx_guest_store_u32(wire + DRAGINFO_CBDRAGINFO_OFFSET, size);
        lx_guest_store_u16(wire + DRAGINFO_CBDRAGITEM_OFFSET, item_size);
        lx_guest_store_u16(wire + DRAGINFO_USOPERATION_OFFSET, DO_DEFAULT);
        lx_guest_store_u16(wire + DRAGINFO_CDITEM_OFFSET, count);
    }
    else
        for (ULONG i = 0; i < count; i++)
            lx_guest_store_u32(wire + i * item_size, item_size);
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    allocation->next = drag_allocations;
    drag_allocations = allocation;
    lx_host_mutex_unlock(drag_mutex);
    return allocation->address;
}

ULONG DrgAllocDraginfo(ULONG count)
{
    return drag_allocate(count, DRAGITEM_WIRE_SIZE, DRAGINFO_WIRE_SIZE, DRAG_ALLOCATION_INFO);
}

ULONG DrgAllocDragtransfer(ULONG count)
{
    return drag_allocate(count, DRAGTRANSFER_WIRE_SIZE, 0, DRAG_ALLOCATION_TRANSFER);
}

BOOL drag_info(ULONG address, uint8 wire[DRAGINFO_WIRE_SIZE])
{
    if (!address || !lx_execution_read(GProcessState.execution, address, wire, DRAGINFO_WIRE_SIZE))
        return FALSE;
    ULONG size = lx_guest_load_u32(wire + DRAGINFO_CBDRAGINFO_OFFSET);
    ULONG item = lx_guest_load_u16(wire + DRAGINFO_CBDRAGITEM_OFFSET);
    ULONG count = lx_guest_load_u16(wire + DRAGINFO_CDITEM_OFFSET);
    return item >= DRAGITEM_WIRE_SIZE && size >= DRAGINFO_WIRE_SIZE &&
           count <= (size - DRAGINFO_WIRE_SIZE) / item &&
           lx_execution_memory(GProcessState.execution, address, size, LX_GUEST_READ) != NULL;
}

BOOL DrgAccessDraginfo(ULONG address)
{
    uint8 wire[DRAGINFO_WIRE_SIZE];
    if (!drag_info(address, wire))
        return FALSE;
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    for (DragAllocation *a = drag_allocations; a; a = a->next)
        if (a->address == address && a->kind == DRAG_ALLOCATION_INFO)
        {
            if (a->references == UINT32_MAX)
            {
                lx_host_mutex_unlock(drag_mutex);
                return FALSE;
            }
            a->references++;
            break;
        }
    lx_host_mutex_unlock(drag_mutex);
    return TRUE;
}

static BOOL drag_free(ULONG address, ULONG kind)
{
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    DragAllocation **link = &drag_allocations, *allocation = NULL;
    while (*link &&
           ((*link)->kind != kind || address < (*link)->address ||
            address - (*link)->address >= (kind == DRAG_ALLOCATION_TRANSFER ? (*link)->size : 1)))
        link = &(*link)->next;
    if (*link && !--(*link)->references)
    {
        allocation = *link;
        *link = allocation->next;
    }
    BOOL found = allocation || *link;
    lx_host_mutex_unlock(drag_mutex);
    if (allocation)
    {
        lx_execution_unmap(GProcessState.execution, allocation->mapping);
        free(allocation->shared);
        free(allocation);
    }
    return found;
}

BOOL DrgFreeDraginfo(ULONG address)
{
    return drag_free(address, DRAG_ALLOCATION_INFO);
}

BOOL DrgFreeDragtransfer(ULONG address)
{
    return drag_free(address, DRAG_ALLOCATION_TRANSFER);
}

ULONG DrgQueryDragitemCount(ULONG address)
{
    uint8 wire[DRAGINFO_WIRE_SIZE];
    return drag_info(address, wire) ? lx_guest_load_u16(wire + DRAGINFO_CDITEM_OFFSET) : 0;
}

ULONG DrgQueryDragitemPtr(ULONG address, ULONG index)
{
    uint8 wire[DRAGINFO_WIRE_SIZE];
    if (!drag_info(address, wire) || index >= lx_guest_load_u16(wire + DRAGINFO_CDITEM_OFFSET))
        return 0;
    return address + DRAGINFO_WIRE_SIZE +
           index * lx_guest_load_u16(wire + DRAGINFO_CBDRAGITEM_OFFSET);
}

BOOL DrgSetDragitem(ULONG address, ULONG item, ULONG bytes, ULONG index)
{
    uint8 wire[DRAGITEM_WIRE_SIZE];
    ULONG target = DrgQueryDragitemPtr(address, index),
          copied = bytes < DRAGITEM_WIRE_SIZE ? bytes : DRAGITEM_WIRE_SIZE;
    if (!target)
        return FALSE;
    return !copied || (lx_execution_read(GProcessState.execution, item, wire, copied) &&
                       lx_execution_write(GProcessState.execution, target, wire, copied));
}

BOOL DrgDeleteDraginfoStrHandles(ULONG address)
{
    uint8 header[DRAGINFO_WIRE_SIZE], item[DRAGITEM_WIRE_SIZE];
    if (!drag_info(address, header))
        return FALSE;
    ULONG count = lx_guest_load_u16(header + DRAGINFO_CDITEM_OFFSET),
          stride = lx_guest_load_u16(header + DRAGINFO_CBDRAGITEM_OFFSET);
    if (!count)
        return TRUE;

    /* Items may share a string handle, which is deleted only once. */
    enum
    {
        ITEM_STRINGS =
            (DRAGITEM_HSTRTARGETNAME_OFFSET - DRAGITEM_HSTRTYPE_OFFSET) / sizeof(ULONG) + 1
    };

    ULONG *strings = malloc((size_t)count * ITEM_STRINGS * sizeof *strings), found = 0;
    if (!strings)
        return FALSE;
    BOOL result = TRUE;
    for (ULONG i = 0; i < count && result; i++)
    {
        result = lx_execution_read(GProcessState.execution,
                                   address + DRAGINFO_WIRE_SIZE + i * stride, item, sizeof item);
        for (unsigned offset = DRAGITEM_HSTRTYPE_OFFSET;
             result && offset <= DRAGITEM_HSTRTARGETNAME_OFFSET; offset += sizeof(ULONG))
        {
            ULONG string = lx_guest_load_u32(item + offset), known = 0;
            while (known < found && strings[known] != string)
                known++;
            if (string && known == found)
                strings[found++] = string;
        }
    }
    for (ULONG i = 0; i < found && result; i++)
        result = DrgDeleteStrHandle(strings[i]);
    free(strings);
    return result;
}

static BOOL rmf_token(const char *begin, const char *end, const char *wanted)
{
    if (!wanted)
        return TRUE;
    while (begin < end)
    {
        while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == ','))
            begin++;
        const char *finish = begin;
        while (finish < end && *finish != ',')
            finish++;
        const char *trim = finish;
        while (trim > begin && (trim[-1] == ' ' || trim[-1] == '\t'))
            trim--;
        if ((size_t)(trim - begin) == strlen(wanted) && !memcmp(begin, wanted, trim - begin))
            return TRUE;
        begin = finish < end ? finish + 1 : end;
    }
    return FALSE;
}

BOOL DrgVerifyRMF(ULONG item_address, PSZ mechanism, PSZ format)
{
    uint8 item[DRAGITEM_WIRE_SIZE];
    if (!lx_execution_read(GProcessState.execution, item_address, item, sizeof item))
        return FALSE;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(lx_guest_load_u32(item + DRAGITEM_HSTRRMF_OFFSET),
                          LX_HANDLE_PM_DRAG_STRING, &borrow))
        return FALSE;
    const char *p = borrow.resource;
    BOOL result = FALSE;
    while (*p && !result)
    {
        if (*p == '<')
        {
            const char *comma = strchr(p + 1, ','), *end = strchr(p + 1, '>');
            if (!comma || !end || comma >= end)
                break;
            result = rmf_token(p + 1, comma, mechanism) && rmf_token(comma + 1, end, format);
            p = end + 1;
        }
        else if (*p == '(')
        {
            const char *end = strchr(p + 1, ')');
            if (!end)
                break;
            const char *second = end + 1;
            while (*second == ' ' || *second == '\t')
                second++;
            if (*second++ != 'x')
            {
                p = end + 1;
                continue;
            }
            while (*second == ' ' || *second == '\t')
                second++;
            if (*second != '(')
                break;
            const char *finish = strchr(second + 1, ')');
            if (!finish)
                break;
            result = rmf_token(p + 1, end, mechanism) && rmf_token(second + 1, finish, format);
            p = finish + 1;
        }
        else
            p++;
    }
    lx_handle_release(&borrow);
    return result;
}

/* The drag PS clips to siblings and children whatever the window's own styles. */
HPS DrgGetPS(HWND window)
{
    return lx_pm_get_clip_ps(window, PSF_CLIPSIBLINGS | PSF_CLIPCHILDREN);
}

BOOL DrgReleasePS(HPS space)
{
    return WinReleasePS(space);
}

/* Clears fsReply and gives the receiver its own free of the element, once per element. */
static void drag_transfer_sent(ULONG transfer, ULONG message)
{
    uint8 reply[2] = {0, 0};
    lx_execution_write(GProcessState.execution, transfer + DRAGTRANSFER_FSREPLY_OFFSET, reply,
                       sizeof reply);
    if (message != DM_RENDER && message != DM_RENDERPREPARE)
        return;
    lx_host_once(&drag_once, drag_initialize);
    lx_host_mutex_lock(drag_mutex, UINT32_MAX);
    for (DragAllocation *a = drag_allocations; a; a = a->next)
        if (a->kind == DRAG_ALLOCATION_TRANSFER && transfer >= a->address &&
            transfer - a->address < a->size)
        {
            ULONG element = (transfer - a->address) / DRAGTRANSFER_WIRE_SIZE;
            if (!a->shared[element])
            {
                a->shared[element] = 1;
                a->references++;
            }
            break;
        }
    lx_host_mutex_unlock(drag_mutex);
}

MRESULT DrgSendTransferMsg(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == DM_RENDER || message == DM_RENDERPREPARE)
        drag_transfer_sent((ULONG)(uintptr_t)first, message);
    return WinSendMsg(window, message, first, second);
}

BOOL DrgPostTransferMsg(HWND window, ULONG message, ULONG transfer, ULONG flags, ULONG reserved,
                        BOOL retry)
{
    (void)reserved;
    uint8 wire[DRAGTRANSFER_WIRE_SIZE];
    if (!lx_execution_read(GProcessState.execution, transfer, wire, sizeof wire) ||
        lx_guest_load_u32(wire) < sizeof wire)
        return FALSE;
    drag_transfer_sent(transfer, message);
    do
    {
        if (WinPostMsg(window, message, (MPARAM)(uintptr_t)transfer, (MPARAM)(uintptr_t)flags))
            return TRUE;
        if (!retry || !WinIsWindow(WinQueryAnchorBlock(window), window))
            return FALSE;
        lx_execution_poll_termination(GProcessState.execution);
        lx_host_sleep_ms(1);
    } while (retry);
    return FALSE;
}
