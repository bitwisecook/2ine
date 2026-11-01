/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_GetProfileString(uint8 *a)
{
    uint32 key_pointer = lx_guest_load_u32(a + 4), fallback_pointer = lx_guest_load_u32(a + 8);
    char *section = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 32768);
    char *key =
        key_pointer ? lx_execution_copy_string(GLoaderState.execution, key_pointer, 32768) : NULL;
    char *fallback = fallback_pointer
                         ? lx_execution_copy_string(GLoaderState.execution, fallback_pointer, 32768)
                         : NULL;
    uint32 capacity = lx_guest_load_u32(a + 16), result = 0;
    LxGuestBorrow output = {0};
    if (!section || (key_pointer && !key) || (fallback_pointer && !fallback) || !capacity ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), capacity,
                             LX_GUEST_WRITE, &output))
        open32SetError(87);
    else
        result = GetProfileString(section, key, fallback, output.host, capacity);
    lx_execution_release(&output);
    free(section);
    free(key);
    free(fallback);
    return result;
}
static uint32 bridge32_FindResource(uint8 *a)
{
    return FindResource(lx_guest_load_u32(a), (PSZ)(uintptr_t)lx_guest_load_u32(a + 4),
                        (PSZ)(uintptr_t)lx_guest_load_u32(a + 8));
}
static uint32 bridge32_LoadResource(uint8 *a)
{
    return LoadResource(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
static uint32 bridge32_SizeofResource(uint8 *a)
{
    return SizeofResource(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
static uint32 bridge32_LockResource(uint8 *a)
{
    return LockResource(lx_guest_load_u32(a));
}
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
static uint32 bridge32_RegOpenKey(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = o32_string(pointer);
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!pointer || name) && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8),
                                                  4, LX_GUEST_WRITE, &output))
    {
        ULONG handle = 0;
        result = (uint32)RegOpenKey(lx_guest_load_u32(a), name, &handle);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
}
static uint32 bridge32_RegCreateKey(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = o32_string(pointer);
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((!pointer || name) && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8),
                                                  4, LX_GUEST_WRITE, &output))
    {
        ULONG handle = 0;
        result = (uint32)RegCreateKey(lx_guest_load_u32(a), name, &handle);
        if (!result)
            lx_guest_store_u32(output.host, handle);
    }
    lx_execution_release(&output);
    free(name);
    return result;
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
static uint32 bridge32_RegDeleteValue(uint8 *a)
{
    const uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = o32_string(pointer);
    uint32 result = pointer && !name ? ERROR_INVALID_PARAMETER
                                     : (uint32)RegDeleteValue(lx_guest_load_u32(a), name);
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

static uint32 bridge32_RegEnumValue(uint8 *a)
{
    uint32 np = lx_guest_load_u32(a + 8), sp = lx_guest_load_u32(a + 12),
           tp = lx_guest_load_u32(a + 20), dp = lx_guest_load_u32(a + 24),
           cp = lx_guest_load_u32(a + 28);
    LxGuestBorrow name = {0}, length = {0}, type = {0}, data = {0}, size = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if (!np || lx_guest_load_u32(a + 16) ||
        !lx_execution_borrow(GLoaderState.execution, sp, 4, 3, &length) ||
        (tp && !lx_execution_borrow(GLoaderState.execution, tp, 4, LX_GUEST_WRITE, &type)) ||
        (cp && !lx_execution_borrow(GLoaderState.execution, cp, 4, 3, &size)))
        goto done;
    ULONG characters = lx_guest_load_u32(length.host),
          count = cp ? lx_guest_load_u32(size.host) : 0, kind = 0;
    if (characters > 32767)
    {
        result = ERROR_MORE_DATA;
        goto done;
    }
    if ((characters &&
         !lx_execution_borrow(GLoaderState.execution, np, characters, LX_GUEST_WRITE, &name)) ||
        (dp && (!cp || (count && !lx_execution_borrow(GLoaderState.execution, dp, count,
                                                      LX_GUEST_WRITE, &data)))))
        goto done;
    char empty = 0;
    result =
        (uint32)RegEnumValue(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                             characters ? name.host : &empty, &characters, NULL, tp ? &kind : NULL,
                             dp ? (count ? data.host : &empty) : NULL, cp ? &count : NULL);
    if (!result || result == ERROR_MORE_DATA)
    {
        lx_guest_store_u32(length.host, characters);
        if (cp)
            lx_guest_store_u32(size.host, count);
        if (tp)
            lx_guest_store_u32(type.host, kind);
    }
done:
    lx_execution_release(&size);
    lx_execution_release(&data);
    lx_execution_release(&type);
    lx_execution_release(&length);
    lx_execution_release(&name);
    return result;
}

static uint32 bridge32_WinCallWinMain(uint8 *a)
{
    return (uint32)WinCallWinMain(
        (LONG)lx_guest_load_u32(a), (PSZ *)(uintptr_t)lx_guest_load_u32(a + 4),
        (PFNWINMAIN)(uintptr_t)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12));
}

static uint32 bridge32_GetTickCount(uint8 *a)
{
    (void)a;
    return GetTickCount();
}

static uint32 bridge32_GetLastError(uint8 *a)
{
    (void)a;
    return GetLastError();
}
static uint32 bridge32_SetLastError(uint8 *a)
{
    SetLastError(lx_guest_load_u32(a));
    return 0;
}
static uint32 bridge32_Sleep(uint8 *a)
{
    Sleep(lx_guest_load_u32(a));
    return 0;
}
static uint32 bridge32_GetModuleHandle(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a);
    if (!pointer)
        return GetModuleHandle(NULL);
    char *name = lx_execution_copy_string(GLoaderState.execution, pointer, 32768);
    if (!name)
    {
        open32SetError(87);
        return 0;
    }
    HMODULE result = GetModuleHandle(name);
    free(name);
    return result;
}

static uint32 bridge32_GetModuleFileName(uint8 *a)
{
    ULONG capacity = lx_guest_load_u32(a + 8);
    LxGuestBorrow span = {0};
    if (!capacity || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                          capacity, LX_GUEST_WRITE, &span))
    {
        SetLastError(87);
        return 0;
    }
    ULONG result = GetModuleFileName(lx_guest_load_u32(a), span.host, capacity);
    lx_execution_release(&span);
    return result;
}
static uint32 bridge32_LoadLibrary(uint8 *a)
{
    char *name = o32_string(lx_guest_load_u32(a));
    if (!name)
    {
        SetLastError(87);
        return 0;
    }
    uint32 result = LoadLibrary(name);
    free(name);
    return result;
}
static uint32 bridge32_FreeLibrary(uint8 *a)
{
    return FreeLibrary(lx_guest_load_u32(a));
}
static uint32 bridge32_GetProcAddress(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = pointer <= UINT16_MAX ? (char *)(uintptr_t)pointer : o32_string(pointer);
    if (!name)
    {
        SetLastError(87);
        return 0;
    }
    PFN function = GetProcAddress(lx_guest_load_u32(a), name);
    uint32 result = 0;
    if (function && !lx_execution_entry_address(GLoaderState.execution, (void *)function, &result))
    {
        open32SetError(127);
        result = 0;
    }
    if (pointer > UINT16_MAX)
        free(name);
    return result;
}

static uint32 bridge32_GetUserDefaultLCID(uint8 *a)
{
    (void)a;
    return GetUserDefaultLCID();
}

static uint32 bridge32_GetProfileInt(uint8 *a)
{
    char *section = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 32768);
    char *key = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), 32768);
    ULONG fallback = lx_guest_load_u32(a + 8);
    if (!section || !key)
        open32SetError(87);
    ULONG result = section && key ? GetProfileInt(section, key, (LONG)fallback) : fallback;
    free(section);
    free(key);
    return result;
}

