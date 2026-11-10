/* Public domain. No rights reserved. Wholly AI-authored. */
/* Original Open32 INC/DEC return a sign; the guest word retains all32 bits. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"

/* lx_execution_atomic_u32 operations. */
enum
{
    ATOMIC_EXCHANGE = 1,
    ATOMIC_ADD = 2
};

static int interlocked_word(PLONG value, int operation, uint32 delta, uint32 *previous)
{
    uint32 address;
    if (!GProcessState.execution || !value ||
        !lx_execution_address(GProcessState.execution, value, sizeof *value,
                              LX_GUEST_READ | LX_GUEST_WRITE, &address) ||
        !lx_execution_atomic_u32(GProcessState.execution, address, operation, delta, previous))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return 1;
}

static LONG interlocked_add(PLONG value, uint32 delta)
{
    uint32 previous;
    if (!interlocked_word(value, ATOMIC_ADD, delta, &previous))
        return 0;
    uint32 result = previous + delta;
    return !result ? 0 : (result & UINT32_C(0x80000000)) ? -1 : 1;
}

LONG InterlockedIncrement(PLONG value)
{
    TRACE_NATIVE("InterlockedIncrement(%p)", value);
    return interlocked_add(value, 1);
}

LONG InterlockedDecrement(PLONG value)
{
    TRACE_NATIVE("InterlockedDecrement(%p)", value);
    return interlocked_add(value, UINT32_MAX);
}

LONG InterlockedExchange(PLONG value, LONG replacement)
{
    TRACE_NATIVE("InterlockedExchange(%p, %d)", value, replacement);
    uint32 previous;
    if (!interlocked_word(value, ATOMIC_EXCHANGE, (uint32)replacement, &previous))
        return 0;
    LONG result;
    memcpy(&result, &previous, sizeof result);
    return result;
}
