/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2/include/private/export16.h"
#include "os2/include/viocalls.h"

#include "host/terminal.h"
#include "emulator/launch_record.h"
#include "os2/baseos/viocalls/generated/viocalls-lx.h"

static uint16 *vio_buffer = NULL;
static uint16 vio_scrw, vio_scrh;
static uint16 vio_curx, vio_cury;
static uint16 vio_hostw, vio_hosth;
static uint16 vio_codepage = 437;
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

    for (size_t i = 0; i < buflen / sizeof(uint16); i++)
        vio_buffer[i] = 0x0720; /* White space on a black text background. */
    vio_scrw = (uint16)scrw;
    vio_scrh = (uint16)scrh;
    vio_hostw = vio_scrw;
    vio_hosth = vio_scrh;
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

    if (lx_launch_no_autoclose())
    {
        lx_host_terminal_refresh();
        lx_host_terminal_flush_input();
        lx_host_terminal_read_input(1);
    }
    lx_host_terminal_close();
    free(vio_buffer);
    vio_buffer = NULL;
    vio_curx = vio_cury = vio_scrw = vio_scrh = 0;
}

static inline void commitToNcurses(void)
{
    lx_host_terminal_refresh();
}

static void pushToNcurses(int y, int x, int numcells, const int commit)
{
    const uint8 *src = (const uint8 *)(vio_buffer + ((y * vio_scrw) + x));
    const uint32 avail = (((vio_scrh - y) * vio_scrw) - x);
    if (numcells > avail)
        numcells = avail;

    while (numcells > 0)
    {
        int count = vio_scrw - x;
        if (count > numcells)
            count = numcells;
        lx_host_terminal_cells(x, y, (const uint16_t *)src, count, vio_curx, vio_cury, 0);
        src += count * sizeof(uint16);
        numcells -= count;
        x = 0;
        y++;
    }
    if (commit)
        commitToNcurses();
}

APIRET16 VioGetAnsi(PUSHORT enabled, HVIO hvio)
{
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!enabled)
        return ERROR_VIO_INVALID_PARMS;
    *enabled = 0;
    return NO_ERROR;
}

APIRET16 VioSetCp(USHORT reserved, USHORT codepage, HVIO hvio)
{
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (reserved)
        return ERROR_VIO_BAD_RESERVE;
    if (codepage != 437 && codepage != 850)
        return ERROR_VIO_BAD_CP;
    vio_codepage = codepage;
    return NO_ERROR;
}

APIRET16 VioSetMode(PVIOMODEINFO info, HVIO hvio)
{
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!info)
        return ERROR_VIO_INVALID_PARMS;
    if (info->cb < 12)
        return ERROR_VIO_INVALID_LENGTH;
    if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    const USHORT cols = info->col ? info->col : vio_scrw;
    const USHORT rows = info->row ? info->row : vio_scrh;
    if ((info->fbType & VGMT_GRAPHICS) || !cols || !rows || cols > vio_hostw || rows > vio_hosth)
        return ERROR_VIO_MODE;
    uint16 *buffer = malloc((size_t)cols * rows * sizeof(uint16));
    if (!buffer)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (size_t i = 0; i < (size_t)cols * rows; i++)
        buffer[i] = 0x0720;
    free(vio_buffer);
    vio_buffer = buffer;
    vio_scrw = cols;
    vio_scrh = rows;
    vio_curx = vio_cury = 0;
    pushToNcurses(0, 0, cols * rows, 1);
    return NO_ERROR;
}

APIRET16 VioGetConfig(USHORT config, PVIOCONFIGINFO info, HVIO hvio)
{
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!info || config > 1)
        return ERROR_VIO_INVALID_PARMS;
    if (info->cb < 10)
        return ERROR_VIO_INVALID_LENGTH;
    const USHORT count = info->cb < sizeof(*info) ? info->cb : sizeof(*info);
    VIOCONFIGINFO result = {0};
    result.cb = count;
    result.adapter = 3; /* VGA adapter. */
    result.display = 4; /* VGA color monitor. */
    result.cbMemory = 256 * 1024;
    memcpy(info, &result, count);
    return NO_ERROR;
}

APIRET16 VioWrtTty(PCH pch, USHORT cb, HVIO hvio)
{
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!pch && cb)
        return ERROR_VIO_INVALID_PARMS;
    if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    for (USHORT i = 0; i < cb; i++)
    {
        const unsigned char ch = (unsigned char)pch[i];
        if (ch == '\r')
            vio_curx = 0;
        else if (ch == '\n')
            vio_cury++;
        else if (ch == '\b')
        {
            if (vio_curx)
                vio_curx--;
        }
        else if (ch == '\a')
            continue;
        else
        {
            const unsigned count = ch == '\t' ? 8 - vio_curx % 8 : 1;
            for (unsigned j = 0; j < count; j++)
            {
                uint16 *cell = vio_buffer + vio_cury * vio_scrw + vio_curx;
                *cell = (*cell & 0xff00u) | (ch == '\t' ? ' ' : ch);
                if (++vio_curx == vio_scrw)
                {
                    vio_curx = 0;
                    if (++vio_cury == vio_scrh)
                    {
                        memmove(vio_buffer, vio_buffer + vio_scrw,
                                (size_t)(vio_scrh - 1) * vio_scrw * sizeof(uint16));
                        vio_cury--;
                        for (unsigned k = 0; k < vio_scrw; k++)
                            vio_buffer[vio_cury * vio_scrw + k] = 0x0720;
                    }
                }
            }
        }
        if (vio_cury == vio_scrh)
        {
            memmove(vio_buffer, vio_buffer + vio_scrw,
                    (size_t)(vio_scrh - 1) * vio_scrw * sizeof(uint16));
            vio_cury--;
            for (unsigned k = 0; k < vio_scrw; k++)
                vio_buffer[vio_cury * vio_scrw + k] = 0x0720;
        }
    }
    pushToNcurses(0, 0, vio_scrw * vio_scrh, 1);
    return NO_ERROR;
}