static uint32 open32_atom_string(uint8 *a, int add)
{
    uint32 pointer = lx_guest_load_u32(a);
    if (pointer <= 0xFFFF)
        return add ? GlobalAddAtom((PSZ)(uintptr_t)pointer)
                   : GlobalFindAtom((PSZ)(uintptr_t)pointer);
    char *name = lx_execution_copy_string(GLoaderState.execution, pointer, 256);
    if (!name)
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = add ? GlobalAddAtom(name) : GlobalFindAtom(name);
    free(name);
    return result;
}
static uint32 bridge32_GlobalAddAtom(uint8 *a)
{
    return open32_atom_string(a, 1);
}
static uint32 bridge32_GlobalFindAtom(uint8 *a)
{
    return open32_atom_string(a, 0);
}
static uint32 bridge32_GlobalDeleteAtom(uint8 *a)
{
    return GlobalDeleteAtom(lx_guest_load_u32(a));
}
static uint32 bridge32_GlobalGetAtomName(uint8 *a)
{
    LONG capacity = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow output = {0};
    if (capacity <= 0 || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                              (ULONG)capacity, LX_GUEST_WRITE, &output))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = GlobalGetAtomName(lx_guest_load_u32(a), output.host, capacity);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_RegisterClass(uint8 *a)
{
    LxGuestBorrow record = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 40, LX_GUEST_READ,
                             &record))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = RegisterClass(record.host);
    lx_execution_release(&record);
    return result;
}
static uint32 bridge32_UnregisterClass(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a);
    char *name = pointer <= UINT16_MAX ? (char *)(uintptr_t)pointer : o32_string(pointer);
    if (pointer > UINT16_MAX && !name)
        return 0;
    ULONG result = UnregisterClass(name, lx_guest_load_u32(a + 4));
    if (pointer > UINT16_MAX)
        free(name);
    return result;
}
static uint32 bridge32_GetClassInfo(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = pointer <= UINT16_MAX ? (char *)(uintptr_t)pointer : o32_string(pointer);
    LxGuestBorrow output = {0};
    if ((pointer > UINT16_MAX && !name) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 40, LX_GUEST_WRITE,
                             &output))
    {
        if (pointer > UINT16_MAX)
            free(name);
        open32SetError(87);
        return 0;
    }
    ULONG result = GetClassInfo(lx_guest_load_u32(a), name, output.host);
    lx_execution_release(&output);
    if (pointer > UINT16_MAX)
        free(name);
    return result;
}

static uint32 bridge32_CreateWindowEx(uint8 *a)
{
    uint32 class_pointer = lx_guest_load_u32(a + 4), title_pointer = lx_guest_load_u32(a + 8);
    char *name =
        class_pointer <= UINT16_MAX ? (char *)(uintptr_t)class_pointer : o32_string(class_pointer);
    char *title = title_pointer ? o32_string(title_pointer) : NULL;
    if ((class_pointer > UINT16_MAX && !name) || (title_pointer && !title))
    {
        if (class_pointer > UINT16_MAX)
            free(name);
        free(title);
        open32SetError(87);
        return 0;
    }
    ULONG result = CreateWindowEx(lx_guest_load_u32(a), name, title, lx_guest_load_u32(a + 12),
                                  (LONG)lx_guest_load_u32(a + 16), (LONG)lx_guest_load_u32(a + 20),
                                  (LONG)lx_guest_load_u32(a + 24), (LONG)lx_guest_load_u32(a + 28),
                                  lx_guest_load_u32(a + 32), lx_guest_load_u32(a + 36),
                                  lx_guest_load_u32(a + 40), lx_guest_load_u32(a + 44));
    if (class_pointer > UINT16_MAX)
        free(name);
    free(title);
    return result;
}
static uint32 bridge32_DefWindowProc(uint8 *a)
{
    return (uint32)DefWindowProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                 lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}
static uint32 bridge32_DestroyWindow(uint8 *a)
{
    return DestroyWindow(lx_guest_load_u32(a));
}

