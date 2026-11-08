/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2/include/private/export16.h"
#include "os2/include/bsesub.h"
#include "host/terminal.h"
#include "host/clock.h"

/* The echo pair and the binary/ASCII pair each select one of two modes. */
#define ECHO_MASK (KEYBOARD_ECHO_ON | KEYBOARD_ECHO_OFF)
#define INPUT_MODE_MASK (KEYBOARD_BINARY_MODE | KEYBOARD_ASCII_MODE)
/* Every fsMask bit KbdSetStatus accepts, through KEYBOARD_SHIFT_REPORT. */
#define STATUS_MASK 0x01ffu

static USHORT keyboard_codepage = 437;
static KBDINFO keyboard_status = {sizeof(KBDINFO), KEYBOARD_ECHO_ON | KEYBOARD_ASCII_MODE, '\r', 0,
                                  0};
static int keyboard_pending = -1;

static void key_info(PKBDKEYINFO info, int ch)
{
    memset(info, 0, sizeof(*info));
    info->fsState = keyboard_status.fsState;
    if (ch >= 0)
    {
        info->chChar = ch == '\n' ? '\r' : ch;
        info->fbStatus = KBDTRF_FINAL_CHAR_IN;
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
    if (fWait > IO_NOWAIT)
        return ERROR_KBD_INVALID_IOWAIT;
    key_info(pkbci, read_key(fWait == IO_WAIT));
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
    if ((pkbci->fsMask & ECHO_MASK) == ECHO_MASK)
        return ERROR_KBD_INVALID_ECHO_MASK;
    if ((pkbci->fsMask & INPUT_MODE_MASK) == INPUT_MODE_MASK)
        return ERROR_KBD_INVALID_INPUT_MASK;
    if (pkbci->fsMask & ~STATUS_MASK)
        return ERROR_KBD_INVALID_MASK;
    if (pkbci->fsMask & ECHO_MASK)
        keyboard_status.fsMask =
            (keyboard_status.fsMask & ~ECHO_MASK) | (pkbci->fsMask & ECHO_MASK);
    if (pkbci->fsMask & INPUT_MODE_MASK)
        keyboard_status.fsMask =
            (keyboard_status.fsMask & ~INPUT_MODE_MASK) | (pkbci->fsMask & INPUT_MODE_MASK);
    if (pkbci->fsMask & KEYBOARD_MODIFY_STATE)
        keyboard_status.fsState = pkbci->fsState;
    if (pkbci->fsMask & KEYBOARD_MODIFY_INTERIM)
        keyboard_status.fsInterim = pkbci->fsInterim;
    if (pkbci->fsMask & KEYBOARD_MODIFY_TURNAROUND)
        keyboard_status.chTurnAround = pkbci->chTurnAround;
    return NO_ERROR;
}

APIRET16 KbdStringIn(PCHAR pch, PSTRINGINBUF pchin, USHORT flag, HKBD hkbd)
{
    TRACE_NATIVE("KbdStringIn(%p, %p, %u, %u)", pch, pchin, flag, hkbd);
    if (hkbd)
        return ERROR_KBD_INVALID_HANDLE;
    if (flag > IO_NOWAIT)
        return ERROR_KBD_INVALID_IOWAIT;
    if (!pch || !pchin)
        return ERROR_INVALID_PARAMETER;
    int count = 0;
    int chr = -1;
    while (count < pchin->cb)
    {
        chr = read_key(flag == IO_WAIT);
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
