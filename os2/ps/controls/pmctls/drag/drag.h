/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared between the PMCTLS drag units. */
#ifndef TWOINE_PMCTLS_DRAG_H
#define TWOINE_PMCTLS_DRAG_H
#include "os2/include/os2def.h"

enum
{
    /* The DRAGINFO header in guest memory (pmstddlg.h DRAGINFO, 32-bit packing); the DRAGITEM
       array follows it. */
    DRAGINFO_CBDRAGINFO_OFFSET = 0,
    DRAGINFO_CBDRAGITEM_OFFSET = 4,
    DRAGINFO_USOPERATION_OFFSET = 6,
    DRAGINFO_HWNDSOURCE_OFFSET = 8,
    DRAGINFO_XDROP_OFFSET = 12,
    DRAGINFO_YDROP_OFFSET = 14,
    DRAGINFO_CDITEM_OFFSET = 16,
    DRAGINFO_WIRE_SIZE = 20,
    /* One DRAGITEM (pmstddlg.h DRAGITEM); its string handles run from hstrType to
       hstrTargetName. */
    DRAGITEM_HSTRTYPE_OFFSET = 8,
    DRAGITEM_HSTRRMF_OFFSET = 12,
    DRAGITEM_HSTRTARGETNAME_OFFSET = 24,
    DRAGITEM_WIRE_SIZE = 36
};

/* Copies the DRAGINFO header at a guest address that DrgAllocDraginfo returned. */
LX_PRIVATE BOOL drag_info(ULONG address, uint8 wire[DRAGINFO_WIRE_SIZE]);
#endif
