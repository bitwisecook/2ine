/* NLS wire structures have 32-bit fields and explicit byte order. */
static uint32 bridge32_DosQueryDBCSEnv(uint8 *arguments)
{
    const uint32 size = lx_guest_load_u32(arguments), cc_pointer = lx_guest_load_u32(arguments + 4);
    const uint32 pointer = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow codepage = {0}, buffer = {0};
    if (!lx_execution_borrow(GLoaderState.execution, cc_pointer, 8, LX_GUEST_READ, &codepage) ||
        (size && !lx_execution_borrow(GLoaderState.execution, pointer, size, LX_GUEST_WRITE, &buffer))) {
        lx_execution_release(&codepage); return ERROR_INVALID_PARAMETER;
    }
    COUNTRYCODE cc = {lx_guest_load_u32(codepage.host), lx_guest_load_u32((uint8 *) codepage.host + 4)};
    const uint32 result = DosQueryDBCSEnv(size, &cc, buffer.host);
    lx_execution_release(&buffer); lx_execution_release(&codepage); return result;
}
static uint32 bridge32_DosMapCase(uint8 *arguments)
{
    const uint32 size = lx_guest_load_u32(arguments), cc_pointer = lx_guest_load_u32(arguments + 4);
    const uint32 pointer = lx_guest_load_u32(arguments + 8);
    LxGuestBorrow codepage = {0}, buffer = {0};
    if (!lx_execution_borrow(GLoaderState.execution, cc_pointer, 8, LX_GUEST_READ, &codepage) ||
        (size && !lx_execution_borrow(GLoaderState.execution, pointer, size, 3, &buffer))) {
        lx_execution_release(&codepage); return ERROR_INVALID_PARAMETER;
    }
    COUNTRYCODE cc = {lx_guest_load_u32(codepage.host), lx_guest_load_u32((uint8 *) codepage.host + 4)};
    const uint32 result = DosMapCase(size, &cc, buffer.host);
    lx_execution_release(&buffer); lx_execution_release(&codepage); return result;
}
static uint32 bridge32_DosQueryCtryInfo(uint8 *arguments)
{
    const uint32 size = lx_guest_load_u32(arguments), cc_pointer = lx_guest_load_u32(arguments + 4);
    const uint32 pointer = lx_guest_load_u32(arguments + 8), actual_pointer = lx_guest_load_u32(arguments + 12);
    LxGuestBorrow codepage = {0}, buffer = {0}, actual = {0};
    if (!lx_execution_borrow(GLoaderState.execution, cc_pointer, 8, LX_GUEST_READ, &codepage) ||
        (size && !lx_execution_borrow(GLoaderState.execution, pointer, size, LX_GUEST_WRITE, &buffer)) ||
        (actual_pointer && !lx_execution_borrow(GLoaderState.execution, actual_pointer, 4, LX_GUEST_WRITE, &actual))) {
        lx_execution_release(&buffer); lx_execution_release(&codepage); return ERROR_INVALID_PARAMETER;
    }
    COUNTRYCODE cc = {lx_guest_load_u32(codepage.host), lx_guest_load_u32((uint8 *) codepage.host + 4)};
    COUNTRYINFO info;
    const uint32 result = DosQueryCtryInfo(sizeof info, &cc, &info, NULL);
    if (size) memset(buffer.host, 0, size);
    if (!result) {
        uint8 wire[44] = {0};
        lx_guest_store_u32(wire, info.country); lx_guest_store_u32(wire + 4, info.codepage);
        lx_guest_store_u32(wire + 8, info.fsDateFmt);
        memcpy(wire + 12, info.szCurrency, 5); memcpy(wire + 17, info.szThousandsSeparator, 2);
        memcpy(wire + 19, info.szDecimal, 2); memcpy(wire + 21, info.szDateSeparator, 2);
        memcpy(wire + 23, info.szTimeSeparator, 2);
        wire[25] = info.fsCurrencyFmt; wire[26] = info.cDecimalPlace; wire[27] = info.fsTimeFmt;
        for (unsigned i = 0; i < 2; i++) lx_guest_store_u16(wire + 28 + i * 2, info.abReserved1[i]);
        memcpy(wire + 32, info.szDataSeparator, 2);
        for (unsigned i = 0; i < 5; i++) lx_guest_store_u16(wire + 34 + i * 2, info.abReserved2[i]);
        const uint32 count = size < sizeof wire ? size : sizeof wire;
        if (count) memcpy(buffer.host, wire, count);
        if (actual.host) lx_guest_store_u32(actual.host, count);
    }
    lx_execution_release(&actual); lx_execution_release(&buffer); lx_execution_release(&codepage);
    return result;
}
