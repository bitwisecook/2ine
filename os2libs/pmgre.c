/* Public domain. No rights reserved. Wholly AI-authored. */
/* Direct display-driver ownership is unavailable on a host window surface. */
#include "os2native.h"
#include "pmgre.h"
#include "os2libs/generated/pmgre-lx.h"
ULONG Gre32Entry3(ULONG first, ULONG second, ULONG operation)
{
    TRACE_NATIVE("Gre32Entry3(%u, %u, 0x%x)", first, second, operation);
    return 0;
}
ULONG Gre32Entry5(ULONG first, ULONG second, ULONG third, ULONG fourth, ULONG operation)
{
    TRACE_NATIVE("Gre32Entry5(%u, %u, %u, %u, 0x%x)", first, second, third, fourth, operation);
    return 0;
}
