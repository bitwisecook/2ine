/* MSG flat 32-bit stack ABI. Guest buffers remain pinned during host output. */
static uint32 bridge32_DosPutMessage(uint8 *arguments)
{
    const uint32 handle = lx_guest_load_u32(arguments);
    const uint32 size = lx_guest_load_u32(arguments + 4);
    const uint32 pointer = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow buffer = {0};
    if (size && !lx_execution_borrow(GLoaderState.execution, pointer, size,
                                     LX_GUEST_READ, &buffer)) return ERROR_INVALID_PARAMETER;
    const uint32 result = DosPutMessage(handle, size, buffer.host);
    lx_execution_release(&buffer);
    return result;
}

APIRET lx_message_get(PVOID, PVOID, ULONG, PCHAR, ULONG, ULONG, PCHAR, PULONG);
static uint32 bridge32_DosTrueGetMessage(uint8 *a)
{
    const uint32 count = lx_guest_load_u32(a + 8), size = lx_guest_load_u32(a + 16);
    if (count > 9 || !size) return ERROR_INVALID_PARAMETER;
    char *name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a + 24), 260);
    if (!name) return ERROR_INVALID_PARAMETER;
    LxGuestBorrow table = {0}, data = {0}, length = {0};
    char *substitutions[9] = {0};
    uint32 result = ERROR_INVALID_PARAMETER;
    if ((count && !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), count * 4,
                                       LX_GUEST_READ, &table)) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12), size, LX_GUEST_WRITE, &data) ||
        !lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 28), 4, LX_GUEST_WRITE, &length)) goto done;
    for (uint32 i = 0; i < count; i++) {
        uint32 pointer = lx_guest_load_u32((uint8 *) table.host + i * 4);
        if (pointer) {
            substitutions[i] = lx_execution_copy_string(GLoaderState.execution, pointer, 65536);
            if (!substitutions[i]) goto done;
        }
    }
    ULONG actual = 0;
    result = lx_message_get(NULL, substitutions, count, data.host, size, lx_guest_load_u32(a + 20), name, &actual);
    if (!result) lx_guest_store_u32(length.host, actual);
done:
    for (uint32 i = 0; i < count; i++) free(substitutions[i]);
    lx_execution_release(&table); lx_execution_release(&data); lx_execution_release(&length); free(name); return result;
}