static uint32 bridge32_GetWindowLong(uint8 *a)
{
    return (uint32)GetWindowLong(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}
static uint32 bridge32_SetWindowLong(uint8 *a)
{
    return (uint32)SetWindowLong(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                 (LONG)lx_guest_load_u32(a + 8));
}

static uint32 bridge32_GetOEMCP(uint8 *a)
{
    (void)a;
    return GetOEMCP();
}

static uint32 bridge32_LoadBitmap(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = pointer <= UINT16_MAX ? (char *)(uintptr_t)pointer : o32_string(pointer);
    if (pointer > UINT16_MAX && !name)
        return 0;
    ULONG result = LoadBitmap(lx_guest_load_u32(a), name);
    if (pointer > UINT16_MAX)
        free(name);
    return result;
}
static uint32 bridge32_GetObject(uint8 *a)
{
    ULONG handle = lx_guest_load_u32(a), pointer = lx_guest_load_u32(a + 8);
    LONG capacity = (LONG)lx_guest_load_u32(a + 4);
    if (!pointer)
        return (uint32)GetObject(handle, capacity, NULL);
    LONG needed = GetObject(handle, 0, NULL);
    if (!needed)
        return 0;
    LxGuestBorrow output = {0};
    if (capacity < needed || !lx_execution_borrow(GLoaderState.execution, pointer, (ULONG)needed,
                                                  LX_GUEST_WRITE, &output))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = (ULONG)GetObject(handle, capacity, output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DeleteObject(uint8 *a)
{
    return DeleteObject(lx_guest_load_u32(a));
}

static uint32 bridge32_GetSystemMetrics(uint8 *a)
{
    return (uint32)GetSystemMetrics((LONG)lx_guest_load_u32(a));
}
static uint32 bridge32_SetWindowPos(uint8 *a)
{
    return SetWindowPos(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                        (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                        (LONG)lx_guest_load_u32(a + 16), (LONG)lx_guest_load_u32(a + 20),
                        lx_guest_load_u32(a + 24));
}

static uint32 bridge32_MoveWindow(uint8 *a)
{
    return MoveWindow(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                      (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                      (LONG)lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20));
}
static uint32 bridge32_ShowWindow(uint8 *a)
{
    return ShowWindow(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}
static uint32 bridge32_UpdateWindow(uint8 *a)
{
    return UpdateWindow(lx_guest_load_u32(a));
}

static uint32 bridge32_GetDC(uint8 *a)
{
    return GetDC(lx_guest_load_u32(a));
}
static uint32 bridge32_ReleaseDC(uint8 *a)
{
    return (uint32)ReleaseDC(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
static uint32 bridge32_CreateCompatibleDC(uint8 *a)
{
    return CreateCompatibleDC(lx_guest_load_u32(a));
}
static uint32 bridge32_DeleteDC(uint8 *a)
{
    return DeleteDC(lx_guest_load_u32(a));
}
static uint32 bridge32_SelectObject(uint8 *a)
{
    return SelectObject(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
static uint32 bridge32_BitBlt(uint8 *a)
{
    return BitBlt(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                  (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                  (LONG)lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20),
                  (LONG)lx_guest_load_u32(a + 24), (LONG)lx_guest_load_u32(a + 28),
                  lx_guest_load_u32(a + 32));
}
static uint32 bridge32_BeginPaint(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 64, LX_GUEST_WRITE,
                             &output))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = BeginPaint(lx_guest_load_u32(a), output.host);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_EndPaint(uint8 *a)
{
    LxGuestBorrow input = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 64, LX_GUEST_READ,
                             &input))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = EndPaint(lx_guest_load_u32(a), input.host);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_SetBkMode(uint8 *a)
{
    return (uint32)SetBkMode(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}
static uint32 bridge32_SetTextColor(uint8 *a)
{
    return SetTextColor(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
static uint32 bridge32_SetBkColor(uint8 *a)
{
    return SetBkColor(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_CreateFontIndirect(uint8 *a)
{
    LxGuestBorrow input = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 60, LX_GUEST_READ,
                             &input))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = CreateFontIndirect(input.host);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_GetStockObject(uint8 *a)
{
    return GetStockObject((LONG)lx_guest_load_u32(a));
}

static uint32 bridge32_DrawText(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 8);
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *text = NULL;
    if (count == -1)
        text = lx_execution_copy_string(GLoaderState.execution, pointer, 65537);
    else if (count >= 0 && count <= 65536)
    {
        text = malloc((size_t)count + 1);
        if (text && !lx_execution_read(GLoaderState.execution, pointer, text, (ULONG)count))
        {
            free(text);
            text = NULL;
        }
        if (text)
            text[count] = 0;
    }
    LxGuestBorrow rectangle = {0};
    if (!text ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), 16, 3, &rectangle))
    {
        free(text);
        open32SetError(87);
        return 0;
    }
    ULONG result = (ULONG)DrawText(lx_guest_load_u32(a), text, count, rectangle.host,
                                   lx_guest_load_u32(a + 16));
    lx_execution_release(&rectangle);
    free(text);
    return result;
}
static uint32 bridge32_TextOut(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 16);
    if (count < 0 || count > 65536)
    {
        open32SetError(87);
        return 0;
    }
    char *text = malloc((size_t)count + 1);
    if (!text ||
        !lx_execution_read(GLoaderState.execution, lx_guest_load_u32(a + 12), text, (ULONG)count))
    {
        free(text);
        open32SetError(87);
        return 0;
    }
    text[count] = 0;
    ULONG result = TextOut(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                           (LONG)lx_guest_load_u32(a + 8), text, count);
    free(text);
    return result;
}

static uint32 bridge32_CreateCompatibleBitmap(uint8 *a)
{
    return CreateCompatibleBitmap(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                  (LONG)lx_guest_load_u32(a + 8));
}
static uint32 bridge32_GetPixel(uint8 *a)
{
    return GetPixel(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                    (LONG)lx_guest_load_u32(a + 8));
}

static uint32 bridge32_OpenFile(uint8 *a)
{
    ULONG flags = lx_guest_load_u32(a + 8);
    char *name = flags & 0x8000 ? NULL : o32_string(lx_guest_load_u32(a));
    LxGuestBorrow record = {0};
    if ((!name && !(flags & 0x8000)) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 136, 3, &record))
    {
        free(name);
        open32SetError(87);
        return UINT32_MAX;
    }
    ULONG result = (ULONG)OpenFile(name, record.host, flags);
    lx_execution_release(&record);
    free(name);
    return result;
}
static uint32 bridge32__lclose(uint8 *a)
{
    return (uint32)_lclose(lx_guest_load_u32(a));
}
static uint32 bridge32__llseek(uint8 *a)
{
    return (uint32)_llseek(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                           (LONG)lx_guest_load_u32(a + 8));
}
static uint32 open32_legacy_io(uint8 *a, int writing)
{
    ULONG size = lx_guest_load_u32(a + 8);
    LxGuestBorrow data = {0};
    if (size && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), size,
                                     writing ? LX_GUEST_READ : LX_GUEST_WRITE, &data))
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    ULONG result = writing ? _lwrite(lx_guest_load_u32(a), data.host, size)
                           : _lread(lx_guest_load_u32(a), data.host, size);
    lx_execution_release(&data);
    return result;
}
static uint32 bridge32__lread(uint8 *a)
{
    return open32_legacy_io(a, 0);
}
static uint32 bridge32__lwrite(uint8 *a)
{
    return open32_legacy_io(a, 1);
}

static uint32 open32_file_io(uint8 *a, int writing)
{
    ULONG size = lx_guest_load_u32(a + 8), pointer = lx_guest_load_u32(a + 12);
    LxGuestBorrow data = {0}, actual = {0};
    ULONG result = 0;
    if (lx_guest_load_u32(a + 16) ||
        !lx_execution_borrow(GLoaderState.execution, pointer, 4, LX_GUEST_WRITE, &actual))
    {
        open32SetError(87);
        return FALSE;
    }
    lx_guest_store_u32(actual.host, 0);
    if (size && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), size,
                                     writing ? LX_GUEST_READ : LX_GUEST_WRITE, &data))
        open32SetError(87);
    else
    {
        ULONG count = 0;
        result = writing ? WriteFile(lx_guest_load_u32(a), data.host, size, &count, NULL)
                         : ReadFile(lx_guest_load_u32(a), data.host, size, &count, NULL);
        lx_guest_store_u32(actual.host, count);
    }
    lx_execution_release(&data);
    lx_execution_release(&actual);
    return result;
}
static uint32 bridge32_ReadFile(uint8 *a)
{
    return open32_file_io(a, 0);
}
static uint32 bridge32_WriteFile(uint8 *a)
{
    return open32_file_io(a, 1);
}
static uint32 bridge32_CloseHandle(uint8 *a)
{
    return CloseHandle(lx_guest_load_u32(a));
}

static uint32 open32_format(uint8 *a, uint32 arguments)
{
    char *format = o32_string(lx_guest_load_u32(a + 4));
    if (!format)
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    char output[1024];
    LONG count = open32FormatGuest(format, arguments, output);
    free(format);
    if (count >= 0 && !lx_execution_write(GLoaderState.execution, lx_guest_load_u32(a), output,
                                          (size_t)count + 1))
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    return (uint32)count;
}
static uint32 bridge32_wsprintf(uint8 *a)
{
    uint32 address = lx_execution_api_arguments(GLoaderState.execution);
    if (!address || address > UINT32_MAX - 8)
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    return open32_format(a, address + 8);
}
static uint32 bridge32_wvsprintf(uint8 *a)
{
    return open32_format(a, lx_guest_load_u32(a + 8));
}

static uint32 bridge32_LoadCursor(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = pointer <= UINT16_MAX ? (char *)(uintptr_t)pointer : o32_string(pointer);
    if (pointer > UINT16_MAX && !name)
        return 0;
    ULONG result = LoadCursor(lx_guest_load_u32(a), name);
    if (pointer > UINT16_MAX)
        free(name);
    return result;
}
static uint32 bridge32_SetCursor(uint8 *a)
{
    return SetCursor(lx_guest_load_u32(a));
}

static uint32 bridge32_GetActiveWindow(uint8 *a)
{
    (void)a;
    return GetActiveWindow();
}
static uint32 bridge32_SetActiveWindow(uint8 *a)
{
    return SetActiveWindow(lx_guest_load_u32(a));
}
static uint32 bridge32_GetFocus(uint8 *a)
{
    (void)a;
    return GetFocus();
}
static uint32 bridge32_SetFocus(uint8 *a)
{
    return SetFocus(lx_guest_load_u32(a));
}

static uint32 bridge32_GetDeviceCaps(uint8 *a)
{
    return (uint32)GetDeviceCaps(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetTextMetrics(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 56, LX_GUEST_WRITE,
                             &output))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = GetTextMetrics(lx_guest_load_u32(a), output.host);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_EnumFontFamilies(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *family = pointer ? o32_string(pointer) : NULL;
    if (pointer && !family)
        return 0;
    ULONG result = (ULONG)EnumFontFamilies(lx_guest_load_u32(a), family, lx_guest_load_u32(a + 8),
                                           lx_guest_load_u32(a + 12));
    free(family);
    return result;
}

static uint32 bridge32_LoadIcon(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *name = pointer <= UINT16_MAX ? (char *)(uintptr_t)pointer : o32_string(pointer);
    if (pointer > UINT16_MAX && !name)
        return 0;
    ULONG result = LoadIcon(lx_guest_load_u32(a), name);
    if (pointer > UINT16_MAX)
        free(name);
    return result;
}
static uint32 bridge32_DestroyIcon(uint8 *a)
{
    return DestroyIcon(lx_guest_load_u32(a));
}

static uint32 bridge32_InitializeCriticalSection(uint8 *a)
{
    InitializeCriticalSection((PVOID)(uintptr_t)lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_EnterCriticalSection(uint8 *a)
{
    EnterCriticalSection((PVOID)(uintptr_t)lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_LeaveCriticalSection(uint8 *a)
{
    LeaveCriticalSection((PVOID)(uintptr_t)lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_DeleteCriticalSection(uint8 *a)
{
    DeleteCriticalSection((PVOID)(uintptr_t)lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_GetCurrentThreadId(uint8 *a)
{
    (void)a;
    return GetCurrentThreadId();
}

static uint32 bridge32_CreateThread(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 20);
    LxGuestBorrow output = {0};
    if (pointer &&
        !lx_execution_borrow(GLoaderState.execution, pointer, 4, LX_GUEST_WRITE, &output))
    {
        open32SetError(87);
        return 0;
    }
    ULONG id = 0;
    ULONG result = CreateThread((PVOID)(uintptr_t)lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12),
                                lx_guest_load_u32(a + 16), pointer ? &id : NULL);
    if (result && pointer)
        lx_guest_store_u32(output.host, id);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_GetExitCodeThread(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output))
    {
        open32SetError(87);
        return 0;
    }
    ULONG code = 0;
    ULONG result = GetExitCodeThread(lx_guest_load_u32(a), &code);
    if (result)
        lx_guest_store_u32(output.host, code);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_ResumeThread(uint8 *a)
{
    return ResumeThread(lx_guest_load_u32(a));
}
static uint32 bridge32_WaitForSingleObject(uint8 *a)
{
    return WaitForSingleObject(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
static uint32 bridge32_ExitThread(uint8 *a)
{
    ExitThread(lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_SetThreadPriority(uint8 *a)
{
    return SetThreadPriority(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetSysColor(uint8 *a)
{
    return GetSysColor((LONG)lx_guest_load_u32(a));
}

static uint32 bridge32_CreateSolidBrush(uint8 *a)
{
    return CreateSolidBrush(lx_guest_load_u32(a));
}
static uint32 bridge32_FillRect(uint8 *a)
{
    LxGuestBorrow rect = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                             &rect))
    {
        open32SetError(87);
        return 0;
    }
    ULONG result = (ULONG)FillRect(lx_guest_load_u32(a), rect.host, lx_guest_load_u32(a + 8));
    lx_execution_release(&rect);
    return result;
}

static uint32 bridge32_GetSystemTime(uint8 *arguments)
{
    LxGuestBorrow output = {0};
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(arguments), 16,
                            LX_GUEST_WRITE, &output))
        GetSystemTime(output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return 0;
}

static uint32 bridge32_SystemTimeToFileTime(uint8 *a)
{
    LxGuestBorrow input = {0}, output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_READ,
                            &input) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 8, LX_GUEST_WRITE,
                            &output))
        result = SystemTimeToFileTime(input.host, output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_GetLocaleInfo(uint8 *a)
{
    LONG capacity = (LONG)lx_guest_load_u32(a + 12);
    if (capacity < 0)
    {
        open32SetError(87);
        return 0;
    }
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (!capacity || lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8),
                                         (ULONG)capacity, LX_GUEST_WRITE, &output))
        result = (uint32)GetLocaleInfo(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), output.host,
                                       capacity);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_IsDBCSLeadByte(uint8 *a)
{
    return IsDBCSLeadByte(lx_guest_load_u32(a));
}

static uint32 bridge32_GetCurrentDirectory(uint8 *a)
{
    ULONG capacity = lx_guest_load_u32(a);
    ULONG required = GetCurrentDirectory(0, NULL);
    if (!required || capacity < required)
        return required;
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), capacity,
                            LX_GUEST_WRITE, &output))
        result = GetCurrentDirectory(capacity, output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_SetCurrentDirectory(uint8 *a)
{
    char *name = o32_string(lx_guest_load_u32(a));
    if (!name)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = SetCurrentDirectory(name);
    free(name);
    return result;
}

static uint32 bridge32_GetUserDefaultLangID(uint8 *a)
{
    (void)a;
    return GetUserDefaultLangID();
}

static uint32 bridge32_GetLocalTime(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_WRITE,
                            &output))
        GetLocalTime(output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return 0;
}

static uint32 bridge32_CreatePalette(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a);
    LxGuestBorrow input = {0};
    uint8 header[4];
    uint32 result = 0;
    if (lx_execution_read(GLoaderState.execution, pointer, header, sizeof header) &&
        lx_execution_borrow(GLoaderState.execution, pointer, 4 + 4u * lx_guest_load_u16(header + 2),
                            LX_GUEST_READ, &input))
        result = CreatePalette(input.host);
    else
        open32SetError(87);
    lx_execution_release(&input);
    return result;
}
static uint32 bridge32_SelectPalette(uint8 *a)
{
    return SelectPalette(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}
static uint32 bridge32_RealizePalette(uint8 *a)
{
    return RealizePalette(lx_guest_load_u32(a));
}
static uint32 bridge32_GetNearestPaletteIndex(uint8 *a)
{
    return GetNearestPaletteIndex(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
static uint32 bridge32_GetPaletteEntries(uint8 *a)
{
    uint32 handle = lx_guest_load_u32(a), start = lx_guest_load_u32(a + 4),
           count = lx_guest_load_u32(a + 8), pointer = lx_guest_load_u32(a + 12);
    if (!pointer)
        return GetPaletteEntries(handle, start, count, NULL);
    uint32 available = GetPaletteEntries(handle, 0, 0, NULL);
    if (!available || start >= available)
        return 0;
    if (count > available - start)
        count = available - start;
    if (!count)
        return 0;
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, pointer, count * 4, LX_GUEST_WRITE, &output))
        result = GetPaletteEntries(handle, start, count, output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_RegisterClipboardFormat(uint8 *a)
{
    char *name = o32_string(lx_guest_load_u32(a));
    if (!name)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = RegisterClipboardFormat(name);
    free(name);
    return result;
}

static uint32 bridge32_CreateMenu(uint8 *a)
{
    (void)a;
    return CreateMenu();
}
static uint32 bridge32_CreatePopupMenu(uint8 *a)
{
    (void)a;
    return CreatePopupMenu();
}
static uint32 bridge32_DestroyMenu(uint8 *a)
{
    return DestroyMenu(lx_guest_load_u32(a));
}

static uint32 open32_menu_insert(uint8 *a, BOOL append)
{
    uint32 flags = lx_guest_load_u32(a + (append ? 4 : 8)),
           pointer = lx_guest_load_u32(a + (append ? 12 : 16));
    if (flags & (4 | 0x100))
    {
        open32SetError(50);
        return 0;
    }
    char *text = pointer ? o32_string(pointer) : NULL;
    if (pointer && !text)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = InsertMenu(lx_guest_load_u32(a), append ? UINT32_MAX : lx_guest_load_u32(a + 4),
                               flags, lx_guest_load_u32(a + (append ? 8 : 12)), text);
    free(text);
    return result;
}
static uint32 bridge32_InsertMenu(uint8 *a)
{
    return open32_menu_insert(a, FALSE);
}
static uint32 bridge32_AppendMenu(uint8 *a)
{
    return open32_menu_insert(a, TRUE);
}
static uint32 bridge32_GetMenuItemCount(uint8 *a)
{
    return (uint32)GetMenuItemCount(lx_guest_load_u32(a));
}
static uint32 bridge32_GetMenuItemID(uint8 *a)
{
    return GetMenuItemID(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}
static uint32 bridge32_GetSubMenu(uint8 *a)
{
    return GetSubMenu(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetMenuState(uint8 *a)
{
    return GetMenuState(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_CheckMenuItem(uint8 *a)
{
    return CheckMenuItem(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_EnableMenuItem(uint8 *a)
{
    return EnableMenuItem(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_IsMenu(uint8 *a)
{
    return IsMenu(lx_guest_load_u32(a));
}

static uint32 bridge32_RemoveMenu(uint8 *a)
{
    return RemoveMenu(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_DeleteMenu(uint8 *a)
{
    return DeleteMenu(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_MessageBeep(uint8 *a)
{
    return MessageBeep(lx_guest_load_u32(a));
}

static uint32 bridge32_GetCapture(uint8 *a)
{
    (void)a;
    return GetCapture();
}

static uint32 bridge32_SetCapture(uint8 *a)
{
    return SetCapture(lx_guest_load_u32(a));
}

static uint32 bridge32_ReleaseCapture(uint8 *a)
{
    (void)a;
    return ReleaseCapture();
}

static uint32 bridge32_GetClientRect(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_WRITE,
                            &output))
        result = GetClientRect(lx_guest_load_u32(a), output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GetWindowRect(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_WRITE,
                            &output))
        result = GetWindowRect(lx_guest_load_u32(a), output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_ClientToScreen(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 8,
                            LX_GUEST_READ | LX_GUEST_WRITE, &output))
        result = ClientToScreen(lx_guest_load_u32(a), output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_ScreenToClient(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 8,
                            LX_GUEST_READ | LX_GUEST_WRITE, &output))
        result = ScreenToClient(lx_guest_load_u32(a), output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GetDesktopWindow(uint8 *a)
{
    (void)a;
    return GetDesktopWindow();
}

static uint32 bridge32_SendMessage(uint8 *a)
{
    return (uint32)SendMessage(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                               lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_CallWindowProc(uint8 *a)
{
    return (uint32)CallWindowProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                  lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12),
                                  lx_guest_load_u32(a + 16));
}

static uint32 bridge32_SetRect(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_WRITE,
                            &output))
        result =
            SetRect(output.host, (LONG)lx_guest_load_u32(a + 4), (LONG)lx_guest_load_u32(a + 8),
                    (LONG)lx_guest_load_u32(a + 12), (LONG)lx_guest_load_u32(a + 16));
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_SetProp(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *copy = pointer > UINT16_MAX ? o32_string(pointer) : NULL;
    if (pointer > UINT16_MAX && !copy)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = SetProp(lx_guest_load_u32(a), copy ? copy : (PSZ)(uintptr_t)pointer,
                            lx_guest_load_u32(a + 8));
    free(copy);
    return result;
}

static uint32 bridge32_GetProp(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *copy = pointer > UINT16_MAX ? o32_string(pointer) : NULL;
    if (pointer > UINT16_MAX && !copy)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = GetProp(lx_guest_load_u32(a), copy ? copy : (PSZ)(uintptr_t)pointer);
    free(copy);
    return result;
}

static uint32 bridge32_RemoveProp(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *copy = pointer > UINT16_MAX ? o32_string(pointer) : NULL;
    if (pointer > UINT16_MAX && !copy)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = RemoveProp(lx_guest_load_u32(a), copy ? copy : (PSZ)(uintptr_t)pointer);
    free(copy);
    return result;
}

static uint32 bridge32_CreateIC(uint8 *a)
{
    char *strings[3] = {0};
    uint32 pointers[3];
    uint32 result = 0;
    for (unsigned i = 0; i < 3; i++)
    {
        pointers[i] = lx_guest_load_u32(a + i * 4);
        if (pointers[i] && !(strings[i] = o32_string(pointers[i])))
        {
            open32SetError(87);
            goto done;
        }
    }
    result =
        CreateIC(strings[0], strings[1], strings[2], (PVOID)(uintptr_t)lx_guest_load_u32(a + 12));
done:
    for (unsigned i = 0; i < 3; i++)
        free(strings[i]);
    return result;
}

static uint32 bridge32_CreateRectRgn(uint8 *a)
{
    return (uint32)CreateRectRgn((LONG)lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                 (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12));
}

static uint32 bridge32_SetRectRgn(uint8 *a)
{
    return (uint32)SetRectRgn(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                              (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                              (LONG)lx_guest_load_u32(a + 16));
}

static uint32 bridge32_CombineRgn(uint8 *a)
{
    return (uint32)CombineRgn(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                              lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12));
}

static uint32 bridge32_SelectClipRgn(uint8 *a)
{
    return (uint32)SelectClipRgn(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetRgnBox(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_WRITE,
                            &output))
        result = (uint32)GetRgnBox(lx_guest_load_u32(a), output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_CreateRectRgnIndirect(uint8 *a)
{
    LxGuestBorrow input = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_READ,
                            &input))
        result = CreateRectRgnIndirect(input.host);
    else
        open32SetError(87);
    lx_execution_release(&input);
    return result;
}

static uint32 bridge32_SetClipboardViewer(uint8 *a)
{
    return SetClipboardViewer(lx_guest_load_u32(a));
}

static uint32 bridge32_GetClipboardViewer(uint8 *a)
{
    (void)a;
    return GetClipboardViewer();
}

static uint32 bridge32_ChangeClipboardChain(uint8 *a)
{
    return ChangeClipboardChain(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_RegisterWindowMessage(uint8 *a)
{
    char *name = o32_string(lx_guest_load_u32(a));
    if (!name)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = RegisterWindowMessage(name);
    free(name);
    return result;
}

static uint32 bridge32_DefFrameProc(uint8 *a)
{
    return (uint32)DefFrameProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12),
                                lx_guest_load_u32(a + 16));
}

static uint32 bridge32_DefMDIChildProc(uint8 *a)
{
    return (uint32)DefMDIChildProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                   lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_SetWindowsHookEx(uint8 *a)
{
    return SetWindowsHookEx((LONG)lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                            lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_UnhookWindowsHookEx(uint8 *a)
{
    return UnhookWindowsHookEx(lx_guest_load_u32(a));
}

static uint32 bridge32_CallNextHookEx(uint8 *a)
{
    return (uint32)CallNextHookEx(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                  lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_GetKeyState(uint8 *a)
{
    return (uint32)GetKeyState((LONG)lx_guest_load_u32(a));
}

static uint32 bridge32_IsIconic(uint8 *a)
{
    return IsIconic(lx_guest_load_u32(a));
}

static uint32 bridge32_GetParent(uint8 *a)
{
    return GetParent(lx_guest_load_u32(a));
}

static uint32 bridge32_SetParent(uint8 *a)
{
    return SetParent(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_RedrawWindow(uint8 *a)
{
    LxGuestBorrow rect = {0};
    uint32 pointer = lx_guest_load_u32(a + 4), result = 0;
    if (!pointer || lx_execution_borrow(GLoaderState.execution, pointer, 16, LX_GUEST_READ, &rect))
        result = RedrawWindow(lx_guest_load_u32(a), rect.host, lx_guest_load_u32(a + 8),
                              lx_guest_load_u32(a + 12));
    else
        open32SetError(87);
    lx_execution_release(&rect);
    return result;
}

static uint32 bridge32_GetMenuString(uint8 *a)
{
    LONG size = (LONG)lx_guest_load_u32(a + 12);
    uint32 pointer = lx_guest_load_u32(a + 8), result = 0;
    LxGuestBorrow output = {0};
    if (size < 0 || (pointer && size &&
                     !lx_execution_borrow(GLoaderState.execution, pointer, (uint32)size,
                                          LX_GUEST_WRITE, &output)))
        open32SetError(87);
    else
        result = (uint32)GetMenuString(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), output.host,
                                       size, lx_guest_load_u32(a + 16));
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_DrawMenuBar(uint8 *a)
{
    return DrawMenuBar(lx_guest_load_u32(a));
}

static uint32 bridge32_FindFirstFile(uint8 *a)
{
    char *name = o32_string(lx_guest_load_u32(a));
    LxGuestBorrow output = {0};
    uint32 result = UINT32_MAX;
    if (name && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 320,
                                    LX_GUEST_WRITE, &output))
        result = FindFirstFile(name, output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    free(name);
    return result;
}

static uint32 bridge32_FindNextFile(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 320, LX_GUEST_WRITE,
                            &output))
        result = FindNextFile(lx_guest_load_u32(a), output.host);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_FindClose(uint8 *a)
{
    return FindClose(lx_guest_load_u32(a));
}

static uint32 bridge32_CreateFont(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 52);
    char *face = o32_string(pointer);
    if (pointer && !face)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = CreateFont(
        (LONG)lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4), (LONG)lx_guest_load_u32(a + 8),
        (LONG)lx_guest_load_u32(a + 12), (LONG)lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20),
        lx_guest_load_u32(a + 24), lx_guest_load_u32(a + 28), lx_guest_load_u32(a + 32),
        lx_guest_load_u32(a + 36), lx_guest_load_u32(a + 40), lx_guest_load_u32(a + 44),
        lx_guest_load_u32(a + 48), face);
    free(face);
    return result;
}

static uint32 bridge32_SetWindowText(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4);
    char *text = o32_string(pointer);
    if (pointer && !text)
    {
        open32SetError(87);
        return 0;
    }
    uint32 result = SetWindowText(lx_guest_load_u32(a), text);
    free(text);
    return result;
}

static uint32 bridge32_GetWindowText(uint8 *a)
{
    LONG capacity = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (capacity <= 0 || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                              (uint32)capacity, LX_GUEST_WRITE, &output))
        open32SetError(87);
    else
        result = (uint32)GetWindowText(lx_guest_load_u32(a), output.host, capacity);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GetWindowTextLength(uint8 *a)
{
    return (uint32)GetWindowTextLength(lx_guest_load_u32(a));
}

static uint32 bridge32_LocalAlloc(uint8 *a)
{
    return LocalAlloc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_LocalFree(uint8 *a)
{
    return LocalFree(lx_guest_load_u32(a));
}

static uint32 bridge32_LocalLock(uint8 *a)
{
    return LocalLock(lx_guest_load_u32(a));
}

static uint32 bridge32_LocalUnlock(uint8 *a)
{
    return LocalUnlock(lx_guest_load_u32(a));
}

static uint32 bridge32_LocalFlags(uint8 *a)
{
    return LocalFlags(lx_guest_load_u32(a));
}

static uint32 bridge32_LocalSize(uint8 *a)
{
    return LocalSize(lx_guest_load_u32(a));
}

static uint32 bridge32_LocalHandle(uint8 *a)
{
    return LocalHandle(lx_guest_load_u32(a));
}

static uint32 bridge32_LocalReAlloc(uint8 *a)
{
    return LocalReAlloc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_GlobalAlloc(uint8 *a)
{
    return GlobalAlloc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GlobalFree(uint8 *a)
{
    return GlobalFree(lx_guest_load_u32(a));
}

static uint32 bridge32_GlobalLock(uint8 *a)
{
    return GlobalLock(lx_guest_load_u32(a));
}

static uint32 bridge32_GlobalUnlock(uint8 *a)
{
    return GlobalUnlock(lx_guest_load_u32(a));
}

static uint32 bridge32_GlobalFlags(uint8 *a)
{
    return GlobalFlags(lx_guest_load_u32(a));
}

static uint32 bridge32_GlobalSize(uint8 *a)
{
    return GlobalSize(lx_guest_load_u32(a));
}

static uint32 bridge32_GlobalHandle(uint8 *a)
{
    return GlobalHandle(lx_guest_load_u32(a));
}

static uint32 bridge32_GlobalReAlloc(uint8 *a)
{
    return GlobalReAlloc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_GetDIBits(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 20), pixels = lx_guest_load_u32(a + 16);
    uint32 start = lx_guest_load_u32(a + 8), count = lx_guest_load_u32(a + 12),
           usage = lx_guest_load_u32(a + 24);
    LxGuestBorrow header = {0}, info = {0}, bits = {0};
    size_t info_bytes, pixel_bytes;
    uint32 result = 0;
    if (!lx_execution_borrow(GLoaderState.execution, pointer, 40, LX_GUEST_READ, &header))
        open32SetError(87);
    else if (open32DibBuffers(header.host, start, count, usage, &info_bytes, &pixel_bytes))
    {
        if (!lx_execution_borrow(GLoaderState.execution, pointer, info_bytes,
                                 LX_GUEST_READ | LX_GUEST_WRITE, &info) ||
            (pixels && pixel_bytes &&
             !lx_execution_borrow(GLoaderState.execution, pixels, pixel_bytes, LX_GUEST_WRITE,
                                  &bits)))
            open32SetError(87);
        else
            result = (uint32)GetDIBits(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), start, count,
                                       bits.host, info.host, usage);
    }
    lx_execution_release(&bits);
    lx_execution_release(&info);
    lx_execution_release(&header);
    return result;
}

static uint32 bridge32_LoadString(uint8 *a)
{
    LONG capacity = (LONG)lx_guest_load_u32(a + 12);
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (capacity <= 0 || !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8),
                                              (uint32)capacity, LX_GUEST_WRITE, &output))
        open32SetError(87);
    else
        result = (uint32)LoadString(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), output.host,
                                    capacity);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_lstrcpy(uint8 *a)
{
    return (uint32)lstrcpy(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_lstrcat(uint8 *a)
{
    return (uint32)lstrcat(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_lstrlen(uint8 *a)
{
    return (uint32)lstrlen(lx_guest_load_u32(a));
}

static uint32 bridge32_SetMapMode(uint8 *a)
{
    return (uint32)SetMapMode(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetMapMode(uint8 *a)
{
    return (uint32)GetMapMode(lx_guest_load_u32(a));
}

static uint32 bridge32_IntersectClipRect(uint8 *a)
{
    return (uint32)IntersectClipRect(
        lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4), (LONG)lx_guest_load_u32(a + 8),
        (LONG)lx_guest_load_u32(a + 12), (LONG)lx_guest_load_u32(a + 16));
}

static uint32 bridge32_ExcludeClipRect(uint8 *a)
{
    return (uint32)ExcludeClipRect(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                   (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                                   (LONG)lx_guest_load_u32(a + 16));
}

static uint32 bridge32_GetTextExtentPoint32(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow text = {0}, output = {0};
    uint32 result = 0;
    if (count < 0 || count > 65536 ||
        (count && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                       (uint32)count, LX_GUEST_READ, &text)) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), 8, LX_GUEST_WRITE,
                             &output))
        open32SetError(87);
    else
        result = GetTextExtentPoint32(lx_guest_load_u32(a), text.host, count, output.host);
    lx_execution_release(&output);
    lx_execution_release(&text);
    return result;
}

static uint32 bridge32_EnableWindow(uint8 *a)
{
    return EnableWindow(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_IsWindowEnabled(uint8 *a)
{
    return IsWindowEnabled(lx_guest_load_u32(a));
}

static uint32 bridge32_IsWindowVisible(uint8 *a)
{
    return IsWindowVisible(lx_guest_load_u32(a));
}

static uint32 bridge32_IsWindow(uint8 *a)
{
    return IsWindow(lx_guest_load_u32(a));
}

static uint32 bridge32_InvalidateRect(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4), result = 0;
    LxGuestBorrow rect = {0};
    if (!pointer || lx_execution_borrow(GLoaderState.execution, pointer, 16, LX_GUEST_READ, &rect))
        result = InvalidateRect(lx_guest_load_u32(a), rect.host, lx_guest_load_u32(a + 8));
    else
        open32SetError(87);
    lx_execution_release(&rect);
    return result;
}

static uint32 bridge32_ValidateRect(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4), result = 0;
    LxGuestBorrow rect = {0};
    if (!pointer || lx_execution_borrow(GLoaderState.execution, pointer, 16, LX_GUEST_READ, &rect))
        result = ValidateRect(lx_guest_load_u32(a), rect.host);
    else
        open32SetError(87);
    lx_execution_release(&rect);
    return result;
}

static uint32 bridge32_InvalidateRgn(uint8 *a)
{
    return InvalidateRgn(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_ValidateRgn(uint8 *a)
{
    return ValidateRgn(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetUpdateRect(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4), result = 0;
    LxGuestBorrow rect = {0};
    if (!pointer || lx_execution_borrow(GLoaderState.execution, pointer, 16, LX_GUEST_WRITE, &rect))
        result = GetUpdateRect(lx_guest_load_u32(a), rect.host, lx_guest_load_u32(a + 8));
    else
        open32SetError(87);
    lx_execution_release(&rect);
    return result;
}

static uint32 bridge32_SetScrollRange(uint8 *a)
{
    return (uint32)SetScrollRange(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                  (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                                  lx_guest_load_u32(a + 16));
}

static uint32 bridge32_SetScrollPos(uint8 *a)
{
    return (uint32)SetScrollPos(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                (LONG)lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_GetScrollPos(uint8 *a)
{
    return (uint32)GetScrollPos(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_ShowScrollBar(uint8 *a)
{
    return (uint32)ShowScrollBar(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                 lx_guest_load_u32(a + 8));
}

static uint32 bridge32_GetScrollRange(uint8 *a)
{
    LxGuestBorrow low = {0}, high = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE,
                            &low) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                            &high))
        result = GetScrollRange(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4), low.host,
                                high.host);
    else
        open32SetError(87);
    lx_execution_release(&high);
    lx_execution_release(&low);
    return result;
}

static uint32 bridge32_GetClassLong(uint8 *a)
{
    return GetClassLong(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetClassName(uint8 *a)
{
    LONG capacity = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (capacity > 0 && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                            (uint32)capacity, LX_GUEST_WRITE, &output))
        result = (uint32)GetClassName(lx_guest_load_u32(a), output.host, capacity);
    else
        open32SetError(87);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GetWindow(uint8 *a)
{
    return GetWindow(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_CreateCaret(uint8 *a)
{
    return CreateCaret(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                       (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12));
}

static uint32 bridge32_DestroyCaret(uint8 *a)
{
    (void)a;
    return DestroyCaret();
}

static uint32 bridge32_HideCaret(uint8 *a)
{
    return HideCaret(lx_guest_load_u32(a));
}

static uint32 bridge32_ShowCaret(uint8 *a)
{
    return ShowCaret(lx_guest_load_u32(a));
}

static uint32 bridge32_SetCaretPos(uint8 *a)
{
    return SetCaretPos((LONG)lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetCaretBlinkTime(uint8 *a)
{
    (void)a;
    return GetCaretBlinkTime();
}

static uint32 bridge32_SetCaretBlinkTime(uint8 *a)
{
    return SetCaretBlinkTime(lx_guest_load_u32(a));
}

static uint32 bridge32_GetCaretPos(uint8 *a)
{
    LxGuestBorrow point = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 8, LX_GUEST_WRITE,
                            &point))
        result = GetCaretPos(point.host);
    else
        open32SetError(87);
    lx_execution_release(&point);
    return result;
}

static uint32 bridge32_BringWindowToTop(uint8 *a)
{
    return BringWindowToTop(lx_guest_load_u32(a));
}

static uint32 bridge32_CreatePen(uint8 *a)
{
    return (uint32)CreatePen((LONG)lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                             lx_guest_load_u32(a + 8));
}

static uint32 bridge32_LineTo(uint8 *a)
{
    return (uint32)LineTo(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                          (LONG)lx_guest_load_u32(a + 8));
}

static uint32 bridge32_SetROP2(uint8 *a)
{
    return (uint32)SetROP2(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_CreatePenIndirect(uint8 *a)
{
    LxGuestBorrow record = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_READ,
                            &record))
        result = CreatePenIndirect(record.host);
    else
        open32SetError(87);
    lx_execution_release(&record);
    return result;
}

static uint32 bridge32_MoveToEx(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 12), result = 0;
    LxGuestBorrow previous = {0};
    if (!pointer ||
        lx_execution_borrow(GLoaderState.execution, pointer, 8, LX_GUEST_WRITE, &previous))
        result = MoveToEx(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                          (LONG)lx_guest_load_u32(a + 8), previous.host);
    else
        open32SetError(87);
    lx_execution_release(&previous);
    return result;
}

static uint32 bridge32_GetROP2(uint8 *a)
{
    return (uint32)GetROP2(lx_guest_load_u32(a));
}

static uint32 bridge32_Rectangle(uint8 *a)
{
    return Rectangle(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                     (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                     (LONG)lx_guest_load_u32(a + 16));
}

static uint32 bridge32_PeekMessage(uint8 *a)
{
    LxGuestBorrow message = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 28, LX_GUEST_WRITE,
                            &message))
        result =
            (uint32)PeekMessage(message.host, lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8),
                                lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16));
    else
        open32SetError(87);
    lx_execution_release(&message);
    return result;
}

static uint32 bridge32_GetMessage(uint8 *a)
{
    LxGuestBorrow message = {0};
    uint32 result = UINT32_MAX;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 28, LX_GUEST_WRITE,
                            &message))
        result = (uint32)GetMessage(message.host, lx_guest_load_u32(a + 4),
                                    lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
    else
        open32SetError(87);
    lx_execution_release(&message);
    return result;
}

static uint32 bridge32_TranslateMessage(uint8 *a)
{
    LxGuestBorrow message = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 28, LX_GUEST_READ,
                            &message))
        result = (uint32)TranslateMessage(message.host);
    else
        open32SetError(87);
    lx_execution_release(&message);
    return result;
}

static uint32 bridge32_DispatchMessage(uint8 *a)
{
    LxGuestBorrow message = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 28, LX_GUEST_READ,
                            &message))
        result = (uint32)DispatchMessage(message.host);
    else
        open32SetError(87);
    lx_execution_release(&message);
    return result;
}

static uint32 bridge32_PostMessage(uint8 *a)
{
    return (uint32)PostMessage(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                               lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_PostQuitMessage(uint8 *a)
{
    PostQuitMessage((LONG)lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_GetMessageTime(uint8 *a)
{
    (void)a;
    return (uint32)GetMessageTime();
}

static uint32 bridge32_GetMessagePos(uint8 *a)
{
    (void)a;
    return (uint32)GetMessagePos();
}

static uint32 bridge32_GetSystemMenu(uint8 *a)
{
    return GetSystemMenu(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetWindowThreadProcessId(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 4), result = 0;
    LxGuestBorrow process = {0};
    if (!pointer ||
        lx_execution_borrow(GLoaderState.execution, pointer, 4, LX_GUEST_WRITE, &process))
        result = GetWindowThreadProcessId(lx_guest_load_u32(a), process.host);
    else
        open32SetError(87);
    lx_execution_release(&process);
    return result;
}

static uint32 bridge32_GetCurrentProcessId(uint8 *a)
{
    (void)a;
    return GetCurrentProcessId();
}

static uint32 bridge32_GetAsyncKeyState(uint8 *a)
{
    return (uint32)GetAsyncKeyState((LONG)lx_guest_load_u32(a));
}

static uint32 bridge32_SetRectEmpty(uint8 *a)
{
    LxGuestBorrow p0 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_WRITE, &p0))
        result = SetRectEmpty(p0.host);
    else
        open32SetError(87);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_CopyRect(uint8 *a)
{
    LxGuestBorrow p0 = {0}, p1 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_WRITE,
                            &p0) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                            &p1))
        result = CopyRect(p0.host, p1.host);
    else
        open32SetError(87);
    lx_execution_release(&p1);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_IsRectEmpty(uint8 *a)
{
    LxGuestBorrow p0 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_READ, &p0))
        result = IsRectEmpty(p0.host);
    else
        open32SetError(87);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_EqualRect(uint8 *a)
{
    LxGuestBorrow p0 = {0}, p1 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_READ, &p0) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                            &p1))
        result = EqualRect(p0.host, p1.host);
    else
        open32SetError(87);
    lx_execution_release(&p1);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_UnionRect(uint8 *a)
{
    LxGuestBorrow p0 = {0}, p1 = {0}, p2 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_WRITE,
                            &p0) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                            &p1) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 16, LX_GUEST_READ,
                            &p2))
        result = UnionRect(p0.host, p1.host, p2.host);
    else
        open32SetError(87);
    lx_execution_release(&p2);
    lx_execution_release(&p1);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_IntersectRect(uint8 *a)
{
    LxGuestBorrow p0 = {0}, p1 = {0}, p2 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_WRITE,
                            &p0) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                            &p1) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 16, LX_GUEST_READ,
                            &p2))
        result = IntersectRect(p0.host, p1.host, p2.host);
    else
        open32SetError(87);
    lx_execution_release(&p2);
    lx_execution_release(&p1);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_SubtractRect(uint8 *a)
{
    LxGuestBorrow p0 = {0}, p1 = {0}, p2 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_WRITE,
                            &p0) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                            &p1) &&
        lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 16, LX_GUEST_READ,
                            &p2))
        result = SubtractRect(p0.host, p1.host, p2.host);
    else
        open32SetError(87);
    lx_execution_release(&p2);
    lx_execution_release(&p1);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_OffsetRect(uint8 *a)
{
    LxGuestBorrow p0 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16,
                            LX_GUEST_READ | LX_GUEST_WRITE, &p0))
        result =
            OffsetRect(p0.host, (LONG)lx_guest_load_u32(a + 4), (LONG)lx_guest_load_u32(a + 8));
    else
        open32SetError(87);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_InflateRect(uint8 *a)
{
    LxGuestBorrow p0 = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16,
                            LX_GUEST_READ | LX_GUEST_WRITE, &p0))
        result =
            InflateRect(p0.host, (LONG)lx_guest_load_u32(a + 4), (LONG)lx_guest_load_u32(a + 8));
    else
        open32SetError(87);
    lx_execution_release(&p0);
    return result;
}

static uint32 bridge32_PtInRect(uint8 *a)
{
    LxGuestBorrow rectangle = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 16, LX_GUEST_READ,
                            &rectangle))
        result = PtInRect(rectangle.host, (LONG)lx_guest_load_u32(a + 4),
                          (LONG)lx_guest_load_u32(a + 8));
    else
        open32SetError(87);
    lx_execution_release(&rectangle);
    return result;
}

static uint32 bridge32_GetClipBox(uint8 *a)
{
    LxGuestBorrow rectangle = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_WRITE,
                            &rectangle))
        result = (uint32)GetClipBox(lx_guest_load_u32(a), rectangle.host);
    else
        open32SetError(87);
    lx_execution_release(&rectangle);
    return result;
}

static uint32 bridge32_FrameRect(uint8 *a)
{
    LxGuestBorrow rectangle = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                            &rectangle))
        result = (uint32)FrameRect(lx_guest_load_u32(a), rectangle.host, lx_guest_load_u32(a + 8));
    else
        open32SetError(87);
    lx_execution_release(&rectangle);
    return result;
}

static uint32 bridge32_InvertRect(uint8 *a)
{
    LxGuestBorrow rectangle = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16, LX_GUEST_READ,
                            &rectangle))
        result = InvertRect(lx_guest_load_u32(a), rectangle.host);
    else
        open32SetError(87);
    lx_execution_release(&rectangle);
    return result;
}

static uint32 bridge32_CreateBitmap(uint8 *a)
{
    LONG width = (LONG)lx_guest_load_u32(a), height = (LONG)lx_guest_load_u32(a + 4);
    uint32 planes = lx_guest_load_u32(a + 8), depth = lx_guest_load_u32(a + 12),
           pointer = lx_guest_load_u32(a + 16);
    size_t bytes;
    LxGuestBorrow bits = {0};
    uint32 result = 0;
    if (open32BitmapBytes(width, height, planes, depth, &bytes))
    {
        if (!pointer ||
            lx_execution_borrow(GLoaderState.execution, pointer, bytes, LX_GUEST_READ, &bits))
            result = CreateBitmap(width, height, planes, depth, bits.host);
        else
            open32SetError(87);
    }
    lx_execution_release(&bits);
    return result;
}

static uint32 bridge32_SetDIBits(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 20), pixels = lx_guest_load_u32(a + 16);
    uint32 start = lx_guest_load_u32(a + 8), count = lx_guest_load_u32(a + 12),
           usage = lx_guest_load_u32(a + 24);
    LxGuestBorrow header = {0}, info = {0}, bits = {0};
    size_t info_bytes, pixel_bytes;
    uint32 result = 0;
    if (!lx_execution_borrow(GLoaderState.execution, pointer, 40, LX_GUEST_READ, &header))
        open32SetError(87);
    else if (open32DibBuffers(header.host, start, count, usage, &info_bytes, &pixel_bytes))
    {
        if (!lx_execution_borrow(GLoaderState.execution, pointer, info_bytes, LX_GUEST_READ,
                                 &info) ||
            (pixel_bytes && (!pixels || !lx_execution_borrow(GLoaderState.execution, pixels,
                                                             pixel_bytes, LX_GUEST_READ, &bits))))
            open32SetError(87);
        else
            result = (uint32)SetDIBits(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), start, count,
                                       bits.host, info.host, usage);
    }
    lx_execution_release(&bits);
    lx_execution_release(&info);
    lx_execution_release(&header);
    return result;
}

static uint32 bridge32_GetTextExtentPoint(uint8 *a)
{
    return bridge32_GetTextExtentPoint32(a);
}

static uint32 bridge32_DestroyCursor(uint8 *a)
{
    return DestroyCursor(lx_guest_load_u32(a));
}

static uint32 bridge32_ExtTextOut(uint8 *a)
{
    ULONG flags = lx_guest_load_u32(a + 12), count = lx_guest_load_u32(a + 24);
    LxGuestBorrow rectangle = {0}, text = {0}, advances = {0};
    uint32 result = 0, rect = lx_guest_load_u32(a + 16), dx = lx_guest_load_u32(a + 28);
    if (count > 65536 ||
        ((flags & 6) && (!rect || !lx_execution_borrow(GLoaderState.execution, rect, 16,
                                                       LX_GUEST_READ, &rectangle))) ||
        (count && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 20), count,
                                       LX_GUEST_READ, &text)) ||
        (count && dx &&
         !lx_execution_borrow(GLoaderState.execution, dx, count * 4, LX_GUEST_READ, &advances)))
        open32SetError(87);
    else
        result = ExtTextOut(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                            (LONG)lx_guest_load_u32(a + 8), flags, rectangle.host, text.host, count,
                            advances.host);
    lx_execution_release(&advances);
    lx_execution_release(&text);
    lx_execution_release(&rectangle);
    return result;
}

