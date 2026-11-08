/* Public domain. No rights reserved. Wholly AI-authored. */
/* FILEDLG keeps guest addresses, including its custom callback and user pointer. */
static uint32 bridge32_WinFileDlg(uint8 *a)
{
    return WinFileDlg(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                      (PVOID)(uintptr_t)lx_guest_load_u32(a + 8));
}

static uint32 bridge32_WinDefFileDlgProc(uint8 *a)
{
    return (uint32)(uintptr_t)WinDefFileDlgProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                                (MPARAM)(uintptr_t)lx_guest_load_u32(a + 8),
                                                (MPARAM)(uintptr_t)lx_guest_load_u32(a + 12));
}

static uint32 bridge32_WinFreeFileDlgList(uint8 *a)
{
    return WinFreeFileDlgList((PVOID)(uintptr_t)lx_guest_load_u32(a));
}

static uint32 bridge32_WinFontDlg(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)WinFontDlg((HWND)lx_guest_load_u32(a + 0),
                                           (HWND)lx_guest_load_u32(a + 4),
                                           (PVOID)(uintptr_t)lx_guest_load_u32(a + 8));
    return result;
}

static uint32 bridge32_WinDefFontDlgProc(uint8 *a)
{

    uint32 result = 0;
    result = (uint32)(uintptr_t)WinDefFontDlgProc(
        (HWND)lx_guest_load_u32(a + 0), (ULONG)lx_guest_load_u32(a + 4),
        (MPARAM)(uintptr_t)lx_guest_load_u32(a + 8), (MPARAM)(uintptr_t)lx_guest_load_u32(a + 12));
    return result;
}
