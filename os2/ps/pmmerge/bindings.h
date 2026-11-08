#include "os2/include/private/pmp.h"

/* Public domain. No rights reserved. Wholly AI-authored. */
static uint32 bridge32_WinQueryCpType(uint8 *arguments)
{
    return WinQueryCpType(lx_guest_load_u32(arguments));
}