static uint32 bridge32_WaitMessage(uint8 *a)
{
    (void)a;
    return WaitMessage();
}

static uint32 bridge32_GetCurrentProcess(uint8 *a)
{
    (void)a;
    return GetCurrentProcess();
}

static uint32 bridge32_GetTextFace(uint8 *a)
{
    LONG capacity = (LONG)lx_guest_load_u32(a + 4);
    uint32 pointer = lx_guest_load_u32(a + 8);
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (capacity < 0 || capacity > 65536 ||
        (pointer && capacity &&
         !lx_execution_borrow(GLoaderState.execution, pointer, capacity, LX_GUEST_WRITE, &output)))
        open32SetError(87);
    else
        result = GetTextFace(lx_guest_load_u32(a), capacity, pointer ? output.host : NULL);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_EnumChildWindows(uint8 *a)
{
    return EnumChildWindows(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                            lx_guest_load_u32(a + 8));
}

static uint32 bridge32_WinTranslateDevicePoints(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 12);
    LxGuestBorrow points = {0};
    uint32 result = 0;
    if (count < 0 || count > 65536 ||
        (count && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8),
                                       (uint32)count * 8, LX_GUEST_READ | LX_GUEST_WRITE, &points)))
        open32SetError(87);
    else
        result = WinTranslateDevicePoints(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                          points.host, count, (LONG)lx_guest_load_u32(a + 16));
    lx_execution_release(&points);
    return result;
}

