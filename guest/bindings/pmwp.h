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
