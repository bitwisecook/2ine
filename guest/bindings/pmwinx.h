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