static uint32 bridge32_DialogBox(uint8 *a)
{
    return (uint32)DialogBox(lx_guest_load_u32(a), (PSZ)(uintptr_t)lx_guest_load_u32(a + 4),
                             lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12));
}

static uint32 bridge32_DialogBoxParam(uint8 *a)
{
    return (uint32)DialogBoxParam(lx_guest_load_u32(a), (PSZ)(uintptr_t)lx_guest_load_u32(a + 4),
                                  lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12),
                                  lx_guest_load_u32(a + 16));
}

static uint32 bridge32_EndDialog(uint8 *a)
{
    return EndDialog(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetDlgItem(uint8 *a)
{
    return GetDlgItem(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4));
}

static uint32 bridge32_SetDlgItemText(uint8 *a)
{
    char *text = o32_string(lx_guest_load_u32(a + 8));
    if (!text)
        return 0;
    uint32 result = SetDlgItemText(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4), text);
    free(text);
    return result;
}

static uint32 bridge32_GetDlgItemText(uint8 *a)
{
    LONG capacity = (LONG)lx_guest_load_u32(a + 12);
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (capacity <= 0 || capacity > 65536 ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), capacity,
                             LX_GUEST_WRITE, &output))
        open32SetError(87);
    else
        result = GetDlgItemText(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4), output.host,
                                capacity);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_SendDlgItemMessage(uint8 *a)
{
    return (uint32)SendDlgItemMessage(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                                      lx_guest_load_u32(a + 8), lx_guest_load_u32(a + 12),
                                      lx_guest_load_u32(a + 16));
}

