/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PMGRE_H
#define TWOINE_PMGRE_H
#include "os2types.h"
OS2EXPORT ULONG OS2API Gre32Entry3(ULONG first, ULONG second, ULONG operation) OS2APIINFO(63);
OS2EXPORT ULONG OS2API Gre32Entry5(ULONG first, ULONG second, ULONG third, ULONG fourth,
                                   ULONG operation) OS2APIINFO(65);
#endif
