/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/pmwinx/batch_services.h"

static BOOL o32_service_span(ULONG address, uint64_t size, unsigned access, BOOL optional,
                             LxGuestBorrow *borrow)
{
    if (!address && (optional || !size))
        return TRUE;
    return size <= 16u * 1024u * 1024u && size <= UINT32_MAX &&
           (uint64_t)address + size <= (UINT64_C(1) << 32) &&
           lx_execution_borrow(GLoaderState.execution, address, (size_t)size, access, borrow);
}

static BOOL o32_service_string(ULONG address, BOOL identity, PSZ *output)
{
    if (!address || (identity && address <= 0xFFFF))
    {
        *output = (PSZ)(uintptr_t)address;
        return TRUE;
    }
    *output = lx_execution_copy_string(GLoaderState.execution, address, 1024u * 1024u);
    return *output != NULL;
}

static void o32_service_free_string(PSZ value)
{
    if ((uintptr_t)value > 0xFFFF)
        free(value);
}

static uint32 bridge32_AddAtom(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, TRUE, &s0) && valid;
    if (valid)
        result = (ULONG)AddAtom(s0);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_AdjustWindowRect(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 16, 3, FALSE, &b0) && valid;
    if (valid)
        result = (ULONG)AdjustWindowRect(b0.host, (ULONG)v1, (BOOL)v2);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_Beep(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)Beep((ULONG)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_BeginDeferWindowPos(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)BeginDeferWindowPos((LONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_CheckRadioButton(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    if (valid)
        result = (ULONG)CheckRadioButton((ULONG)v0, (LONG)v1, (LONG)v2, (LONG)v3);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_ChildWindowFromPoint(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)ChildWindowFromPoint((ULONG)v0, (LONG)v1, (LONG)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_ChooseColor(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 36, 3, FALSE, &b0) && valid;
    if (valid)
        result = (ULONG)ChooseColor(b0.host);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_ChooseFont(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 60, 3, FALSE, &b0) && valid;
    if (valid)
        result = (ULONG)ChooseFont(b0.host);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_ClipCursor(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 16, 1, TRUE, &b0) && valid;
    if (valid)
        result = (ULONG)ClipCursor(b0.host);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_CompareFileTime(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 8, 1, FALSE, &b0) && valid;
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 8, 1, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)CompareFileTime(b0.host, b1.host);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_CopyFile(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    if (valid)
        result = (ULONG)CopyFile(s0, s1, (BOOL)v2);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_CreateDialog(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    PSZ s1 = NULL;
    valid = o32_service_string(v1, TRUE, &s1) && valid;
    if (valid)
        result = (ULONG)CreateDialog((ULONG)v0, s1, (ULONG)v2, (ULONG)v3);
    else
        open32SetError(87);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_CreateDialogParam(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    PSZ s1 = NULL;
    valid = o32_service_string(v1, TRUE, &s1) && valid;
    if (valid)
        result = (ULONG)CreateDialogParam((ULONG)v0, s1, (ULONG)v2, (ULONG)v3, (ULONG)v4);
    else
        open32SetError(87);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_CreateDirectory(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 12, 1, TRUE, &b1) && valid;
    if (valid)
        result = (ULONG)CreateDirectory(s0, b1.host);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_CreateEvent(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 12, 1, TRUE, &b0) && valid;
    PSZ s3 = NULL;
    valid = o32_service_string(v3, FALSE, &s3) && valid;
    if (valid)
        result = (ULONG)CreateEvent(b0.host, (BOOL)v1, (BOOL)v2, s3);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    o32_service_free_string(s3);
    return result;
}

static uint32 bridge32_CreateMutex(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 12, 1, TRUE, &b0) && valid;
    PSZ s2 = NULL;
    valid = o32_service_string(v2, FALSE, &s2) && valid;
    if (valid)
        result = (ULONG)CreateMutex(b0.host, (BOOL)v1, s2);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    o32_service_free_string(s2);
    return result;
}

static uint32 bridge32_DdeAbandonTransaction(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)DdeAbandonTransaction((ULONG)v0, (ULONG)v1, (ULONG)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DdeAccessData(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 4, 2, TRUE, &b1) && valid;
    if (valid)
        result = (ULONG)DdeAccessData((ULONG)v0, b1.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_DdeAddData(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, v2, 1, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)DdeAddData((ULONG)v0, b1.host, (ULONG)v2, (ULONG)v3);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_DdeClientTransaction(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    ULONG v5 = lx_guest_load_u32(a + 20);
    ULONG v6 = lx_guest_load_u32(a + 24);
    ULONG v7 = lx_guest_load_u32(a + 28);
    LxGuestBorrow b7 = {0};
    valid = o32_service_span(v7, 4, 2, TRUE, &b7) && valid;
    if (valid)
        result = (ULONG)DdeClientTransaction((ULONG)v0, (ULONG)v1, (ULONG)v2, (ULONG)v3, (ULONG)v4,
                                             (ULONG)v5, (ULONG)v6, b7.host);
    else
        open32SetError(87);
    lx_execution_release(&b7);
    return result;
}

static uint32 bridge32_DdeConnect(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b3 = {0};
    valid = o32_service_span(v3, 40, 1, TRUE, &b3) && valid;
    if (valid)
        result = (ULONG)DdeConnect((ULONG)v0, (ULONG)v1, (ULONG)v2, b3.host);
    else
        open32SetError(87);
    lx_execution_release(&b3);
    return result;
}

static uint32 bridge32_DdeCreateDataHandle(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    ULONG v5 = lx_guest_load_u32(a + 20);
    ULONG v6 = lx_guest_load_u32(a + 24);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, v2, 1, TRUE, &b1) && valid;
    if (valid)
        result = (ULONG)DdeCreateDataHandle((ULONG)v0, b1.host, (ULONG)v2, (ULONG)v3, (ULONG)v4,
                                            (ULONG)v5, (ULONG)v6);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_DdeDisconnect(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)DdeDisconnect((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DdeFreeDataHandle(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)DdeFreeDataHandle((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DdeGetData(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, v2, 2, TRUE, &b1) && valid;
    if (valid)
        result = (ULONG)DdeGetData((ULONG)v0, b1.host, (ULONG)v2, (ULONG)v3);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_DdePostAdvise(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)DdePostAdvise((ULONG)v0, (ULONG)v1, (ULONG)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DdeQueryConvInfo(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 100, 3, FALSE, &b2) && valid;
    if (valid)
        result = (ULONG)DdeQueryConvInfo((ULONG)v0, (ULONG)v1, b2.host);
    else
        open32SetError(87);
    lx_execution_release(&b2);
    return result;
}

static uint32 bridge32_DdeSetUserHandle(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)DdeSetUserHandle((ULONG)v0, (ULONG)v1, (ULONG)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DdeUnaccessData(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)DdeUnaccessData((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DefDlgProc(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    if (valid)
        result = (ULONG)DefDlgProc((ULONG)v0, (ULONG)v1, (ULONG)v2, (ULONG)v3);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DeferWindowPos(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    ULONG v5 = lx_guest_load_u32(a + 20);
    ULONG v6 = lx_guest_load_u32(a + 24);
    ULONG v7 = lx_guest_load_u32(a + 28);
    if (valid)
        result = (ULONG)DeferWindowPos((ULONG)v0, (ULONG)v1, (ULONG)v2, (LONG)v3, (LONG)v4,
                                       (LONG)v5, (LONG)v6, (ULONG)v7);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DeleteAtom(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)DeleteAtom((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DeleteFile(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    if (valid)
        result = (ULONG)DeleteFile(s0);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_DlgDirSelectEx(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, v2, 2, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)DlgDirSelectEx((ULONG)v0, b1.host, (LONG)v2, (LONG)v3);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_DosDateTimeToFileTime(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 8, 2, FALSE, &b2) && valid;
    if (valid)
        result = (ULONG)DosDateTimeToFileTime((USHORT)v0, (USHORT)v1, b2.host);
    else
        open32SetError(87);
    lx_execution_release(&b2);
    return result;
}

static uint32 bridge32_DragAcceptFiles(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
    {
        DragAcceptFiles((ULONG)v0, (BOOL)v1);
        result = 0;
    }
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DragFinish(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
    {
        DragFinish((ULONG)v0);
        result = 0;
    }
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_DragQueryFile(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, v3, 2, TRUE, &b2) && valid;
    if (valid)
        result = (ULONG)DragQueryFile((ULONG)v0, (ULONG)v1, b2.host, (ULONG)v3);
    else
        open32SetError(87);
    lx_execution_release(&b2);
    return result;
}

static uint32 bridge32_DragQueryPoint(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 8, 2, TRUE, &b1) && valid;
    if (valid)
        result = (ULONG)DragQueryPoint((ULONG)v0, b1.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_EndDeferWindowPos(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)EndDeferWindowPos((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_EnumThreadWindows(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)EnumThreadWindows((ULONG)v0, (ULONG)v1, (ULONG)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_EnumWindows(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)EnumWindows((ULONG)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_FileTimeToDosDateTime(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 8, 1, FALSE, &b0) && valid;
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 2, 2, FALSE, &b1) && valid;
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 2, 2, FALSE, &b2) && valid;
    if (valid)
        result = (ULONG)FileTimeToDosDateTime(b0.host, b1.host, b2.host);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    lx_execution_release(&b1);
    lx_execution_release(&b2);
    return result;
}

static uint32 bridge32_FindAtom(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, TRUE, &s0) && valid;
    if (valid)
        result = (ULONG)FindAtom(s0);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_FindWindow(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, TRUE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    if (valid)
        result = (ULONG)FindWindow(s0, s1);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_GetCursor(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    (void)a;
    if (valid)
        result = (ULONG)GetCursor();
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetDiskFreeSpace(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 4, 2, TRUE, &b1) && valid;
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 4, 2, TRUE, &b2) && valid;
    LxGuestBorrow b3 = {0};
    valid = o32_service_span(v3, 4, 2, TRUE, &b3) && valid;
    LxGuestBorrow b4 = {0};
    valid = o32_service_span(v4, 4, 2, TRUE, &b4) && valid;
    if (valid)
        result = (ULONG)GetDiskFreeSpace(s0, b1.host, b2.host, b3.host, b4.host);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    lx_execution_release(&b1);
    lx_execution_release(&b2);
    lx_execution_release(&b3);
    lx_execution_release(&b4);
    return result;
}

static uint32 bridge32_GetDlgCtrlID(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)GetDlgCtrlID((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetDlgItemInt(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 4, 2, TRUE, &b2) && valid;
    if (valid)
        result = (ULONG)GetDlgItemInt((ULONG)v0, (LONG)v1, b2.host, (BOOL)v3);
    else
        open32SetError(87);
    lx_execution_release(&b2);
    return result;
}

static uint32 bridge32_GetDoubleClickTime(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    (void)a;
    if (valid)
        result = (ULONG)GetDoubleClickTime();
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetEnvironmentStrings(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    (void)a;
    if (valid)
        result = (ULONG)GetEnvironmentStrings();
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetEnvironmentVariable(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, v2, 2, TRUE, &b1) && valid;
    if (valid)
        result = (ULONG)GetEnvironmentVariable(s0, b1.host, (ULONG)v2);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_GetFileTime(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 8, 2, TRUE, &b1) && valid;
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 8, 2, TRUE, &b2) && valid;
    LxGuestBorrow b3 = {0};
    valid = o32_service_span(v3, 8, 2, TRUE, &b3) && valid;
    if (valid)
        result = (ULONG)GetFileTime((ULONG)v0, b1.host, b2.host, b3.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    lx_execution_release(&b2);
    lx_execution_release(&b3);
    return result;
}

static uint32 bridge32_GetFileTitle(uint8 *a)
{
    ULONG result = UINT32_MAX;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, (USHORT)v2, 2, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)GetFileTitle(s0, b1.host, (USHORT)v2);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_GetForegroundWindow(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    (void)a;
    if (valid)
        result = (ULONG)GetForegroundWindow();
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetKeyNameText(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, v2, 2, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)GetKeyNameText((ULONG)v0, b1.host, (LONG)v2);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_GetLastActivePopup(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)GetLastActivePopup((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetNextDlgTabItem(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)GetNextDlgTabItem((ULONG)v0, (ULONG)v1, (BOOL)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetNextWindow(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)GetNextWindow((ULONG)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetPriorityClipboardFormat(uint8 *a)
{
    ULONG result = UINT32_MAX;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, (uint64_t)v1 * 4, 1, FALSE, &b0) && valid;
    if (valid)
        result = (ULONG)GetPriorityClipboardFormat(b0.host, (LONG)v1);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_GetPrivateProfileInt(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    PSZ s3 = NULL;
    valid = o32_service_string(v3, FALSE, &s3) && valid;
    if (valid)
        result = (ULONG)GetPrivateProfileInt(s0, s1, (LONG)v2, s3);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    o32_service_free_string(s3);
    return result;
}

static uint32 bridge32_GetSystemDirectory(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, v1, 2, TRUE, &b0) && valid;
    if (valid)
        result = (ULONG)GetSystemDirectory(b0.host, (ULONG)v1);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_GetTempFileName(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    LxGuestBorrow b3 = {0};
    valid = o32_service_span(v3, 260, 2, FALSE, &b3) && valid;
    if (valid)
        result = (ULONG)GetTempFileName(s0, s1, (ULONG)v2, b3.host);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    lx_execution_release(&b3);
    return result;
}

static uint32 bridge32_GetTempPath(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, v0, 2, TRUE, &b1) && valid;
    if (valid)
        result = (ULONG)GetTempPath((ULONG)v0, b1.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_GetTimeZoneInformation(uint8 *a)
{
    ULONG result = UINT32_MAX;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 172, 2, FALSE, &b0) && valid;
    if (valid)
        result = (ULONG)GetTimeZoneInformation(b0.host);
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_GetTopWindow(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)GetTopWindow((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetUpdateRgn(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)GetUpdateRgn((ULONG)v0, (ULONG)v1, (BOOL)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetWindowWord(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)GetWindowWord((ULONG)v0, (LONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GlobalMemoryStatus(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    LxGuestBorrow b0 = {0};
    valid = o32_service_span(v0, 32, 2, FALSE, &b0) && valid;
    if (valid)
    {
        GlobalMemoryStatus(b0.host);
        result = 0;
    }
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32_IsBadHugeReadPtr(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)IsBadHugeReadPtr((PVOID)(uintptr_t)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_IsBadHugeWritePtr(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)IsBadHugeWritePtr((PVOID)(uintptr_t)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_IsBadStringPtr(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)IsBadStringPtr((ULONG)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_IsCharAlpha(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)IsCharAlpha((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_IsCharAlphaNumeric(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)IsCharAlphaNumeric((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_IsCharUpper(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)IsCharUpper((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_IsChild(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)IsChild((ULONG)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_IsDialogMessage(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 28, 3, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)IsDialogMessage((ULONG)v0, b1.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_LockWindowUpdate(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)LockWindowUpdate((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_MapDialogRect(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 16, 3, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)MapDialogRect((ULONG)v0, b1.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_MapWindowPoints(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, (uint64_t)v3 * 8, 3, FALSE, &b2) && valid;
    if (valid)
        result = (ULONG)MapWindowPoints((ULONG)v0, (ULONG)v1, b2.host, (ULONG)v3);
    else
        open32SetError(87);
    lx_execution_release(&b2);
    return result;
}

static uint32 bridge32_MessageBox(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    PSZ s2 = NULL;
    valid = o32_service_string(v2, FALSE, &s2) && valid;
    if (valid)
        result = (ULONG)MessageBox((ULONG)v0, s1, s2, (ULONG)v3);
    else
        open32SetError(87);
    o32_service_free_string(s1);
    o32_service_free_string(s2);
    return result;
}

static uint32 bridge32_ModifyMenu(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    PSZ s4 = NULL;
    valid = o32_service_string(v4, FALSE, &s4) && valid;
    if (valid)
        result = (ULONG)ModifyMenu((ULONG)v0, (ULONG)v1, (ULONG)v2, (ULONG)v3, s4);
    else
        open32SetError(87);
    o32_service_free_string(s4);
    return result;
}

static uint32 bridge32_MoveFile(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    if (valid)
        result = (ULONG)MoveFile(s0, s1);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_OutputDebugString(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    if (valid)
    {
        OutputDebugString(s0);
        result = 0;
    }
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_PostThreadMessage(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    if (valid)
        result = (ULONG)PostThreadMessage((ULONG)v0, (ULONG)v1, (ULONG)v2, (ULONG)v3);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_RegQueryInfoKey(uint8 *a)
{
    ULONG result = 87;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    ULONG v5 = lx_guest_load_u32(a + 20);
    ULONG v6 = lx_guest_load_u32(a + 24);
    ULONG v7 = lx_guest_load_u32(a + 28);
    ULONG v8 = lx_guest_load_u32(a + 32);
    ULONG v9 = lx_guest_load_u32(a + 36);
    ULONG v10 = lx_guest_load_u32(a + 40);
    ULONG v11 = lx_guest_load_u32(a + 44);
    LxGuestBorrow b2 = {0};
    ULONG n2 = 0;
    if (v2)
    {
        valid = o32_service_span(v2, 4, 3, FALSE, &b2) && valid;
        if (b2.host)
            n2 = lx_guest_load_u32(b2.host);
    }
    LxGuestBorrow b3 = {0};
    ULONG n3 = 0;
    if (v3)
    {
        valid = o32_service_span(v3, 4, 3, FALSE, &b3) && valid;
        if (b3.host)
            n3 = lx_guest_load_u32(b3.host);
    }
    LxGuestBorrow b4 = {0};
    ULONG n4 = 0;
    if (v4)
    {
        valid = o32_service_span(v4, 4, 3, FALSE, &b4) && valid;
        if (b4.host)
            n4 = lx_guest_load_u32(b4.host);
    }
    LxGuestBorrow b5 = {0};
    ULONG n5 = 0;
    if (v5)
    {
        valid = o32_service_span(v5, 4, 3, FALSE, &b5) && valid;
        if (b5.host)
            n5 = lx_guest_load_u32(b5.host);
    }
    LxGuestBorrow b6 = {0};
    ULONG n6 = 0;
    if (v6)
    {
        valid = o32_service_span(v6, 4, 3, FALSE, &b6) && valid;
        if (b6.host)
            n6 = lx_guest_load_u32(b6.host);
    }
    LxGuestBorrow b7 = {0};
    ULONG n7 = 0;
    if (v7)
    {
        valid = o32_service_span(v7, 4, 3, FALSE, &b7) && valid;
        if (b7.host)
            n7 = lx_guest_load_u32(b7.host);
    }
    LxGuestBorrow b8 = {0};
    ULONG n8 = 0;
    if (v8)
    {
        valid = o32_service_span(v8, 4, 3, FALSE, &b8) && valid;
        if (b8.host)
            n8 = lx_guest_load_u32(b8.host);
    }
    LxGuestBorrow b9 = {0};
    ULONG n9 = 0;
    if (v9)
    {
        valid = o32_service_span(v9, 4, 3, FALSE, &b9) && valid;
        if (b9.host)
            n9 = lx_guest_load_u32(b9.host);
    }
    LxGuestBorrow b10 = {0};
    ULONG n10 = 0;
    if (v10)
    {
        valid = o32_service_span(v10, 4, 3, FALSE, &b10) && valid;
        if (b10.host)
            n10 = lx_guest_load_u32(b10.host);
    }
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, n2, 2, TRUE, &b1) && valid;
    LxGuestBorrow b11 = {0};
    valid = o32_service_span(v11, 8, 2, TRUE, &b11) && valid;
    if (valid)
        result = (ULONG)RegQueryInfoKey((ULONG)v0, b1.host, v2 ? &n2 : NULL, v3 ? &n3 : NULL,
                                        v4 ? &n4 : NULL, v5 ? &n5 : NULL, v6 ? &n6 : NULL,
                                        v7 ? &n7 : NULL, v8 ? &n8 : NULL, v9 ? &n9 : NULL,
                                        v10 ? &n10 : NULL, b11.host);
    else
        open32SetError(87);
    if (b2.host)
        lx_guest_store_u32(b2.host, n2);
    if (b3.host)
        lx_guest_store_u32(b3.host, n3);
    if (b4.host)
        lx_guest_store_u32(b4.host, n4);
    if (b5.host)
        lx_guest_store_u32(b5.host, n5);
    if (b6.host)
        lx_guest_store_u32(b6.host, n6);
    if (b7.host)
        lx_guest_store_u32(b7.host, n7);
    if (b8.host)
        lx_guest_store_u32(b8.host, n8);
    if (b9.host)
        lx_guest_store_u32(b9.host, n9);
    if (b10.host)
        lx_guest_store_u32(b10.host, n10);
    lx_execution_release(&b1);
    lx_execution_release(&b2);
    lx_execution_release(&b3);
    lx_execution_release(&b4);
    lx_execution_release(&b5);
    lx_execution_release(&b6);
    lx_execution_release(&b7);
    lx_execution_release(&b8);
    lx_execution_release(&b9);
    lx_execution_release(&b10);
    lx_execution_release(&b11);
    return result;
}

static uint32 bridge32_RegQueryValue(uint8 *a)
{
    ULONG result = 87;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b3 = {0};
    ULONG n3 = 0;
    if (v3)
    {
        valid = o32_service_span(v3, 4, 3, FALSE, &b3) && valid;
        if (b3.host)
            n3 = lx_guest_load_u32(b3.host);
    }
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, n3, 2, TRUE, &b2) && valid;
    if (valid)
        result = (ULONG)RegQueryValue((ULONG)v0, s1, b2.host, v3 ? &n3 : NULL);
    else
        open32SetError(87);
    if (b3.host)
        lx_guest_store_u32(b3.host, n3);
    o32_service_free_string(s1);
    lx_execution_release(&b2);
    lx_execution_release(&b3);
    return result;
}

static uint32 bridge32_RegSetValue(uint8 *a)
{
    ULONG result = 87;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    LxGuestBorrow b3 = {0};
    valid = o32_service_span(v3, v4, 1, FALSE, &b3) && valid;
    if (valid)
        result = (ULONG)RegSetValue((ULONG)v0, s1, (ULONG)v2, b3.host, (ULONG)v4);
    else
        open32SetError(87);
    o32_service_free_string(s1);
    lx_execution_release(&b3);
    return result;
}

static uint32 bridge32_ReleaseMutex(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)ReleaseMutex((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_RemoveDirectory(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    if (valid)
        result = (ULONG)RemoveDirectory(s0);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_AddFontResource(uint8 *a)
{
    ULONG result = 0;
    PSZ s0 = NULL;
    if (o32_service_string(lx_guest_load_u32(a + 0), FALSE, &s0))
        result = (ULONG)AddFontResource(s0);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_RemoveFontResource(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    if (valid)
        result = (ULONG)RemoveFontResource(s0);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_ReplyMessage(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)ReplyMessage((LONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_ResetEvent(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)ResetEvent((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_SearchPath(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    ULONG v5 = lx_guest_load_u32(a + 20);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    PSZ s2 = NULL;
    valid = o32_service_string(v2, FALSE, &s2) && valid;
    LxGuestBorrow b4 = {0};
    valid = o32_service_span(v4, v3, 2, TRUE, &b4) && valid;
    LxGuestBorrow b5 = {0};
    valid = o32_service_span(v5, 4, 2, TRUE, &b5) && valid;
    if (valid)
        result = (ULONG)SearchPath(s0, s1, s2, (ULONG)v3, b4.host, b5.host);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    o32_service_free_string(s2);
    lx_execution_release(&b4);
    lx_execution_release(&b5);
    return result;
}

static uint32 bridge32_SetDlgItemInt(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    if (valid)
        result = (ULONG)SetDlgItemInt((ULONG)v0, (LONG)v1, (ULONG)v2, (BOOL)v3);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_SetEnvironmentVariable(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    if (valid)
        result = (ULONG)SetEnvironmentVariable(s0, s1);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_SetEvent(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)SetEvent((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_SetFileAttributes(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    if (valid)
        result = (ULONG)SetFileAttributes(s0, (ULONG)v1);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_SetFileTime(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 8, 1, TRUE, &b1) && valid;
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 8, 1, TRUE, &b2) && valid;
    LxGuestBorrow b3 = {0};
    valid = o32_service_span(v3, 8, 1, TRUE, &b3) && valid;
    if (valid)
        result = (ULONG)SetFileTime((ULONG)v0, b1.host, b2.host, b3.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    lx_execution_release(&b2);
    lx_execution_release(&b3);
    return result;
}

static uint32 bridge32_SetHandleCount(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)SetHandleCount((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_SetWindowWord(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    if (valid)
        result = (ULONG)SetWindowWord((ULONG)v0, (LONG)v1, (USHORT)v2);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_ShowCursor(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)ShowCursor((BOOL)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_TrackPopupMenu(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    ULONG v5 = lx_guest_load_u32(a + 20);
    ULONG v6 = lx_guest_load_u32(a + 24);
    LxGuestBorrow b6 = {0};
    valid = o32_service_span(v6, 16, 1, TRUE, &b6) && valid;
    if (valid)
        result = (ULONG)TrackPopupMenu((ULONG)v0, (ULONG)v1, (LONG)v2, (LONG)v3, (LONG)v4,
                                       (ULONG)v5, b6.host);
    else
        open32SetError(87);
    lx_execution_release(&b6);
    return result;
}

static uint32 bridge32_TranslateMDISysAccel(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 28, 1, FALSE, &b1) && valid;
    if (valid)
        result = (ULONG)TranslateMDISysAccel((ULONG)v0, b1.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    return result;
}

static uint32 bridge32_VkKeyScan(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)VkKeyScan((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_WriteProfileString(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    PSZ s2 = NULL;
    valid = o32_service_string(v2, FALSE, &s2) && valid;
    if (valid)
        result = (ULONG)WriteProfileString(s0, s1, s2);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    o32_service_free_string(s2);
    return result;
}

static uint32 bridge32_ZeroMemory(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    LxGuestBorrow b0 = {0};
    valid = !v1 || lx_execution_borrow(GLoaderState.execution, v0, v1, 2, &b0);
    if (valid)
    {
        ZeroMemory(b0.host, (ULONG)v1);
        result = 0;
    }
    else
        open32SetError(87);
    lx_execution_release(&b0);
    return result;
}

static uint32 bridge32__lcreat(uint8 *a)
{
    ULONG result = UINT32_MAX;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    if (valid)
        result = (ULONG)_lcreat(s0, (LONG)v1);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32__lopen(uint8 *a)
{
    ULONG result = UINT32_MAX;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    if (valid)
        result = (ULONG)_lopen(s0, (LONG)v1);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    return result;
}

static uint32 bridge32_MapVirtualKey(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)MapVirtualKey((ULONG)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetMenuContextHelpId(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)GetMenuContextHelpId((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_SetMenuContextHelpId(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    if (valid)
        result = (ULONG)SetMenuContextHelpId((ULONG)v0, (ULONG)v1);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_ExtractIcon(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    if (valid)
        result = (ULONG)ExtractIcon((ULONG)v0, s1, (ULONG)v2);
    else
        open32SetError(87);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_lstrcmp2(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    PSZ s0 = NULL;
    valid = o32_service_string(v0, FALSE, &s0) && valid;
    PSZ s1 = NULL;
    valid = o32_service_string(v1, FALSE, &s1) && valid;
    if (valid)
        result = (ULONG)lstrcmp2(s0, s1);
    else
        open32SetError(87);
    o32_service_free_string(s0);
    o32_service_free_string(s1);
    return result;
}

static uint32 bridge32_GetKeyboardLayout(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    if (valid)
        result = (ULONG)GetKeyboardLayout((ULONG)v0);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_GetStringTypeEx(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    PSZ s2 = NULL;
    LONG effective_count = (LONG)v3;
    if (effective_count == -1)
    {
        valid = o32_service_string(v2, FALSE, &s2) && valid;
        if (s2)
            effective_count = (LONG)strlen(s2) + 1;
    }
    else if (effective_count > 0 && effective_count <= 1024 * 1024)
    {
        s2 = malloc((size_t)effective_count);
        valid = s2 && lx_execution_read(GLoaderState.execution, v2, s2, effective_count) && valid;
    }
    else
        valid = FALSE;
    LxGuestBorrow b4 = {0};
    valid = o32_service_span(v4, (uint64_t)effective_count * 2, 2, FALSE, &b4) && valid;
    if (valid)
        result = (ULONG)GetStringTypeEx((ULONG)v0, (ULONG)v1, s2, (LONG)v3, b4.host);
    else
        open32SetError(87);
    o32_service_free_string(s2);
    lx_execution_release(&b4);
    return result;
}

static uint32 bridge32__CreateDialogIndirectParam(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    if (valid)
        result = (ULONG)_CreateDialogIndirectParam((ULONG)v0, (ULONG)v1, (ULONG)v2, (ULONG)v3,
                                                   (ULONG)v4);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32__DialogBoxIndirect(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    if (valid)
        result = (ULONG)_DialogBoxIndirect((ULONG)v0, (ULONG)v1, (ULONG)v2, (ULONG)v3);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32__DialogBoxIndirectParam(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    ULONG v3 = lx_guest_load_u32(a + 12);
    ULONG v4 = lx_guest_load_u32(a + 16);
    if (valid)
        result =
            (ULONG)_DialogBoxIndirectParam((ULONG)v0, (ULONG)v1, (ULONG)v2, (ULONG)v3, (ULONG)v4);
    else
        open32SetError(87);
    return result;
}

static uint32 bridge32_WinXTranslateMsg(uint8 *a)
{
    ULONG result = 0;
    BOOL valid = TRUE;
    ULONG v0 = lx_guest_load_u32(a + 0);
    ULONG v1 = lx_guest_load_u32(a + 4);
    ULONG v2 = lx_guest_load_u32(a + 8);
    LxGuestBorrow b1 = {0};
    valid = o32_service_span(v1, 32, 1, FALSE, &b1) && valid;
    LxGuestBorrow b2 = {0};
    valid = o32_service_span(v2, 28, 2, FALSE, &b2) && valid;
    if (valid)
        result = (ULONG)WinXTranslateMsg((ULONG)v0, b1.host, b2.host);
    else
        open32SetError(87);
    lx_execution_release(&b1);
    lx_execution_release(&b2);
    return result;
}