static uint32 bridge32_PatBlt(uint8 *a)
{
    return PatBlt(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                  (LONG)lx_guest_load_u32(a + 8), (LONG)lx_guest_load_u32(a + 12),
                  (LONG)lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20));
}

static uint32 bridge32_CreatePatternBrush(uint8 *a)
{
    return CreatePatternBrush(lx_guest_load_u32(a));
}

static uint32 bridge32_DPtoLP(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow points = {0};
    uint32 result = 0;
    if (count < 0 || count > 65536 ||
        (count && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                       (ULONG)count * 8, LX_GUEST_READ | LX_GUEST_WRITE, &points)))
        open32SetError(87);
    else
        result = DPtoLP(lx_guest_load_u32(a), points.host, count);
    lx_execution_release(&points);
    return result;
}

static uint32 bridge32_LPtoDP(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow points = {0};
    uint32 result = 0;
    if (count < 0 || count > 65536 ||
        (count && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                       (ULONG)count * 8, LX_GUEST_READ | LX_GUEST_WRITE, &points)))
        open32SetError(87);
    else
        result = LPtoDP(lx_guest_load_u32(a), points.host, count);
    lx_execution_release(&points);
    return result;
}

static uint32 bridge32_GetKeyboardState(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a), 256, LX_GUEST_WRITE,
                             &output))
        open32SetError(87);
    else
        result = GetKeyboardState(output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_Polygon(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow points = {0};
    uint32 result = 0;
    if (count < 2 || count > 65536 ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), (ULONG)count * 8,
                             LX_GUEST_READ, &points))
        open32SetError(87);
    else
        result = Polygon(lx_guest_load_u32(a), points.host, count);
    lx_execution_release(&points);
    return result;
}

