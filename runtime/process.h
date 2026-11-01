/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_PROCESS_H
#define TWOINE_PROCESS_H
#include "formats/types.h"
#include "runtime/execution.h"
#pragma pack(push, 1)
typedef struct LxTIB2
{
    uint32 tib2_ultid;
    uint32 tib2_ulpri;
    uint32 tib2_version;
    uint16 tib2_usMCCount;
    uint16 tib2_fMCForceFlag;
} LxTIB2;

typedef struct LxTIB
{
    uint32 tib_pexchain;
    uint32 tib_pstack;
    uint32 tib_pstacklimit;
    uint32 tib_ptib2;
    uint32 tib_version;
    uint32 tib_ordinal;
} LxTIB;

typedef struct LxPIB
{
    uint32 pib_ulpid;
    uint32 pib_ulppid;
    uint32 pib_hmte;
    char *pib_pchcmd;
    char *pib_pchenv;
    uint32 pib_flstatus;
    uint32 pib_ultype;
} LxPIB;

#pragma pack(pop)

/* Each TIB/TIB2 pair is followed by 32 guest TLS words. */
typedef struct LxPostTIB
{
    uint32 tls[32];

} LxPostTIB;

/* Host-only thread metadata. Never serialized into an OS/2 TIB or TLS area. */
typedef struct LxHostThreadState
{
    void *anchor_block;
    void (*pm_cleanup)(void);
    uint32 pm_error;
    LxGuestMapping *tib_mapping;
} LxHostThreadState;
LxHostThreadState *lxHostThreadState(void);
void lxHostThreadCleanup(void);

#endif
