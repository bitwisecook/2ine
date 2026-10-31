/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "nls.h"

#include "nls-lx.h"

APIRET16 Dos16GetDBCSEv(USHORT buflen, PCOUNTRYCODE16 pcc, PCHAR buf)
{

    TRACE_NATIVE("Dos16GetDBCSEv(%u, %p, %p)", (uint)buflen, pcc, buf);
    if (!pcc)
        return ERROR_INVALID_PARAMETER;
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;
    if (buflen)
        memset(buf, '\0', buflen);
    return NO_ERROR;
}

APIRET16 Dos16CaseMap(USHORT len, PCOUNTRYCODE16 pcc, PCHAR pch)
{
    TRACE_NATIVE("Dos16CaseMap(%u, %p, %p)", (uint)len, pcc, pch);
    if (!pcc)
        return ERROR_INVALID_PARAMETER;
    COUNTRYCODE cc = {pcc->country, pcc->codepage};
    APIRET16 ret = DosMapCase(len, &cc, pch);
    pcc->country = cc.country;
    pcc->codepage = cc.codepage;
    return ret;
}

APIRET16 Dos16GetCtryInfo(USHORT len, PCOUNTRYCODE16 pcc, PCOUNTRYINFO16 pch, PUSHORT dlen)
{
    TRACE_NATIVE("Dos16GetCtryInfo(%u, %p, %p)", len, pcc, pch);
    if (!pcc || !pch || (len < 6))
        return ERROR_INVALID_PARAMETER;
    COUNTRYINFO ci;
    COUNTRYCODE cc = {pcc->country, pcc->codepage};
    APIRET16 ret = DosQueryCtryInfo(sizeof(ci), &cc, &ci, NULL);
    if (ret)
        return ret;
    uint8 wire[38] = {0};
    lx_guest_store_u16(wire, ci.country);
    lx_guest_store_u16(wire + 2, ci.codepage);
    lx_guest_store_u16(wire + 4, ci.fsDateFmt);
    memcpy(wire + 6, ci.szCurrency, 32);
    memcpy(pch, wire, len < sizeof wire ? len : sizeof wire);
    if (dlen)
        *dlen = sizeof wire;
    return NO_ERROR;
}

APIRET DosQueryDBCSEnv(ULONG buflen, PCOUNTRYCODE pcc, PCHAR buf)
{

    TRACE_NATIVE("DosQueryDBCSEnv(%u, %p, %p)", (uint)buflen, pcc, buf);
    if (!pcc)
        return ERROR_INVALID_PARAMETER;
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;
    if (buflen)
        memset(buf, '\0', buflen);
    return NO_ERROR;
}

APIRET DosMapCase(ULONG cb, PCOUNTRYCODE pcc, PCHAR pch)
{

    TRACE_NATIVE("DosMapCase(%u, %p, %p)", (uint)cb, pcc, pch);
    if (!pcc)
        return ERROR_INVALID_PARAMETER;
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;

    for (ULONG i = 0; i < cb; i++)
    {
        const CHAR ch = *pch;
        *(pch++) = ((ch >= 'A') && (ch <= 'Z')) ? ch - ('A' - 'a') : ch;
    }

    return NO_ERROR;
}

APIRET DosQueryCtryInfo(ULONG cb, PCOUNTRYCODE pcc, PCOUNTRYINFO pci, PULONG pcbActual)
{
    /* Short country-info buffers receive the available prefix. */
    COUNTRYINFO ci;
    memset(&ci, '\0', sizeof(ci));

    /* Zero bytes beyond the available country information. */
    memset(pci, '\0', cb);

    if (!pcc)
        return ERROR_INVALID_PARAMETER;
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;

    ci.country = 1;
    ci.codepage = 437;
    ci.fsDateFmt = 0;
    ci.szCurrency[0] = '$';
    ci.szThousandsSeparator[0] = ',';
    ci.szDecimal[0] = '.';
    ci.szDateSeparator[0] = '-';
    ci.szTimeSeparator[0] = ':';
    ci.fsCurrencyFmt = 0;
    ;
    ci.cDecimalPlace = 2;
    ci.fsTimeFmt = 0;

    ci.szDataSeparator[0] = ',';

    if (pcbActual)
        *pcbActual = 38; // OS/2 Warp 4.52 doesn't count the abReserved[5] at the end.

    memcpy(pci, &ci, cb);

    return NO_ERROR;
}