static uint32 bridge32_Polyline(uint8 *a)
{
    LONG count = (LONG)lx_guest_load_u32(a + 8);
    LxGuestBorrow points = {0};
    uint32 result = 0;
    if (count < 2 || count > 65536 ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), (ULONG)count * 8,
                             LX_GUEST_READ, &points))
        open32SetError(87);
    else
        result = Polyline(lx_guest_load_u32(a), points.host, count);
    lx_execution_release(&points);
    return result;
}

static uint32 bridge32_SetTextAlign(uint8 *a)
{
    return SetTextAlign(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetTextAlign(uint8 *a)
{
    return GetTextAlign(lx_guest_load_u32(a));
}

static uint32 bridge32_SetBrushOrgEx(uint8 *a)
{
    uint32 pointer = lx_guest_load_u32(a + 12);
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (pointer &&
        !lx_execution_borrow(GLoaderState.execution, pointer, 8, LX_GUEST_WRITE, &output))
        open32SetError(87);
    else
        result = SetBrushOrgEx(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                               (LONG)lx_guest_load_u32(a + 8), output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_GetBrushOrgEx(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 8, LX_GUEST_WRITE,
                             &output))
        open32SetError(87);
    else
        result = GetBrushOrgEx(lx_guest_load_u32(a), output.host);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_SetForegroundWindow(uint8 *a)
{
    return SetForegroundWindow(lx_guest_load_u32(a));
}

static uint32 bridge32_IsBadReadPtr(uint8 *a)
{
    return IsBadReadPtr((PVOID)(uintptr_t)lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_IsBadWritePtr(uint8 *a)
{
    return IsBadWritePtr((PVOID)(uintptr_t)lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_IsBadCodePtr(uint8 *a)
{
    return IsBadCodePtr(lx_guest_load_u32(a));
}

static uint32 bridge32_CharUpper(uint8 *a)
{
    return CharUpper(lx_guest_load_u32(a));
}

static uint32 bridge32_CharLower(uint8 *a)
{
    return CharLower(lx_guest_load_u32(a));
}

static uint32 bridge32_CharUpperBuff(uint8 *a)
{
    return CharUpperBuff(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_CharLowerBuff(uint8 *a)
{
    return CharLowerBuff(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_CharNext(uint8 *a)
{
    return CharNext(lx_guest_load_u32(a));
}

static uint32 bridge32_CharPrev(uint8 *a)
{
    return CharPrev(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_lstrcmpi(uint8 *a)
{
    return lstrcmpi(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_GetFileAttributes(uint8 *a)
{
    char *name = o32_string(lx_guest_load_u32(a));
    if (!name)
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    uint32 result = GetFileAttributes(name);
    free(name);
    return result;
}
