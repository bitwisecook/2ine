/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef _INCL_NERRNO_H_
#define _INCL_NERRNO_H_

/* OS/2 socket errors: the BSD error number biased by SOCBASEERR. */
enum
{
    SOCBASEERR = 10000,
    SOCEFAULT = SOCBASEERR + 14,
    SOCEINVAL = SOCBASEERR + 22,
    SOCENOPROTOOPT = SOCBASEERR + 42,
    SOCEPROTONOSUPPORT = SOCBASEERR + 43,
    SOCESOCKTNOSUPPORT = SOCBASEERR + 44,
    SOCEAFNOSUPPORT = SOCBASEERR + 47
};
#endif
