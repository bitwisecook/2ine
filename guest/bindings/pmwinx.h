/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_WinInitializeOpen32(uint8 *arguments)
{
    WinInitializeOpen32(lx_guest_load_u32(arguments));
    return 0;
}
static uint32 bridge32_WinTerminateOpen32(uint8 *arguments)
{
    WinTerminateOpen32(lx_guest_load_u32(arguments));
    return 0;
}
static uint32 bridge32_MulDiv(uint8 *arguments)
{
    return (uint32)MulDiv((LONG)lx_guest_load_u32(arguments),
                          (LONG)lx_guest_load_u32(arguments + 4),
                          (LONG)lx_guest_load_u32(arguments + 8));
}

static char *o32_string(uint32 pointer)
{
    return pointer ? lx_execution_copy_string(GLoaderState.execution, pointer, 32768) : NULL;
}
static uint32 bridge32_RegCloseKey(uint8 *a)
{
    return (uint32)RegCloseKey(lx_guest_load_u32(a));
}
static uint32 bridge32_RegOpenKeyEx(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = o32_string(pointer);
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!pointer || name) && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 16),
                                                  4, LX_GUEST_WRITE, &output))
    {
        ULONG handle = 0;
        result = (uint32)RegOpenKeyEx(lx_guest_load_u32(a), name, lx_guest_load_u32(a + 8),
                                      lx_guest_load_u32(a + 12), &handle);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}
static uint32 bridge32_RegCreateKeyEx(uint8 *a)
{
    const uint32 np = lx_guest_load_u32(a + 4), cp = lx_guest_load_u32(a + 12),
                 dp = lx_guest_load_u32(a + 32);
    char *name = o32_string(np), *class_name = o32_string(cp);
    LxGuestBorrow output = {0}, disposition = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!np || name) && (!cp || class_name) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 28), 4, LX_GUEST_WRITE,
                            &output) &&
        (!dp || lx_execution_borrow(GLoaderState.execution, dp, 4, LX_GUEST_WRITE, &disposition)))
    {
        ULONG handle = 0, created = 0;
        result = (uint32)RegCreateKeyEx(
            lx_guest_load_u32(a), name, lx_guest_load_u32(a + 8), class_name,
            lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20),
            (void *)(uintptr_t)lx_guest_load_u32(a + 24), &handle, dp ? &created : NULL);
        if (!result)
        {
            lx_guest_store_u32(output.host, handle);
            if (dp)
                lx_guest_store_u32(disposition.host, created);
        }
    }
    lx_execution_release(&disposition);
    lx_execution_release(&output);
    free(class_name);
    free(name);
    return result;
}
static uint32 bridge32_RegSetValueEx(uint8 *a)
{
    const uint32 np = lx_guest_load_u32(a + 4), dp = lx_guest_load_u32(a + 16),
                 size = lx_guest_load_u32(a + 20);
    char *name = o32_string(np);
    LxGuestBorrow data = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!np || name) &&
        (!size || lx_execution_borrow(GLoaderState.execution, dp, size, LX_GUEST_READ, &data)))
        result = (uint32)RegSetValueEx(lx_guest_load_u32(a), name, lx_guest_load_u32(a + 8),
                                       lx_guest_load_u32(a + 12), data.host, size);
    lx_execution_release(&data);
    free(name);
    return result;
}
static uint32 bridge32_RegQueryValueEx(uint8 *a)
{
    const uint32 np = lx_guest_load_u32(a + 4), rp = lx_guest_load_u32(a + 8),
                 tp = lx_guest_load_u32(a + 12), dp = lx_guest_load_u32(a + 16),
                 sp = lx_guest_load_u32(a + 20);
    char *name = o32_string(np);
    LxGuestBorrow type = {0}, size = {0}, data = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!np || name) && !rp &&
        (!tp || lx_execution_borrow(GLoaderState.execution, tp, 4, LX_GUEST_WRITE, &type)) &&
        (!sp || lx_execution_borrow(GLoaderState.execution, sp, 4, 3, &size)))
    {
        ULONG count = sp ? lx_guest_load_u32(size.host) : 0, kind = 0;
        uint8 empty = 0;
        if ((!dp || sp) &&
            (!dp || !count ||
             lx_execution_borrow(GLoaderState.execution, dp, count, LX_GUEST_WRITE, &data)))
        {
            result = (uint32)RegQueryValueEx(lx_guest_load_u32(a), name, NULL, tp ? &kind : NULL,
                                             dp ? (count ? data.host : &empty) : NULL,
                                             sp ? &count : NULL);
            if (result == NO_ERROR || result == ERROR_MORE_DATA)
            {
                if (sp)
                    lx_guest_store_u32(size.host, count);
                if (tp)
                    lx_guest_store_u32(type.host, kind);
            }
        }
    }
    lx_execution_release(&data);
    lx_execution_release(&size);
    lx_execution_release(&type);
    free(name);
    return result;
}

static uint32 bridge32_WinXProcessMsg(uint8 *a)
{
    uint32 address = lx_guest_load_u32(a);
    LxGuestBorrow message = {0};
    if (!lx_execution_borrow(GLoaderState.execution, address, 32, LX_GUEST_READ, &message))
        return 0;
    uint32 result = WinXProcessMsg(message.host);
    lx_execution_release(&message);
    return result;
}
