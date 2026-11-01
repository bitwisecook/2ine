/* Public domain. No rights reserved. Wholly AI-authored. */
static int profile_strings(uint8 *arguments, char **strings, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
    {
        uint32 pointer = lx_guest_load_u32(arguments + 4 + i * 4);
        strings[i] =
            pointer ? lx_execution_copy_string(GLoaderState.execution, pointer, 65536) : NULL;
        if (pointer && !strings[i])
        {
            for (unsigned j = 0; j < i; j++)
                free(strings[j]);
            return 0;
        }
    }
    return 1;
}
static void profile_strings_free(char **strings, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        free(strings[i]);
}
static uint32 bridge32_PrfOpenProfile(uint8 *a)
{
    char *name[1];
    if (!profile_strings(a, name, 1))
        return 0;
    const uint32 result = PrfOpenProfile(lx_guest_load_u32(a), name[0]);
    free(name[0]);
    return result;
}
static uint32 bridge32_PrfCloseProfile(uint8 *a)
{
    return PrfCloseProfile(lx_guest_load_u32(a));
}
static uint32 bridge32_PrfQueryProfileInt(uint8 *a)
{
    char *s[2];
    if (!profile_strings(a, s, 2))
        return lx_guest_load_u32(a + 12);
    const uint32 result = (uint32)PrfQueryProfileInt(lx_guest_load_u32(a), s[0], s[1],
                                                     (LONG)lx_guest_load_u32(a + 12));
    profile_strings_free(s, 2);
    return result;
}
static uint32 bridge32_PrfQueryProfileString(uint8 *a)
{
    char *s[3];
    if (!profile_strings(a, s, 3))
        return 0;
    const uint32 size = lx_guest_load_u32(a + 20);
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (!size || lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 16), size,
                                     LX_GUEST_WRITE, &output))
        result = PrfQueryProfileString(lx_guest_load_u32(a), s[0], s[1], s[2], output.host, size);
    lx_execution_release(&output);
    profile_strings_free(s, 3);
    return result;
}
static uint32 bridge32_PrfWriteProfileString(uint8 *a)
{
    char *s[3];
    if (!profile_strings(a, s, 3))
        return 0;
    uint32 result = PrfWriteProfileString(lx_guest_load_u32(a), s[0], s[1], s[2]);
    profile_strings_free(s, 3);
    return result;
}
static uint32 bridge32_PrfQueryProfileSize(uint8 *a)
{
    char *s[2];
    if (!profile_strings(a, s, 2))
        return 0;
    LxGuestBorrow output = {0};
    uint32 result = 0;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                            &output))
    {
        ULONG size = 0;
        result = PrfQueryProfileSize(lx_guest_load_u32(a), s[0], s[1], &size);
        lx_guest_store_u32(output.host, size);
    }
    lx_execution_release(&output);
    profile_strings_free(s, 2);
    return result;
}
static uint32 bridge32_PrfQueryProfileData(uint8 *a)
{
    char *s[2];
    if (!profile_strings(a, s, 2))
        return 0;
    uint32 result = 0;
    LxGuestBorrow output = {0}, size = {0};
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 16), 4, 3, &size))
    {
        ULONG capacity = lx_guest_load_u32(size.host);
        if (!capacity || lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12),
                                             capacity, LX_GUEST_WRITE, &output))
        {
            result = PrfQueryProfileData(lx_guest_load_u32(a), s[0], s[1], output.host, &capacity);
            lx_guest_store_u32(size.host, capacity);
        }
    }
    lx_execution_release(&output);
    lx_execution_release(&size);
    profile_strings_free(s, 2);
    return result;
}
static uint32 bridge32_PrfWriteProfileData(uint8 *a)
{
    char *s[2];
    if (!profile_strings(a, s, 2))
        return 0;
    const uint32 pointer = lx_guest_load_u32(a + 12), size = lx_guest_load_u32(a + 16);
    LxGuestBorrow data = {0};
    uint32 result = 0;
    uint8 empty = 0;
    if (!pointer || !size ||
        lx_execution_borrow(GLoaderState.execution, pointer, size, LX_GUEST_READ, &data))
        result = PrfWriteProfileData(lx_guest_load_u32(a), s[0], s[1],
                                     pointer ? (size ? data.host : &empty) : NULL, size);
    lx_execution_release(&data);
    profile_strings_free(s, 2);
    return result;
}
