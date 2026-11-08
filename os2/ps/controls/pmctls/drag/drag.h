/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared between the PMCTLS drag units. */
#ifndef TWOINE_PMCTLS_DRAG_H
#define TWOINE_PMCTLS_DRAG_H
#include "os2/include/os2def.h"

enum
{
    /* Bytes of the DRAGINFO header in guest memory (pmstddlg.h DRAGINFO, 32-bit packing). */
    DRAGINFO_WIRE_SIZE = 20,
    /* Bytes of one DRAGITEM (pmstddlg.h DRAGITEM). */
    DRAGITEM_WIRE_SIZE = 36
};

/* Copies the DRAGINFO header at a guest address that DrgAllocDraginfo returned. */
LX_PRIVATE BOOL drag_info(ULONG address, uint8 wire[DRAGINFO_WIRE_SIZE]);
#endif
