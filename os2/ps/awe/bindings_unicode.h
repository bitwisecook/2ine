/* Public domain. No rights reserved. Wholly AI-authored. */
/* Counted character conversions snapshot guest input and bound each output span. */
enum
{
    OPEN32_UNICODE_WIRE_LIMIT = 16 * 1024 * 1024
};

static void *open32_unicode_snapshot(uint32 address, LONG count, unsigned unit, size_t *bytes)
{
    *bytes = 0;
    if (!address || !count || count < -1 ||
        (count > 0 && (uint64_t)count * unit > OPEN32_UNICODE_WIRE_LIMIT))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    size_t capacity = count > 0 ? (size_t)count * unit : 256;
    uint8 *copy = malloc(capacity);
    if (!copy)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (count > 0)
    {
        if ((uint64_t)address + capacity > (UINT64_C(1) << 32) ||
            !lx_execution_read(GProcessState.execution, address, copy, capacity))
            goto invalid;
        *bytes = capacity;
        return copy;
    }
    for (size_t size = 0; size < OPEN32_UNICODE_WIRE_LIMIT; size += unit)
    {
        if (size + unit > capacity)
        {
            uint8 *grown = realloc(copy, capacity * 2);
            if (!grown)
            {
                free(copy);
                open32SetError(ERROR_NOT_ENOUGH_MEMORY);
                return NULL;
            }
            copy = grown;
            capacity *= 2;
        }
        if ((uint64_t)address + size + unit > (UINT64_C(1) << 32) ||
            !lx_execution_read(GProcessState.execution, address + (uint32)size, copy + size, unit))
            goto invalid;
        if (unit == 1 ? !copy[size] : !lx_guest_load_u16(copy + size))
        {
            *bytes = size + unit;
            return copy;
        }
    }
invalid:
    free(copy);
    open32SetError(ERROR_INVALID_PARAMETER);
    return NULL;
}

static int open32_unicode_overlap(uint32 source, size_t bytes, uint32 output, size_t capacity)
{
    return output && capacity &&
           (source == output || (capacity && (uint64_t)source < (uint64_t)output + capacity &&
                                 (uint64_t)output < (uint64_t)source + bytes));
}

static uint32 bridge32_MultiByteToWideChar(uint8 *a)
{
    uint32 source = lx_guest_load_u32(a + 8), output = lx_guest_load_u32(a + 16);
    LONG count = (LONG)lx_guest_load_u32(a + 12), capacity = (LONG)lx_guest_load_u32(a + 20);
    LxGuestBorrow destination = {0};
    uint32 result = 0;
    size_t bytes = 0;
    char *input = NULL;
    if (capacity < 0 || (uint64_t)capacity * 2 > OPEN32_UNICODE_WIRE_LIMIT)
        open32SetError(ERROR_INVALID_PARAMETER);
    else if ((input = open32_unicode_snapshot(source, count, 1, &bytes)))
    {
        if (open32_unicode_overlap(source, bytes, output, (size_t)capacity * 2))
            open32SetError(ERROR_INVALID_PARAMETER);
        else if (capacity &&
                 !lx_execution_borrow(GProcessState.execution, output, (size_t)capacity * 2,
                                      LX_GUEST_WRITE, &destination))
            open32SetError(ERROR_INVALID_PARAMETER);
        else
            result = (uint32)MultiByteToWideChar(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                                 input, count, destination.host, capacity);
    }
    lx_execution_release(&destination);
    free(input);
    return result;
}

static uint32 bridge32_WideCharToMultiByte(uint8 *a)
{
    uint32 source = lx_guest_load_u32(a + 8), output = lx_guest_load_u32(a + 16);
    uint32 default_address = lx_guest_load_u32(a + 24), used_address = lx_guest_load_u32(a + 28);
    LONG count = (LONG)lx_guest_load_u32(a + 12), capacity = (LONG)lx_guest_load_u32(a + 20);
    LxGuestBorrow destination = {0}, used = {0};
    char default_text[14];
    uint8 *input = NULL;
    size_t bytes = 0;
    uint32 result = 0;
    if (capacity < 0)
    {
        open32SetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if ((uint64_t)capacity > OPEN32_UNICODE_WIRE_LIMIT)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    input = open32_unicode_snapshot(source, count, 2, &bytes);
    if (!input)
        return 0;
    if (open32_unicode_overlap(source, bytes, output, (size_t)capacity))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        goto done;
    }
    if (default_address)
    {
        size_t i;
        for (i = 0; i < sizeof default_text; i++)
        {
            if ((uint64_t)default_address + i >= (UINT64_C(1) << 32) ||
                !lx_execution_read(GProcessState.execution, default_address + (uint32)i,
                                   default_text + i, 1))
            {
                open32SetError(ERROR_INVALID_PARAMETER);
                goto done;
            }
            if (!default_text[i])
                break;
        }
        if (!i || i == sizeof default_text)
        {
            open32SetError(ERROR_INTERNAL_ERROR);
            goto done;
        }
    }
    if ((capacity && !lx_execution_borrow(GProcessState.execution, output, (size_t)capacity,
                                          LX_GUEST_WRITE, &destination)) ||
        (used_address &&
         !lx_execution_borrow(GProcessState.execution, used_address, 4, LX_GUEST_WRITE, &used)))
        open32SetError(ERROR_INVALID_PARAMETER);
    else
        result = (uint32)WideCharToMultiByte(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), input,
                                             count, destination.host, capacity,
                                             default_address ? default_text : NULL, used.host);
done:
    lx_execution_release(&used);
    lx_execution_release(&destination);
    free(input);
    return result;
}
