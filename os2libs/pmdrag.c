/* Public domain. No rights reserved. Wholly AI-authored. */
/* Drag string handles retain independent, checked copies of caller strings. */
#include "os2libs/os2native.h"
#include "os2libs/pmdrag.h"
#include "os2libs/generated/pmdrag-lx.h"
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
