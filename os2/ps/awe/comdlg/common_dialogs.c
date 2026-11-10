/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hook and template helpers shared by ChooseColor and ChooseFont. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/comdlg/common_dialogs.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/include/private/pmp.h"
#include "os2/include/bsedos.h"

enum
{
    /* Bytes of the longest template name copied from the guest. */
    TEMPLATE_NAME_LIMIT = 1024
};

/* Translates one PM dialog message for a Windows hook; nonzero means the hook consumed it. */
ULONG open32CommonDialogHook(ULONG hook, ULONG window, ULONG message, ULONG first, ULONG second)
{
    if (!hook)
        return 0;
    ULONG kind, a, b;
    if (message == WM_COMMAND || message == WM_CONTROL)
    {
        ULONG child = WinWindowFromID(window, first & 0xffff);
        kind = WIN32_WM_COMMAND;
        a = message == WM_CONTROL ? open32ControlNotification(child, first) : first & 0xffff;
        b = child;
    }
    else if (message == WM_DESTROY)
    {
        kind = WIN32_WM_DESTROY;
        a = b = 0;
    }
    else
    {
        QMSG native = {.hwnd = window,
                       .msg = message,
                       .mp1 = (MPARAM)(uintptr_t)first,
                       .mp2 = (MPARAM)(uintptr_t)second};
        if (!open32PmMessage(&native, &kind, &a, &b))
            return 0;
    }
    return lx_execution_callback32_4(GProcessState.execution, hook, window, kind, a, b);
}

/* The template name is a resource number or a guest string. */
ULONG open32DialogTemplateIdentity(ULONG module, ULONG name)
{
    /* Like MAKEINTRESOURCE, a value below 64K is a resource number, not a string address. */
    if (name <= UINT16_MAX)
        return name;
    char *text = lx_execution_copy_string(GProcessState.execution, name, TEMPLATE_NAME_LIMIT);
    ULONG identity = text ? open32ResourceIdentity(module, text, RT_DIALOG) : 0;
    free(text);
    return identity;
}

ULONG open32DialogTemplateModule(ULONG module)
{
    return (!module || module == UINT32_MAX) && GProcessState.main_module
               ? GProcessState.main_module->handle
               : module;
}

ULONG open32GuestRecordAddress(PVOID record, ULONG size)
{
    ULONG address = 0;
    return lx_execution_address(GProcessState.execution, record, size, LX_GUEST_READ, &address)
               ? address
               : 0;
}
