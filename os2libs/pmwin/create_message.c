/* Public domain. No rights reserved. Wholly AI-authored. */
/* WM_CREATE carries control data and a complete CREATESTRUCT in the callback's address space. */
#include "os2libs/pmwin/internal.h"
#include "guest/abi.h"

MRESULT sendCreateMessage(Window *window, const CREATESTRUCT *creation)
{
    LxExecution *execution = GLoaderState.execution;
    if (!execution || !window->window_proc || (uintptr_t)window->window_proc > UINT32_MAX ||
        !lx_execution_memory(execution, (uint32)(uintptr_t)window->window_proc, 1,
                             LX_GUEST_EXECUTE))
        return sendMessage(window, WM_CREATE, creation->pCtlData, (PVOID)creation);
    const uintptr_t class_value = (uintptr_t)creation->pszClass;
    const int predefined = class_value >= 0xffff0001 && class_value <= 0xffff0070;
    size_t class_size = !predefined && creation->pszClass ? strlen(creation->pszClass) + 1 : 0;
    size_t title_size = creation->pszText ? strlen(creation->pszText) + 1 : 0;
    if (class_size > 65536 || title_size > 65536)
        return (MRESULT)(uintptr_t)TRUE;
    size_t size = (52 + class_size + title_size + 4095) & ~(size_t)4095;
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, size, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return (MRESULT)(uintptr_t)TRUE;
    uint8 *wire = lx_execution_mapping_host(mapping);
    uint32 address = lx_execution_mapping_address(mapping);
    uint32 values[13] = {(uint32)(uintptr_t)creation->pPresParams,
                         (uint32)(uintptr_t)creation->pCtlData,
                         creation->id,
                         creation->hwndInsertBehind,
                         creation->hwndOwner,
                         (uint32)creation->cy,
                         (uint32)creation->cx,
                         (uint32)creation->y,
                         (uint32)creation->x,
                         creation->flStyle,
                         title_size ? address + 52 + (uint32)class_size : 0,
                         predefined   ? (uint32)class_value
                         : class_size ? address + 52
                                      : 0,
                         creation->hwndParent};
    for (unsigned i = 0; i < 13; i++)
        lx_guest_store_u32(wire + 4 * i, values[i]);
    if (class_size)
        memcpy(wire + 52, creation->pszClass, class_size);
    if (title_size)
        memcpy(wire + 52 + class_size, creation->pszText, title_size);
    MRESULT result = sendMessage(window, WM_CREATE, creation->pCtlData, (MPARAM)(uintptr_t)address);
    lx_execution_unmap(execution, mapping);
    return result;
}

MRESULT sendOwnerItem(Window *owner, ULONG id, OWNERITEM *item)
{
    LxExecution *execution = GLoaderState.execution;
    if (!execution || (uintptr_t)owner->window_proc > UINT32_MAX ||
        !lx_execution_memory(execution, (uint32)(uintptr_t)owner->window_proc, 1, LX_GUEST_EXECUTE))
        return sendMessage(owner, WM_DRAWITEM, (MPARAM)(uintptr_t)id, item);
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return 0;
    memcpy(lx_execution_mapping_host(mapping), item, sizeof *item);
    MRESULT result = sendMessage(owner, WM_DRAWITEM, (MPARAM)(uintptr_t)id,
                                 (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping));
    memcpy(item, lx_execution_mapping_host(mapping), sizeof *item);
    lx_execution_unmap(execution, mapping);
    return result;
}
