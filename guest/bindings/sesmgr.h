/* Public domain. No rights reserved. Wholly AI-authored. */
/* STARTDATA contains guest flat pointers even on a host with 64-bit pointers. */
static uint32 bridge32_DosSetSession(uint8 *a)
{
    LxGuestBorrow input = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 6, LX_GUEST_READ,
                             &input))
        return ERROR_INVALID_PARAMETER;
    STATUSDATA status = {lx_guest_load_u16(input.host), lx_guest_load_u16(input.host + 2),
                         lx_guest_load_u16(input.host + 4)};
    uint32 result = DosSetSession(lx_guest_load_u32(a), &status);
    lx_execution_release(&input);
    return result;
}
static uint32 bridge32_DosSelectSession(uint8 *a)
{
    return DosSelectSession(lx_guest_load_u32(a));
}
static uint32 bridge32_DosStartSession(uint8 *a)
{
    LxGuestBorrow input = {0}, sid = {0}, pid = {0};
    uint32 pointer = lx_guest_load_u32(a), result = ERROR_INVALID_PARAMETER;
    char *strings[5] = {0};
    if (!lx_execution_borrow(GLoaderState.execution, pointer, 2, LX_GUEST_READ, &input))
        return result;
    unsigned length = lx_guest_load_u16(input.host);
    lx_execution_release(&input);
    if (length < 24 || length > 60 ||
        !lx_execution_borrow(GLoaderState.execution, pointer, length, LX_GUEST_READ, &input) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &sid) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 8), 4, LX_GUEST_WRITE,
                             &pid))
        goto done;
    const uint8 *p = input.host;
    STARTDATA data = {0};
    data.Length = length;
    data.Related = lx_guest_load_u16(p + 2);
    data.FgBg = lx_guest_load_u16(p + 4);
    data.TraceOpt = lx_guest_load_u16(p + 6);
    for (unsigned i = 0; i < 5; i++)
    {
        unsigned at = i == 4 ? 32 : 8 + i * 4;
        uint32 address = at + 4 <= length ? lx_guest_load_u32(p + at) : 0;
        if (address)
        {
            strings[i] =
                lx_execution_copy_string(GLoaderState.execution, address, i == 2 ? 4096 : 1024);
            if (!strings[i])
                goto done;
        }
    }
    data.PgmTitle = strings[0];
    data.PgmName = strings[1];
    data.PgmInputs = (PBYTE)strings[2];
    data.TermQ = (PBYTE)strings[3];
    data.IconFile = strings[4];
    /* Explicit guest environment blocks use the existing process bridge contract. */
    if (length >= 28 && lx_guest_load_u32(p + 24))
        goto done;
    if (length >= 32)
    {
        data.InheritOpt = lx_guest_load_u16(p + 28);
        data.SessionType = lx_guest_load_u16(p + 30);
    }
    ULONG session;
    PID process;
    result = DosStartSession(&data, &session, &process);
    if (!result)
    {
        lx_guest_store_u32(sid.host, session);
        lx_guest_store_u32(pid.host, process);
    }
done:
    for (unsigned i = 0; i < 5; i++)
        free(strings[i]);
    lx_execution_release(&input);
    lx_execution_release(&sid);
    lx_execution_release(&pid);
    return result;
}
