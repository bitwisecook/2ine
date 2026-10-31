/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest pointer ownership is kept out of host API implementations. */
#include "arguments.h"
#include <stdlib.h>
#include <string.h>
int lx_guest_argument_far(LxExecution *e, uint32_t pointer, size_t size,
                           unsigned permissions, int live, int nullable, LxGuestArgument *a)
{
    memset(a, 0, sizeof *a);
    if (!pointer && (nullable || !size)) return 1;
    /* Snapshot output buffers so unwritten bytes retain their caller-supplied values. */
    unsigned access = permissions | (live ? 0 : LX_GUEST_READ);
    if (!lx_execution_borrow_far(e, pointer, size, access, &a->borrow)) return 0;
    a->size = size; a->permissions = permissions; a->host = a->borrow.host;
    if (!lx_execution_address(e, a->host, size, access, &a->address)) goto failed;
    if (!live) {
        /* Space for an aligned host POD object, including its tail padding. */
        a->allocation = calloc(1, size + 16);
        if (!a->allocation || !lx_execution_read(e, a->address, a->allocation, size)) goto failed;
        a->host = a->allocation;
    }
    return 1;
failed:
    lx_guest_argument_finish(a, 0); return 0;
}
int lx_guest_argument_strings(LxExecution *e, uint32_t pointer, unsigned count,
                               size_t maximum, int nullable, LxGuestArgument *a)
{
    memset(a, 0, sizeof *a);
    if (!pointer && nullable) return 1;
    LxGuestBorrow first;
    if (!lx_execution_borrow_far(e, pointer, 1, LX_GUEST_READ, &first)) return 0;
    uint32_t address;
    int ok = lx_execution_address(e, first.host, 1, LX_GUEST_READ, &address);
    lx_execution_release(&first);
    if (!ok) return 0;
    size_t remaining = 0x10000u - (pointer & 0xffffu);
    if (maximum > remaining) maximum = remaining;
    a->allocation = count == 1 ? lx_execution_copy_string(e, address, maximum) :
        lx_execution_copy_strings(e, address, maximum, count);
    a->host = a->allocation; return a->host != NULL;
}
void lx_guest_argument_finish(LxGuestArgument *a, int copy_back)
{
    if (a->allocation && a->borrow.mapping && copy_back && (a->permissions & LX_GUEST_WRITE))
        if (!lx_execution_write(a->borrow.execution, a->address, a->allocation, a->size)) abort();
    free(a->allocation); lx_execution_release(&a->borrow); memset(a, 0, sizeof *a);
}
