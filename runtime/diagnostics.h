/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_DIAGNOSTICS_H
#define TWOINE_DIAGNOSTICS_H
#include <stdio.h>
#ifndef _STUBBED
#define _STUBBED(x, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        static int seen_this = 0;                                                                  \
        if (!seen_this)                                                                            \
        {                                                                                          \
            seen_this = 1;                                                                         \
            fprintf(stderr, "2INE " what ": %s at %s (%s:%d)\n", x, __FUNCTION__, __FILE__,        \
                    __LINE__);                                                                     \
        }                                                                                          \
    } while (0)
#endif

#if 0
#ifndef STUBBED
#define STUBBED(x) _STUBBED(x, "STUBBED")
#endif
#ifndef FIXME
#define FIXME(x) _STUBBED(x, "FIXME")
#endif
#endif

#ifndef STUBBED
#define STUBBED(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif
#ifndef FIXME
#define FIXME(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

#endif
