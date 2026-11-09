/* Public domain. No rights reserved. Wholly AI-authored. */
/* Code-page aware string comparison and case mapping. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "os2/include/bsedos.h"
#include "os2/ps/common/pm_text.h"

ULONG WinCompareStrings(HAB hab, ULONG codepage, ULONG country, PSZ first, PSZ second,
                        ULONG reserved)
{
    if (!getAnchorBlock(hab) || !first || !second || reserved)
        return 0;
    COUNTRYCODE code = {country, codepage};
    char weights[256];
    ULONG length;
    if (DosQueryCollate(sizeof weights, &code, weights, &length))
        return 0;
    const unsigned char *a = (const unsigned char *)first, *b = (const unsigned char *)second;
    while (*a && *b && weights[*a] == weights[*b])
    {
        a++;
        b++;
    }
    unsigned left = (unsigned char)weights[*a], right = (unsigned char)weights[*b];
    return left == right ? 1 : left < right ? 2 : 3;
}

static BOOL case_parameters(HAB hab, ULONG codepage, ULONG country)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (codepage != 0 && codepage != 437 && codepage != 850)
    {
        if (anchor)
            lxHostThreadState()->pm_error = PMERR_INVALID_SRC_CODEPAGE;
        return FALSE;
    }
    if (country != 0 && country != 1)
    {
        if (anchor)
            lxHostThreadState()->pm_error = PMERR_PARAMETER_OUT_OF_RANGE;
        return FALSE;
    }
    return TRUE;
}

ULONG WinUpperChar(HAB hab, ULONG codepage, ULONG country, ULONG character)
{
    if (!case_parameters(hab, codepage, country) || character > 255)
        return 0;
    /* Preserve non-ASCII bytes, as does the current NLS DosMapCase provider. */
    return character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character;
}

ULONG WinUpper(HAB hab, ULONG codepage, ULONG country, PSZ text)
{
    TRACE_NATIVE("WinUpper(%u, %u, %u, '%s')", (uint)hab, (uint)codepage, (uint)country, text);
    if (!text || !case_parameters(hab, codepage, country))
        return 0;
    size_t length = strlen(text);
    if (length > UINT32_MAX)
        return 0;
    for (size_t i = 0; i < length; i++)
        if (text[i] >= 'a' && text[i] <= 'z')
            text[i] -= 'a' - 'A';
    return (ULONG)length;
}
