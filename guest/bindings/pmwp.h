/* Public domain. No rights reserved. Wholly AI-authored. */
/* Object setup strings are copied from bounded guest ranges. */
static uint32 bridge32_WinQueryObject(uint8 *a)
{
    char *id = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 4097);
    ULONG result = id ? WinQueryObject(id) : 0;
    free(id);
    return result;
}

static uint32 bridge32_WinCreateObject(uint8 *a)
{
    char *s[4] = {0};
    static const size_t limits[] = {256, 4097, 32768, 4097};
    ULONG result = 0;
    for (unsigned i = 0; i < 4; i++)
    {
        s[i] = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + i * 4),
                                        limits[i]);
        if (!s[i])
            goto done;
    }
    result = WinCreateObject(s[0], s[1], s[2], s[3], lx_guest_load_u32(a + 16));
done:
    for (unsigned i = 0; i < 4; i++)
        free(s[i]);
    return result;
}

static uint32 bridge32_WinSetObjectData(uint8 *a)
{
    char *setup = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), 32768);
    BOOL result = setup && WinSetObjectData(lx_guest_load_u32(a), setup);
    free(setup);
    return result;
}

static uint32 bridge32_WinDestroyObject(uint8 *a)
{
    return WinDestroyObject(lx_guest_load_u32(a));
}

static uint32 bridge32_WinRegisterObjectClass(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 256);
    char *module = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), 260);
    BOOL result = name && module && WinRegisterObjectClass(name, module);
    free(name);
    free(module);
    return result;
}

static uint32 bridge32_WinDeregisterObjectClass(uint8 *a)
{
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 256);
    BOOL result = name && WinDeregisterObjectClass(name);
    free(name);
    return result;
}

static uint32 bridge32_WinIsSomDDReady(uint8 *a)
{
    (void)a;
    return WinIsSomDDReady();
}

static uint32 bridge32_WinRestartSomDD(uint8 *a)
{
    return WinRestartSomDD(lx_guest_load_u32(a));
}

static uint32 bridge32_WinStoreWindowPos(uint8 *a)
{
    char *s0 = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 0), 65536);
    char *s1 = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), 65536);
    uint32 result = 0;
    if (!s0 || !s1)
        goto done;
    result = (uint32)WinStoreWindowPos(s0, s1, (HWND)lx_guest_load_u32(a + 8));
done:
    free(s0);
    free(s1);
    return result;
}

static uint32 bridge32_WinRestoreWindowPos(uint8 *a)
{
    char *s0 = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 0), 65536);
    char *s1 = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), 65536);
    uint32 result = 0;
    if (!s0 || !s1)
        goto done;
    result = (uint32)WinRestoreWindowPos(s0, s1, (HWND)lx_guest_load_u32(a + 8));
done:
    free(s0);
    free(s1);
    return result;
}

static uint32 bridge32_WinLoadFileIcon(uint8 *a)
{
    char *s0 = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 0), 65536);
    uint32 result = 0;
    if (!s0)
        goto done;
    result = (uint32)WinLoadFileIcon(s0, (BOOL)lx_guest_load_u32(a + 4));
done:
    free(s0);
    return result;
}

static uint32 bridge32_WinFreeFileIcon(uint8 *a)
{
    uint32 result = 0;
    result = (uint32)WinFreeFileIcon((HPOINTER)lx_guest_load_u32(a + 0));
    return result;
}

static uint32 bridge32_WinReplaceObjectClass(uint8 *a)
{
    char *s0 = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 0), 65536);
    char *s1 = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 4), 65536);
    uint32 result = 0;
    if (!s0 || !s1)
        goto done;
    result = (uint32)WinReplaceObjectClass(s0, s1, (BOOL)lx_guest_load_u32(a + 8));
done:
    free(s0);
    free(s1);
    return result;
}

