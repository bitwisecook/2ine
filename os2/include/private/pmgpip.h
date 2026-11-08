/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Private GPI interfaces shared by 2ine's DLLs. */

#ifndef _INCL_PMGPIP_H_
#define _INCL_PMGPIP_H_

#include "os2/include/pmgpi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Metafiles */

    /* A native metafile object holding a copy of the given bytes. */
    OS2EXPORT ULONG lx_pm_metafile_create(const void *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