APIRET16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio)
{
    TRACE_NATIVE("VioGetMode(%p, %u)", pvioModeInfo, (uint)hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;
    else if (pvioModeInfo == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (pvioModeInfo->cb < 12)
        return ERROR_VIO_INVALID_LENGTH;

    else if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;

    USHORT count =
        pvioModeInfo->cb < sizeof(*pvioModeInfo) ? pvioModeInfo->cb : sizeof(*pvioModeInfo);
    VIOMODEINFO info = {0};
    info.cb = count;
    info.fbType = VGMT_OTHER;
    info.color = 4;
    info.col = vio_scrw;
    info.row = vio_scrh;
    info.hres = 640;
    info.vres = 400;
    info.attrib = 1;
    memcpy(pvioModeInfo, &info, count);

    return NO_ERROR;
}

APIRET16 VioGetCp(USHORT reserved, PUSHORT codepage, HVIO hvio)
{
    if (reserved || !codepage)
        return ERROR_VIO_INVALID_PARMS;
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    *codepage = vio_codepage;
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
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!pCell)
        return ERROR_VIO_INVALID_PARMS;
    if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    if (usTopRow >= vio_scrh)
        return ERROR_VIO_TOPROW;
    if (usLeftCol >= vio_scrw)
        return ERROR_VIO_LEFTCOL;
    if (usBotRow >= vio_scrh)
        usBotRow = vio_scrh - 1;
    if (usRightCol >= vio_scrw)
        usRightCol = vio_scrw - 1;
    if (usTopRow > usBotRow || usLeftCol > usRightCol)
        return ERROR_VIO_INVALID_PARMS;
    const unsigned rows = usBotRow - usTopRow + 1;
    const unsigned columns = usRightCol - usLeftCol + 1;
    /* Zero lines clears the rectangle, as does a count exceeding its height. */
    const unsigned lines = !cbLines || cbLines > rows ? rows : cbLines;
    const uint16 cell = lx_guest_load_u16(pCell);
    for (unsigned row = 0; row < rows; row++)
    {
        uint16 *dst = vio_buffer + (usTopRow + row) * vio_scrw + usLeftCol;
        if (row + lines < rows)
            memmove(dst, dst + lines * vio_scrw, columns * sizeof(uint16));
        else
            for (unsigned col = 0; col < columns; col++)
                dst[col] = cell;
        pushToNcurses(usTopRow + row, usLeftCol, columns, 0);
    }
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

APIRET16 VioScrollDn(USHORT top, USHORT left, USHORT bottom, USHORT right, USHORT lines, PBYTE cell,
                     HVIO hvio)
{
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!cell)
        return ERROR_VIO_INVALID_PARMS;
    if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    if (top >= vio_scrh)
        return ERROR_VIO_TOPROW;
    if (left >= vio_scrw)
        return ERROR_VIO_LEFTCOL;
    if (bottom >= vio_scrh)
        bottom = vio_scrh - 1;
    if (right >= vio_scrw)
        right = vio_scrw - 1;
    if (top > bottom || left > right)
        return ERROR_VIO_INVALID_PARMS;
    const unsigned rows = bottom - top + 1;
    const unsigned columns = right - left + 1;
    const unsigned count = !lines || lines > rows ? rows : lines;
    const uint16 fill = lx_guest_load_u16(cell);
    for (unsigned row = rows; row-- > 0;)
    {
        uint16 *dst = vio_buffer + (top + row) * vio_scrw + left;
        if (row >= count)
            memmove(dst, dst - count * vio_scrw, columns * sizeof(uint16));
        else
            for (unsigned col = 0; col < columns; col++)
                dst[col] = fill;
        pushToNcurses(top + row, left, columns, 0);
    }
    commitToNcurses();
    return NO_ERROR;
}

APIRET16 VioReadCharStr(PCH pch, PUSHORT pcb, USHORT row, USHORT column, HVIO hvio)
{
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!pcb || (!pch && *pcb))
        return ERROR_VIO_INVALID_PARMS;
    if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    if (row >= vio_scrh)
        return ERROR_VIO_ROW;
    if (column >= vio_scrw)
        return ERROR_VIO_COL;
    const unsigned offset = row * vio_scrw + column;
    const unsigned available = (unsigned)vio_scrw * vio_scrh - offset;
    if (*pcb > available)
        *pcb = available;
    for (unsigned i = 0; i < *pcb; i++)
        pch[i] = (CHAR)vio_buffer[offset + i];
    return NO_ERROR;
}

APIRET16 VioSetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio)
{
    TRACE_NATIVE("VioSetCurType(%p, %u)", pvioCursorInfo, (uint)hvio);
    if (hvio)
        return ERROR_VIO_INVALID_HANDLE;
    if (!pvioCursorInfo)
        return ERROR_VIO_INVALID_PARMS;
    if (!initNcurses())
        return ERROR_VIO_INVALID_HANDLE;
    vio_cursorinfo = *pvioCursorInfo;
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
