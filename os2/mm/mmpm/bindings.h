/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest ABI bridges for the MCI and MMIO entries. */
enum
{
    /* Longest MCI command string or MMIO file name copied from the guest. */
    MCI_COMMAND_STRING_LIMIT = 65536,
    MMIO_NAME_LIMIT = 512,
    MIX_WRITE_ENTRY_PAGE_SIZE = 4096,
    /* The mixer write entry takes a handle, a buffer list and a buffer count. */
    MIX_WRITE_ARGUMENT_BYTES = 3 * sizeof(uint32)
};

static uint32 bridge32_mixWrite(uint8 *a)
{
    return lx_admc_queue_buffers(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                 lx_guest_load_u32(a + 8));
}

static uint32 bridge32_mciSendCommand(uint8 *a)
{
    USHORT message = lx_guest_load_u32(a + 4);
    LxGuestBorrow parameters = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12),
                             lx_mci_parameter_size(message, lx_guest_load_u32(a + 8)),
                             LX_GUEST_READ | LX_GUEST_WRITE, &parameters))
        return MCIERR_INVALID_BUFFER;
    uint32 result = mciSendCommand(lx_guest_load_u32(a), message, lx_guest_load_u32(a + 8),
                                   parameters.host, lx_guest_load_u32(a + 16));
    lx_execution_release(&parameters);
    return result;
}

static uint32 bridge32_mciSendString(uint8 *a)
{
    char *command = lx_execution_copy_string(GProcessState.execution, lx_guest_load_u32(a),
                                             MCI_COMMAND_STRING_LIMIT);
    LxGuestBorrow output = {0};
    uint32 size = lx_guest_load_u32(a + 8), result = MCIERR_INVALID_BUFFER;
    if (command && (!size || lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4),
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
    if (size && lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), size,
                                    LX_GUEST_WRITE, &output))
        result = mciGetErrorString(lx_guest_load_u32(a), output.host, size);
    lx_execution_release(&output);
    return result;
}

/* DART's write callback is a guest-callable API entry, separate from exported ordinals. */
ULONG lx_admc_write_entry(void)
{
    static ULONG write_entry;
    if (write_entry)
        return write_entry;
    LxGuestMapping *mapping = lx_execution_map(
        GProcessState.execution, 0, MIX_WRITE_ENTRY_PAGE_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return 0;
    const LxExecutionApi32 api = {bridge32_mixWrite, MIX_WRITE_ARGUMENT_BYTES, 0, LX_ABI_STACK32};
    if (!lx_execution_emit_bridge32(GProcessState.execution, lx_execution_mapping_host(mapping),
                                    MIX_WRITE_ENTRY_PAGE_SIZE, &api) ||
        !lx_execution_protect(GProcessState.execution, mapping, LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        lx_execution_unmap(GProcessState.execution, mapping);
        return 0;
    }
    write_entry = lx_execution_mapping_address(mapping);
    return write_entry;
}

static uint32 bridge32_mmioOpen(uint8 *a)
{
    LxGuestBorrow info = {0};
    uint32 name_address = lx_guest_load_u32(a), info_address = lx_guest_load_u32(a + 4);
    /* A file name makes MMIOINFO optional; a memory file requires it. */
    /* The IOProc's view of MMIOINFO is copied back before this returns. */
    if ((info_address || !name_address) &&
        !lx_execution_borrow(GProcessState.execution, info_address, MMIOINFO_SIZE,
                             LX_GUEST_READ | LX_GUEST_WRITE, &info))
        return 0;
    char *name = name_address ? lx_execution_copy_string(GProcessState.execution, name_address,
                                                         MMIO_NAME_LIMIT)
                              : NULL;
    uint32 result = !name_address || name ? mmioOpen(name, info.host, lx_guest_load_u32(a + 8)) : 0;
    free(name);
    lx_execution_release(&info);
    return result;
}

static uint32 bridge32_mmioInstallIOProc(uint8 *a)
{
    return mmioInstallIOProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                             lx_guest_load_u32(a + 8));
}

static uint32 bridge32_mmioClose(uint8 *a)
{
    return mmioClose(lx_guest_load_u32(a), (USHORT)lx_guest_load_u32(a + 4));
}

static uint32 mmio_transfer_bridge(uint8 *a, int writing)
{
    LxGuestBorrow data = {0};
    int32_t count = (int32_t)lx_guest_load_u32(a + 8);
    uint32 result = MMIO_ERROR;
    if (!count && lx_guest_load_u32(a + 4))
        return 0;
    if (count >= 0 &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), (size_t)count,
                            writing ? LX_GUEST_READ : LX_GUEST_WRITE, &data))
        result = writing ? mmioWrite(lx_guest_load_u32(a), data.host, count)
                         : mmioRead(lx_guest_load_u32(a), data.host, count);
    lx_execution_release(&data);
    return result;
}

static uint32 bridge32_mmioRead(uint8 *a)
{
    return mmio_transfer_bridge(a, 0);
}

static uint32 bridge32_mmioWrite(uint8 *a)
{
    return mmio_transfer_bridge(a, 1);
}

static uint32 bridge32_mmioSeek(uint8 *a)
{
    return mmioSeek(lx_guest_load_u32(a), (LONG)lx_guest_load_u32(a + 4),
                    (LONG)lx_guest_load_u32(a + 8));
}

static uint32 bridge32_mmioGetHeader(uint8 *a)
{
    LxGuestBorrow data = {0}, count = {0};
    int32_t capacity = (int32_t)lx_guest_load_u32(a + 8);
    uint32 result = MMIOERR_INVALID_PARAMETER;
    if (capacity > 0 &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), (size_t)capacity,
                            LX_GUEST_WRITE, &data) &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), sizeof(LONG),
                            LX_GUEST_WRITE, &count))
    {
        LONG bytes = 0;
        result = mmioGetHeader(lx_guest_load_u32(a), data.host, capacity, &bytes,
                               lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20));
        lx_guest_store_u32(count.host, bytes);
    }
    lx_execution_release(&count);
    lx_execution_release(&data);
    return result;
}
