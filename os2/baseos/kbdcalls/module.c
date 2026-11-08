/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2/include/private/export16.h"
#include "os2/include/kbdcalls.h"
#include "host/terminal.h"
#include "host/clock.h"

#include "os2/baseos/kbdcalls/generated/kbdcalls-lx.h"

static USHORT keyboard_codepage = 437;
static KBDINFO keyboard_status = {sizeof(KBDINFO), 0x0009, '\r', 0, 0};
static int keyboard_pending = -1;

static void key_info(PKBDKEYINFO info, int ch)
{
    memset(info, 0, sizeof(*info));
    info->fsState = keyboard_status.fsState;
    if (ch >= 0)
    {
        info->chChar = ch == '\n' ? '\r' : ch;
        info->fbStatus = 0x40; /* A complete character is available. */
        info->time = (ULONG)(lx_host_monotonic_us() / 1000);
    }
}

static int read_key(int wait)
{
    int ch = keyboard_pending;
    if (ch >= 0)
        keyboard_pending = -1;
    else
        ch = lx_host_terminal_read_input(wait);
    return ch;
}

APIRET16 KbdPeek(PKBDKEYINFO pkbci, HKBD hkbd)
{
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (!pkbci)
        return ERROR_KBD_PARAMETER;
    if (keyboard_pending < 0)
        keyboard_pending = lx_host_terminal_read_input(0);
    key_info(pkbci, keyboard_pending);
    return NO_ERROR;
}

APIRET16 KbdGetCp(ULONG reserved, PUSHORT codepage, HKBD hkbd)
{
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (reserved || !codepage)
        return ERROR_KBD_PARAMETER;
    *codepage = keyboard_codepage;
    return NO_ERROR;
}

APIRET16 KbdSetCp(USHORT reserved, USHORT codepage, HKBD hkbd)
{
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (reserved)
        return ERROR_KBD_PARAMETER;
    if (codepage != 437 && codepage != 850)
        return ERROR_KBD_INVALID_CODEPAGE;
    keyboard_codepage = codepage;
    return NO_ERROR;
}

APIRET16 KbdFlushBuffer(HKBD hkbd)
{
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    keyboard_pending = -1;
    lx_host_terminal_flush_input();
    return NO_ERROR;
}

APIRET16 KbdCharIn(PKBDKEYINFO pkbci, USHORT fWait, HKBD hkbd)
{
    TRACE_NATIVE("KbdCharIn(%p, %u, %u)", pkbci, fWait, hkbd);
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (!pkbci)
        return ERROR_KBD_PARAMETER;
    if (fWait > 1)
        return ERROR_KBD_INVALID_IOWAIT;
    key_info(pkbci, read_key(fWait == 0));
    return NO_ERROR;
}

APIRET16 KbdGetStatus(PKBDINFO pkbci, HKBD hkbd)
{
    TRACE_NATIVE("KbdGetStatus(%p, %u)", pkbci, hkbd);
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (!pkbci || pkbci->cb != sizeof(*pkbci))
        return ERROR_KBD_INVALID_LENGTH;
    *pkbci = keyboard_status;
    return NO_ERROR;
}

APIRET16 KbdSetStatus(PKBDINFO pkbci, HKBD hkbd)
{
    TRACE_NATIVE("KbdSetStatus(%p, %u)", pkbci, hkbd);
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (!pkbci || pkbci->cb != sizeof(*pkbci))
        return ERROR_KBD_INVALID_LENGTH;
    if ((pkbci->fsMask & 3) == 3)
        return ERROR_KBD_INVALID_ECHO_MASK;
    if ((pkbci->fsMask & 12) == 12)
        return ERROR_KBD_INVALID_INPUT_MASK;
    if (pkbci->fsMask & ~0x01ffu)
        return ERROR_KBD_INVALID_MASK;
    if (pkbci->fsMask & 3)
        keyboard_status.fsMask = (keyboard_status.fsMask & ~3u) | (pkbci->fsMask & 3);
    if (pkbci->fsMask & 12)
        keyboard_status.fsMask = (keyboard_status.fsMask & ~12u) | (pkbci->fsMask & 12);
    if (pkbci->fsMask & 0x10)
        keyboard_status.fsState = pkbci->fsState;
    if (pkbci->fsMask & 0x20)
        keyboard_status.fsInterim = pkbci->fsInterim;
    if (pkbci->fsMask & 0x40)
        keyboard_status.chTurnAround = pkbci->chTurnAround;
    return NO_ERROR;
}

APIRET16 KbdStringIn(PCHAR pch, PSTRINGINBUF pchin, USHORT flag, HKBD hkbd)
{
    TRACE_NATIVE("KbdStringIn(%p, %p, %u, %u)", pch, pchin, flag, hkbd);
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (flag > 1)
        return ERROR_KBD_INVALID_IOWAIT;
    if (!pch || !pchin)
        return ERROR_INVALID_PARAMETER;
    int count = 0;
    int chr = -1;
    while (count < pchin->cb)
    {
        chr = read_key(flag == 0);
        if (chr < 0 || chr == '\r' || chr == '\n')
            break;
        pch[count++] = chr;
    }
    if (count < pchin->cb && chr >= 0)
    {
        pch[count] = '\r';
    }
    if (pchin)
        pchin->cchIn = count;
    return NO_ERROR;
}
