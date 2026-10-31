/* Public domain. No rights reserved. Wholly AI-authored. */
/* Scoped USE16 ABI arguments: checked spans, aligned snapshots and copy-back. */
#ifndef TWOINE_GUEST_ARGUMENTS_H
#define TWOINE_GUEST_ARGUMENTS_H
#include "runtime/execution.h"
typedef struct LxGuestArgument
{
    LxGuestBorrow borrow;
    void *host, *allocation;
    uint32_t address;
    size_t size;
    unsigned permissions;
} LxGuestArgument;
int lx_guest_argument_far(LxExecution *execution, uint32_t pointer, size_t size,
                          unsigned permissions, int live, int nullable, LxGuestArgument *argument);
/* Count=1 for a C string, two for an OS/2 command block, zero for environment. */
int lx_guest_argument_strings(LxExecution *execution, uint32_t pointer, unsigned count,
                              size_t maximum, int nullable, LxGuestArgument *argument);
void lx_guest_argument_finish(LxGuestArgument *argument, int copy_back);
#endif
