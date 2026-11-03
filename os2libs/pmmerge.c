/* Public domain. No rights reserved. Wholly AI-authored. */
/* PMMERGE bidirectional code-page classification delegates queue ownership to PMWIN. */
#include "os2native.h"
#include "pmmerge.h"
#include "os2libs/generated/pmmerge-lx.h"
ULONG WinQueryCpType(HMQ queue)
{
    ULONG codepage = queue ? WinQueryCp(queue) : GLoaderState.process_codepage;
    if (!codepage && queue)
        return 0;
    switch (codepage)
    {
        case 864:
        case 1046:
            return 2;
        case 775:
        case 921:
            return 3;
        case 855:
        case 866:
        case 915:
            return 4;
        case 737:
        case 869:
        case 813:
            return 5;
        case 856:
        case 862:
        case 916:
            return 6;
        case 932:
        case 942:
        case 943:
            return 7;
        case 949:
        case 970:
            return 8;
        case 852:
        case 912:
            return 9;
        case 936:
        case 1381:
        case 1386:
            return 10;
        case 950:
        case 964:
            return 11;
        case 874:
            return 12;
        case 857:
        case 920:
            return 13;
        case 1200:
        case 1208:
            return 14;
        default:
            return 1;
    }
}
