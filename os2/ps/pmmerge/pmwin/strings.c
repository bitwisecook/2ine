/* Public domain. No rights reserved. Wholly AI-authored. */
/* Code-page aware string comparison and case mapping. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/include/bsedos.h"

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