static uint32 bridge32_WinQueryActiveDesktopPathname(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_guest_load_u32(a + 4) > 65536 ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 0),
                             lx_guest_load_u32(a + 4), LX_GUEST_WRITE, &output))
        goto done;
    result = (uint32)WinQueryActiveDesktopPathname(output.host, (ULONG)lx_guest_load_u32(a + 4));
done:
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_WinQueryObjectPath(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_guest_load_u32(a + 8) > 65536 ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                             lx_guest_load_u32(a + 8), LX_GUEST_WRITE, &output))
        goto done;
    result = (uint32)WinQueryObjectPath((ULONG)lx_guest_load_u32(a + 0), output.host,
                                        (ULONG)lx_guest_load_u32(a + 8));
done:
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_WinSaveObject(uint8 *a)
{
    uint32 result = 0;
    result = (uint32)WinSaveObject((ULONG)lx_guest_load_u32(a + 0), (BOOL)lx_guest_load_u32(a + 4));
    return result;
}

static uint32 bridge32_WinOpenObject(uint8 *a)
{
    uint32 result = 0;
    result = (uint32)WinOpenObject((ULONG)lx_guest_load_u32(a + 0), (ULONG)lx_guest_load_u32(a + 4),
                                   (BOOL)lx_guest_load_u32(a + 8));
    return result;
}

static uint32 bridge32_WinMoveObject(uint8 *a)
{
    uint32 result = 0;
    result = (uint32)WinMoveObject((ULONG)lx_guest_load_u32(a + 0), (ULONG)lx_guest_load_u32(a + 4),
                                   (ULONG)lx_guest_load_u32(a + 8));
    return result;
}

static uint32 bridge32_WinCopyObject(uint8 *a)
{
    uint32 result = 0;
    result = (uint32)WinCopyObject((ULONG)lx_guest_load_u32(a + 0), (ULONG)lx_guest_load_u32(a + 4),
                                   (ULONG)lx_guest_load_u32(a + 8));
    return result;
}

static uint32 bridge32_WinCreateShadow(uint8 *a)
{
    uint32 result = 0;
    result =
        (uint32)WinCreateShadow((ULONG)lx_guest_load_u32(a + 0), (ULONG)lx_guest_load_u32(a + 4),
                                (ULONG)lx_guest_load_u32(a + 8));
    return result;
}

static uint32 bridge32_WinRestartWPDServer(uint8 *a)
{
    uint32 result = 0;
    result = (uint32)WinRestartWPDServer((BOOL)lx_guest_load_u32(a + 0));
    return result;
}

static uint32 bridge32_WinIsWPDServerReady(uint8 *a)
{
    uint32 result = 0;
    result = (uint32)WinIsWPDServerReady();
    return result;
}

static uint32 bridge32_WinEnumObjectClasses(uint8 *a)
{
    ULONG size;
    uint32 capacity = lx_guest_load_u32(a + 4);
    if (!lx_execution_read(GLoaderState.execution, capacity, &size, 4))
        return 0;
    BOOL result = WinEnumObjectClasses((PVOID)(uintptr_t)lx_guest_load_u32(a), &size);
    return lx_execution_write(GLoaderState.execution, capacity, &size, 4) && result;
}

static uint32 bridge32_WPFilesystemNewClass(uint8 *a)
{
    return WPFilesystemNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WPFolderNewClass(uint8 *a)
{
    return WPFolderNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WPDataFileNewClass(uint8 *a)
{
    return WPDataFileNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WPObjectNewClass(uint8 *a)
{
    return WPObjectNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WPAbstractNewClass(uint8 *a)
{
    return WPAbstractNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WPTransientNewClass(uint8 *a)
{
    return WPTransientNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WPDesktopNewClass(uint8 *a)
{
    return WPDesktopNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WPClassManagerNewClass(uint8 *a)
{
    return WPClassManagerNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_M_WPFolderNewClass(uint8 *a)
{
    return M_WPFolderNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_M_WPDataFileNewClass(uint8 *a)
{
    return M_WPDataFileNewClass(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
