/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Live OS/2 processes that share this ball, IPC scope and C: mapping register
   a named host shared object, so a peer can tell whether a PID is alive. */
#include "emulator/process/internal.h"
#include "host/shared_memory.h"

/* 64-bit FNV-1a over the scope strings, each followed by a separator byte. */
#define PEER_HASH_OFFSET_BASIS UINT64_C(1469598103934665603)
#define PEER_HASH_PRIME UINT64_C(1099511628211)
#define PEER_SCOPE_SEPARATOR 0xff
#define PEER_SCOPE_DRIVE_C 2

static LxHostShared *ProcessPeerLease;

static void processPeerName(uint32 pid, char name[64])
{
    uint64 hash = PEER_HASH_OFFSET_BASIS;
    const char *scope[] = {getenv("TWOINE_BALL"), getenv("TWOINE_IPC_DIR"),
                           GProcessState.disks[PEER_SCOPE_DRIVE_C]};
    for (size_t i = 0; i < sizeof scope / sizeof scope[0]; i++)
    {
        for (const char *p = scope[i]; p && *p; p++)
            hash = (hash ^ (unsigned char)*p) * PEER_HASH_PRIME;
        hash = (hash ^ PEER_SCOPE_SEPARATOR) * PEER_HASH_PRIME;
    }
    snprintf(name, 64, "/2ine-peer-%016llx-%08x", (unsigned long long)hash, (unsigned)pid);
}

int lxRegisterProcessPeer(void)
{
    if (ProcessPeerLease)
        return 1;
    char name[64];
    processPeerName(GProcessState.pib.process_id, name);
    return lx_host_shared_open(name, 1, &ProcessPeerLease) == LX_HOST_SHARED_OK;
}

int lxIsProcessPeer(uint32 pid)
{
    if (!pid)
        return 0;
    char name[64];
    processPeerName(pid, name);
    return lx_host_shared_live(name) == 1;
}

void lxUnregisterProcessPeer(void)
{
    if (ProcessPeerLease)
    {
        lx_host_shared_close(ProcessPeerLease);
        ProcessPeerLease = NULL;
    }
}
