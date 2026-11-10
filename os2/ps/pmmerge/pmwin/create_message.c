/* Public domain. No rights reserved. Wholly AI-authored. */
/* WM_CREATE carries control data and a complete CREATESTRUCT in the callback's address space. */
#include "os2/include/private/pmwinp.h"
#include "emulator/abi.h"

/* The guest CREATESTRUCT is 13 32-bit fields; the class name and title follow it. */
enum
{
    GUEST_CREATESTRUCT_FIELDS = 13,
    GUEST_CREATESTRUCT_SIZE = GUEST_CREATESTRUCT_FIELDS * 4,
    MAX_CREATE_STRING = 65536
};

MRESULT sendCreateMessage(Window *window, const CREATESTRUCT *creation)
{
    LxExecution *execution = GProcessState.execution;
    if (!execution || !window->window_proc || (uintptr_t)window->window_proc > UINT32_MAX ||
        !lx_execution_memory(execution, (uint32)(uintptr_t)window->window_proc, 1,
                             LX_GUEST_EXECUTE))
        return sendMessage(window, WM_CREATE, creation->pCtlData, (PVOID)creation);
    const uintptr_t class_value = (uintptr_t)creation->pszClass;
    const int predefined =
        class_value >= WC_FRAME && class_value <= PM_INTEGER_ATOM_BASE + PM_LAST_PREDEFINED_CLASS;
    size_t class_size = !predefined && creation->pszClass ? strlen(creation->pszClass) + 1 : 0;
    size_t title_size = creation->pszText ? strlen(creation->pszText) + 1 : 0;
    if (class_size > MAX_CREATE_STRING || title_size > MAX_CREATE_STRING)
        return (MRESULT)(uintptr_t)TRUE;
    size_t size = (GUEST_CREATESTRUCT_SIZE + class_size + title_size + PM_GUEST_PAGE_SIZE - 1) &
                  ~(size_t)(PM_GUEST_PAGE_SIZE - 1);
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, size, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return (MRESULT)(uintptr_t)TRUE;
    uint8 *wire = lx_execution_mapping_host(mapping);
    uint32 address = lx_execution_mapping_address(mapping);
    uint32 values[GUEST_CREATESTRUCT_FIELDS] = {
        (uint32)(uintptr_t)creation->pPresParams,
        (uint32)(uintptr_t)creation->pCtlData,
        creation->id,
        creation->hwndInsertBehind,
        creation->hwndOwner,
        (uint32)creation->cy,
        (uint32)creation->cx,
        (uint32)creation->y,
        (uint32)creation->x,
        creation->flStyle,
        title_size ? address + GUEST_CREATESTRUCT_SIZE + (uint32)class_size : 0,
        predefined   ? (uint32)class_value
        : class_size ? address + GUEST_CREATESTRUCT_SIZE
                     : 0,
        creation->hwndParent};
    for (unsigned i = 0; i < GUEST_CREATESTRUCT_FIELDS; i++)
        lx_guest_store_u32(wire + 4 * i, values[i]);
    if (class_size)
        memcpy(wire + GUEST_CREATESTRUCT_SIZE, creation->pszClass, class_size);
    if (title_size)
        memcpy(wire + GUEST_CREATESTRUCT_SIZE + class_size, creation->pszText, title_size);
    MRESULT result = sendMessage(window, WM_CREATE, creation->pCtlData, (MPARAM)(uintptr_t)address);
    lx_execution_unmap(execution, mapping);
    return result;
}

MRESULT sendOwnerItem(Window *owner, ULONG id, OWNERITEM *item)
{
    LxExecution *execution = GProcessState.execution;
    if (!execution || (uintptr_t)owner->window_proc > UINT32_MAX ||
        !lx_execution_memory(execution, (uint32)(uintptr_t)owner->window_proc, 1, LX_GUEST_EXECUTE))
        return sendMessage(owner, WM_DRAWITEM, (MPARAM)(uintptr_t)id, item);
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, PM_GUEST_PAGE_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return 0;
    memcpy(lx_execution_mapping_host(mapping), item, sizeof *item);
    MRESULT result = sendMessage(owner, WM_DRAWITEM, (MPARAM)(uintptr_t)id,
                                 (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping));
    memcpy(item, lx_execution_mapping_host(mapping), sizeof *item);
    lx_execution_unmap(execution, mapping);
    return result;
}
