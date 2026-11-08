/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_EMULATOR_PROCESS_H
#define TWOINE_EMULATOR_PROCESS_H
#include "formats/executable/types.h"
#include "emulator/execution.h"
#include "emulator/abi.h"

/* The process information block as the host sees it: the fields of the guest
   PIB (LxGuestPib in emulator/abi.h) with the command line and environment as
   host pointers. lx_loader publishes the guest copy. */
typedef struct LxProcessInfo
{
    uint32 process_id;
    uint32 parent_id;
    uint32 module;
    char *command;
    char *environment;
    uint32 status;
    uint32 type;
} LxProcessInfo;

/* Host-only thread metadata. Never serialized into an OS/2 TIB or TLS area. */
typedef struct LxHostThreadState
{
    void *anchor_block;
    void (*pm_cleanup)(void);
    uint32 pm_error;
    uint32 raised_report;
    unsigned raise_depth;
    uint32 critical_depth;
    LxGuestMapping *tib_mapping;
} LxHostThreadState;

LxHostThreadState *lxHostThreadState(void);
void lxHostThreadCleanup(void);
/* Live runtime peers share the configured ball, IPC scope and C: mapping. */
int lxRegisterProcessPeer(void);
int lxIsProcessPeer(uint32 pid);
void lxUnregisterProcessPeer(void);

#endif
