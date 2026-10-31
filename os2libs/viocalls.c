/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "viocalls.h"

#include "hal/terminal.h"
#include "os2libs/generated/viocalls-lx.h"

static uint16 *vio_buffer = NULL;
static uint16 vio_scrw, vio_scrh;
static uint16 vio_curx, vio_cury;
static VIOCURSORINFO vio_cursorinfo;

static int initNcurses(void)
{
    if (vio_buffer != NULL)
        return 1;

    int scrh, scrw;
    if (!lx_host_terminal_open(&scrw, &scrh))
        return 0;

    const size_t buflen = scrw * scrh * sizeof(uint16);
    vio_buffer = malloc(buflen);
    if (!vio_buffer)
    {
        lx_host_terminal_close();
        return 0;
    }

    memset(vio_buffer, '\0', buflen);
    vio_scrw = (uint16)scrw;
    vio_scrh = (uint16)scrh;
    vio_curx = vio_cury = 0;

    FIXME("these are just the default values OS/2 4.52 returns");
    vio_cursorinfo.yStart = 15;
    vio_cursorinfo.cEnd = 15;
    vio_cursorinfo.cx = 1;
    vio_cursorinfo.attr = 0;

    return 1;
}

static void deinitNcurses(void)
{
    if (!vio_buffer)
        return;

    lx_host_terminal_close();
    free(vio_buffer);
    vio_buffer = NULL;
    vio_curx = vio_cury = vio_scrw = vio_scrh = 0;
}

static inline void commitToNcurses(void)
{
    lx_host_terminal_refresh();
}

static void pushToNcurses(const int y, const int x, int numcells, const int commit)
{
    const uint8 *src = (const uint8 *)(vio_buffer + ((y * vio_scrw) + x));
    const uint32 avail = (((vio_scrh - y) * vio_scrw) - x);
    if (numcells > avail)
        numcells = avail;

    lx_host_terminal_cells(x, y, (const uint16_t *)src, numcells, vio_curx, vio_cury, commit);
}

