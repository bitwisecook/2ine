/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_mixWrite(uint8 *a)
{
    return queue_buffers(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}
static uint32 bridge32_mciSendCommand(uint8 *a)
{
    USHORT message = lx_guest_load_u32(a + 4);
    LxGuestBorrow parameters = {0};
    if (!lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 12),
                             parameter_size(message), LX_GUEST_READ | LX_GUEST_WRITE, &parameters))
        return MCIERR_INVALID_BUFFER;
    uint32 result = mciSendCommand(lx_guest_load_u32(a), message, lx_guest_load_u32(a + 8),
                                   parameters.host, lx_guest_load_u32(a + 16));
    lx_execution_release(&parameters);
    return result;
}
static uint32 bridge32_mciSendString(uint8 *a)
{
    char *command = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(a), 65536);
    LxGuestBorrow output = {0};
    uint32 size = lx_guest_load_u32(a + 8), result = MCIERR_INVALID_BUFFER;
    if (command && (!size || lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4),
                                                 size, LX_GUEST_WRITE, &output)))
        result = mciSendString(command, output.host, size, lx_guest_load_u32(a + 12),
                               lx_guest_load_u32(a + 16));
    lx_execution_release(&output);
    free(command);
    return result;
}
static uint32 bridge32_mciGetErrorString(uint8 *a)
{
    LxGuestBorrow output = {0};
    uint32 size = lx_guest_load_u32(a + 8), result = 0;
    if (size && lx_execution_borrow(GLoaderState.execution, lx_guest_load_u32(a + 4), size,
                                    LX_GUEST_WRITE, &output))
        result = mciGetErrorString(lx_guest_load_u32(a), output.host, size);
    lx_execution_release(&output);
    return result;
}
/* DART's write callback is a guest-callable API entry, separate from exported ordinals. */
static int initialize_write_bridge(void)
{
    LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!mapping)
        return 0;
    const LxExecutionApi32 api = {bridge32_mixWrite, 12, 0, LX_ABI_STACK32};
    if (!lx_execution_emit_bridge32(GLoaderState.execution, lx_execution_mapping_host(mapping),
                                    4096, &api) ||
        !lx_execution_protect(GLoaderState.execution, mapping, 5))
    {
        lx_execution_unmap(GLoaderState.execution, mapping);
        return 0;
    }
    write_entry = lx_execution_mapping_address(mapping);
    return 1;
}
