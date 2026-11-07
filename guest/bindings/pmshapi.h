/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_WinAddSwitchEntry(uint8 *a)
{
    SWCNTRL value;
    return lx_execution_read(GLoaderState.execution, lx_guest_load_u32(a), &value, sizeof value)
               ? WinAddSwitchEntry(&value)
               : 0;
}
static uint32 bridge32_WinRemoveSwitchEntry(uint8 *a)
{
    return WinRemoveSwitchEntry(lx_guest_load_u32(a));
}
static uint32 bridge32_WinChangeSwitchEntry(uint8 *a)
{
    SWCNTRL value;
    return lx_execution_read(GLoaderState.execution, lx_guest_load_u32(a + 4), &value, sizeof value)
               ? WinChangeSwitchEntry(lx_guest_load_u32(a), &value)
               : ERROR_INVALID_PARAMETER;
}
static uint32 bridge32_WinQuerySwitchEntry(uint8 *a)
{
    SWCNTRL value;
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), sizeof value,
                             LX_GUEST_WRITE, &output))
        return ERROR_INVALID_PARAMETER;
    uint32 result = WinQuerySwitchEntry(lx_guest_load_u32(a), &value);
    if (!result)
        memcpy(output.host, &value, sizeof value);
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinQuerySwitchHandle(uint8 *a)
{
    return WinQuerySwitchHandle(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
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
static uint32 bridge32_PrfQueryProfile(uint8 *a)
{
    LxGuestBorrow record = {0}, user = {0}, system = {0};
    uint32 result = FALSE;
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 16,
                             LX_GUEST_READ | LX_GUEST_WRITE, &record))
        return FALSE;
    uint8 *wire = record.host;
    ULONG user_capacity = lx_guest_load_u32(wire), system_capacity = lx_guest_load_u32(wire + 8);
    uint32 user_pointer = lx_guest_load_u32(wire + 4),
           system_pointer = lx_guest_load_u32(wire + 12);
    if ((user_capacity && !lx_execution_borrow(GLoaderState.execution, user_pointer, user_capacity,
                                               LX_GUEST_WRITE, &user)) ||
        (system_capacity && !lx_execution_borrow(GLoaderState.execution, system_pointer,
                                                 system_capacity, LX_GUEST_WRITE, &system)))
        goto done;
    PRFPROFILE profile = {user_capacity, user.host, system_capacity, system.host};
    result = PrfQueryProfile(lx_guest_load_u32(a), &profile);
    lx_guest_store_u32(wire, profile.cchUserName);
    lx_guest_store_u32(wire + 8, profile.cchSysName);
done:
    lx_execution_release(&system);
    lx_execution_release(&user);
    lx_execution_release(&record);
    return result;
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

static uint32 bridge32_WinQueryTaskSizePos(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if (lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 36, LX_GUEST_WRITE,
                            &output))
    {
        SWP position;
        result = WinQueryTaskSizePos(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), &position);
        if (!result)
            memcpy(output.host, &position, sizeof position);
    }
    lx_execution_release(&output);
    return result;
}
static uint32 bridge32_WinCreateSwitchEntry(uint8 *a)
{
    SWCNTRL value;
    return lx_execution_read(GLoaderState.execution, lx_guest_load_u32(a + 4), &value, sizeof value)
        ? WinCreateSwitchEntry(lx_guest_load_u32(a), &value) : 0;
}
static uint32 bridge32_WinQueryTaskTitle(uint8 *a)
{
    LxGuestBorrow title = {0};
    ULONG capacity = lx_guest_load_u32(a + 8), result = ERROR_INVALID_PARAMETER;
    if (capacity && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), capacity,
                                         LX_GUEST_WRITE, &title))
        result = WinQueryTaskTitle(lx_guest_load_u32(a), title.host, capacity);
    lx_execution_release(&title); return result;
}
/* PROGDETAILS guest offsets: 4 progc, 8 fbVisible, 12-32 pointers, 36 SWP, 72 total. */
static uint32 bridge32_WinStartApp(uint8 *a)
{
    uint8 wire[72];
    uint32 address = lx_guest_load_u32(a + 4), parameters = lx_guest_load_u32(a + 8);
    char *strings[4] = {NULL}, *inputs = NULL;
    LxGuestArgument environment = {0};
    uint32 result = 0;
    if (!address || !lx_execution_read(GLoaderState.execution, address, wire, sizeof wire))
        goto done;
    for (unsigned i = 0; i < 4; i++)
    {
        uint32 pointer = lx_guest_load_u32(wire + 12 + i * 4);
        if (pointer &&
            !(strings[i] = lx_execution_copy_string(GLoaderState.execution, pointer, 65536)))
            goto done;
    }
    if (parameters &&
        !(inputs = lx_execution_copy_string(GLoaderState.execution, parameters, 65536)))
        goto done;
    if (!lx_guest_argument_strings(GLoaderState.execution, lx_guest_load_u32(wire + 32), 0, 65536,
                                   1, &environment))
        goto done;
    PROGDETAILS details = {.Length = lx_guest_load_u32(wire),
                           .progc = lx_guest_load_u32(wire + 4),
                           .fbVisible = lx_guest_load_u32(wire + 8),
                           .pszTitle = strings[0],
                           .pszExecutable = strings[1],
                           .pszParameters = strings[2],
                           .pszStartupDir = strings[3],
                           .pszEnvironment = environment.host};
    details.swpInitial.fl = lx_guest_load_u32(wire + 36);
    details.swpInitial.cy = (LONG)lx_guest_load_u32(wire + 40);
    details.swpInitial.cx = (LONG)lx_guest_load_u32(wire + 44);
    details.swpInitial.y = (LONG)lx_guest_load_u32(wire + 48);
    details.swpInitial.x = (LONG)lx_guest_load_u32(wire + 52);
    result = WinStartApp(lx_guest_load_u32(a), &details, inputs,
                         (PVOID)(uintptr_t)lx_guest_load_u32(a + 12), lx_guest_load_u32(a + 16));
done:
    lx_guest_argument_finish(&environment, 0);
    free(inputs);
    for (unsigned i = 0; i < 4; i++)
        free(strings[i]);
    return result;
}
