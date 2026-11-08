/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_BSEMEMF_H_
#define _INCL_BSEMEMF_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Memory allocation and permission flags */

    /* OS/2 Toolkit BSEMEMF memory permissions and allocation attributes. */
    enum
    {
        PAG_READ = 0x01,
        PAG_WRITE = 0x02,
        PAG_EXECUTE = 0x04,
        PAG_GUARD = 0x08,
        PAG_COMMIT = 0x10,
        PAG_DECOMMIT = 0x20,
        OBJ_TILE = 0x40,
        OBJ_GETTABLE = 0x100,
        OBJ_GIVEABLE = 0x200,
        PAG_DEFAULT = 0x400
    };

#ifdef __cplusplus
}
#endif

#endif