APIRET16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio)
{
    TRACE_NATIVE("VioGetMode(%p, %u)", pvioModeInfo, (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (pvioModeInfo == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (pvioModeInfo->cb != sizeof(*pvioModeInfo))
        return ERROR_VIO_INVALID_LENGTH;

    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    memset(pvioModeInfo, '\0', sizeof(*pvioModeInfo));
    pvioModeInfo->cb = sizeof(*pvioModeInfo);
    pvioModeInfo->fbType = VGMT_OTHER;
    pvioModeInfo->color = 4;
    pvioModeInfo->col = vio_scrw;
    pvioModeInfo->row = vio_scrh;
    pvioModeInfo->hres = 640;
    pvioModeInfo->vres = 400;
    pvioModeInfo->fmt_ID = 0;
    pvioModeInfo->attrib = 1;
    FIXME("fill in the rest of these");

    return NO_ERROR;
}

APIRET16 VioGetCurPos(PUSHORT pusRow, PUSHORT pusColumn, HVIO hvio)
{
    TRACE_NATIVE("VioGetCurPos(%p, %p, %u)", pusRow, pusColumn, (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    if (pusRow)
        *pusRow = vio_cury;

    if (pusColumn)
        *pusColumn = vio_curx;

    return NO_ERROR;
}

APIRET16 VioGetBuf(PULONG pLVB, PUSHORT pcbLVB, HVIO hvio)
{
    TRACE_NATIVE("VioGetBuf(%p, %p, %u)", pLVB, pcbLVB, (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    if (pLVB)
        *pLVB = (ULONG)vio_buffer;
    if (pcbLVB)
        *pcbLVB = vio_scrw * vio_scrh * sizeof(ULONG);
    return NO_ERROR;
}

APIRET16 VioGetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio)
{
    TRACE_NATIVE("VioGetCurType(%p, %u)", pvioCursorInfo, (uint)hvio);
    return NO_ERROR;

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (pvioCursorInfo == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    memcpy(pvioCursorInfo, &vio_cursorinfo, sizeof(*pvioCursorInfo));
    return NO_ERROR;
}

APIRET16 VioScrollUp(USHORT usTopRow, USHORT usLeftCol, USHORT usBotRow, USHORT usRightCol,
                     USHORT cbLines, PBYTE pCell, HVIO hvio)
{
    TRACE_NATIVE("VioScrollUp(%u, %u, %u, %u, %u, %p, %u)", (uint)usTopRow, (uint)usLeftCol,
                 (uint)usBotRow, (uint)usRightCol, (uint)cbLines, pCell, (uint)hvio);
    FIXME("buggy");
    return NO_ERROR;
    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (pCell == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    if (usLeftCol >= vio_scrw)
        usLeftCol = vio_scrw - 1;
    if (usRightCol >= vio_scrw)
        usRightCol = vio_scrw - 1;
    if (usTopRow >= vio_scrh)
        usTopRow = vio_scrh - 1;
    if (usBotRow >= vio_scrh)
        usBotRow = vio_scrh - 1;

    if (usLeftCol >= usRightCol)
        return NO_ERROR;
    else if (usTopRow >= usBotRow)
        return NO_ERROR;
    else if (cbLines == 0)
        return NO_ERROR;

    const uint32 rowlen = (usRightCol - usLeftCol) + 1;
    const uint32 overlines = (cbLines > usTopRow) ? (cbLines - usTopRow) : 0;
    const uint32 collen = ((usBotRow - usTopRow) + 1) - overlines;
    const uint32 adjust = overlines * vio_scrw;

    uint16 *src = vio_buffer + ((usTopRow * vio_scrw) + usLeftCol) + adjust;
    uint16 *dst = (src - (cbLines * vio_scrw)) + adjust;
    const size_t rowcpylen = rowlen * sizeof(uint16);
    for (uint32 i = 0; i < collen; i++)
    {
        memcpy(dst, src, rowcpylen);
        src += vio_scrw;
        dst += vio_scrw;
    }

    const uint16 clear_cell = *((uint16 *)pCell);
    for (uint32 i = 0; i < cbLines; i++)
    {
        if (dst >= (vio_buffer + (vio_scrw * vio_scrh)))
            break;
        uint16 *origdst = dst;
        for (uint32 j = 0; j < rowlen; j++)
        {
            *(dst++) = clear_cell;
        }
        dst = origdst + vio_scrw;
    }

    int starty = usTopRow - cbLines;
    if (starty < 0)
        starty = 0;
    int endy = starty + collen + cbLines;
    if (endy >= vio_scrh)
        endy = vio_scrh;

    for (int y = starty; y < endy; y++)
        pushToNcurses(y, usLeftCol, rowlen, 0);
    commitToNcurses();

    return NO_ERROR;
}

APIRET16 VioSetCurPos(USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioSetCurPos(%u, %u, %u)", (uint)usRow, (uint)usColumn, (uint)hvio);
    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    vio_cury = usRow;
    vio_curx = usColumn;

    lx_host_terminal_cursor(vio_curx, vio_cury);

    return NO_ERROR;
}

APIRET16 VioSetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio)
{
    TRACE_NATIVE("VioSetCurType(%p, %u)", pvioCursorInfo, (uint)hvio);
    FIXME("write me");
    return NO_ERROR;
}

APIRET16 VioReadCellStr(PCH pchCellStr, PUSHORT pcb, USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioReadCellStr(%p, %p, %u, %u, %u)", pchCellStr, pcb, (uint)usRow, (uint)usColumn,
                 (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint32 maxidx = ((uint32)vio_scrh) * ((uint32)vio_scrw);
    const uint32 idx = (((uint32)usRow) * ((uint32)vio_scrw)) + ((uint32)usColumn);
    const uint32 avail = (maxidx - idx) * sizeof(uint16);
    if (((uint32)*pcb) > avail)
        *pcb = (USHORT)avail;
    memcpy(pchCellStr, vio_buffer + idx, (size_t)*pcb);
    return NO_ERROR;
}

APIRET16 VioWrtCellStr(PCH pchCellStr, USHORT cb, USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioWrtCellStr(%p, %u, %u, %u, %u)", pchCellStr, (uint)cb, (uint)usRow,
                 (uint)usColumn, (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint16 *src = (uint16 *)pchCellStr;
    uint16 *dst = vio_buffer + ((usRow * vio_scrw) + usColumn);
    const uint32 avail = (((vio_scrh - usRow) * vio_scrw) - usColumn) * sizeof(uint16);
    if (((uint32)cb) > avail)
        cb = (USHORT)avail;

    memcpy(dst, src, cb);

    pushToNcurses(usRow, usColumn, cb / sizeof(uint16), 1);

    return NO_ERROR;
}

APIRET16 VioWrtCharStrAtt(PCH pch, USHORT cb, USHORT usRow, USHORT usColumn, PBYTE pAttr, HVIO hvio)
{
    TRACE_NATIVE("VioWrtCharStrAtt(%p, %u, %u, %u, %p, %u)", pch, (uint)cb, (uint)usRow,
                 (uint)usColumn, pAttr, (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint8 attr = *pAttr;
    const uint8 *src = (uint8 *)pch;
    uint8 *dst = (uint8 *)(vio_buffer + ((usRow * vio_scrw) + usColumn));
    const uint32 avail = (((vio_scrh - usRow) * vio_scrw) - usColumn);
    if (((uint32)cb) > avail)
        cb = (USHORT)avail;

    for (uint32 i = 0; i < cb; i++, src++)
    {
        *(dst++) = *src;
        *(dst++) = attr;
    }

    pushToNcurses(usRow, usColumn, cb, 1);

    return NO_ERROR;
}

APIRET16 VioWrtNCell(PBYTE pCell, USHORT cb, USHORT usRow, USHORT usColumn, HVIO hvio)
{
    TRACE_NATIVE("VioWrtNCell(%p, %u, %u, %u, %u)", pCell, (uint)cb, (uint)usRow, (uint)usColumn,
                 (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    else if (usRow >= vio_scrh)
        return ERROR_VIO_ROW;
    else if (usColumn >= vio_scrw)
        return ERROR_VIO_COL;

    const uint16 cell = *((uint16 *)pCell);
    uint16 *dst = vio_buffer + ((usRow * vio_scrw) + usColumn);
    const uint32 avail = (((vio_scrh - usRow) * vio_scrw) - usColumn);
    if (((uint32)cb) > avail)
        cb = (USHORT)avail;

    for (uint32 i = 0; i < cb; i++)
        *(dst++) = cell;

    pushToNcurses(usRow, usColumn, cb, 1);

    return NO_ERROR;
}

LX_NATIVE_DESTRUCTOR(viocalls)
{
    deinitNcurses();
}
