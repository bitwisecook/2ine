/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

int semaphoreWord(PHSEM16 sem, int exchange, uint32 value, uint32 *previous)
{
    if (!sem)
        return 0;
    if (GProcessState.execution)
    {
        uint32 address;
        return lx_execution_address(GProcessState.execution, sem, 4, 3, &address) &&
               lx_execution_atomic_u32(GProcessState.execution, address, exchange, value, previous);
    }
    grabLock(&GMutexDosCalls);
    *previous = lx_guest_load_u32(sem);
    if (exchange)
        lx_guest_store_u32(sem, value);
    ungrabLock(&GMutexDosCalls);
    return 1;
}

APIRET16 waitSemaphoreWord(PHSEM16 sem, LONG milliseconds, int request)
{
    const uint64 deadline =
        milliseconds < 0 ? UINT64_MAX : lx_host_monotonic_us() + (uint64)milliseconds * 1000;
    for (;;)
    {
        uint32 previous;
        if (!semaphoreWord(sem, request, 1, &previous))
            return ERROR_INVALID_PARAMETER;
        if (!previous)
            return NO_ERROR;
        if (milliseconds == 0 || lx_host_monotonic_us() >= deadline)
            return ERROR_TIMEOUT;
        lx_host_sleep_ms(1);
    }
}

APIRET16 DosSemRequest(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemRequest(%p, %d)", sem, (int)ms);
    return waitSemaphoreWord(sem, ms, 1);
}

APIRET16 DosSemClear(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemClear(%p)", sem);
    uint32 previous;
    return semaphoreWord(sem, 1, 0, &previous) ? NO_ERROR : ERROR_INVALID_PARAMETER;
}

APIRET16 DosSemWait(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemWait(%p, %d)", sem, (int)ms);
    return waitSemaphoreWord(sem, ms, 0);
}

APIRET16 DosSemSet(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemSet(%p)", sem);
    uint32 previous;
    return semaphoreWord(sem, 1, 1, &previous) ? NO_ERROR : ERROR_INVALID_PARAMETER;
}

/* DOSFSRSEM fields. pid and tid form one owner dword. The kernel-private sem word
   doubles as this structure's lock while no original code can observe it. */
enum
{
    FSRS_OWNER = 2,
    FSRS_TID = 4,
    FSRS_USAGE = 6,
    FSRS_CLIENT = 8,
    FSRS_SEM = 10
};

#define FSRS_LOCKED UINT32_MAX

#define FSRS_WAITERS 0xffu

#define FSRS_KEPT_ON_RELEASE 0xffff0f00u

static int lockFSRamSem(uint8 *sem, uint32 *word)
{
    for (;;)
    {
        if (!semaphoreWord((PHSEM16)(sem + FSRS_SEM), 1, FSRS_LOCKED, word))
            return 0;
        if (*word != FSRS_LOCKED)
            return 1;
        lx_host_sleep_ms(1);
    }
}

static void unlockFSRamSem(uint8 *sem, uint32 word)
{
    uint32 previous;
    semaphoreWord((PHSEM16)(sem + FSRS_SEM), 1, word, &previous);
}

static uint32 fsRamSemPid(void)
{
    /* A zero pid field means free, so a host pid folding to zero needs another value. */
    const uint16 pid = (uint16)GProcessState.pib.process_id;
    return pid ? pid : 1;
}

APIRET16 Dos16FSRamSemRequest(PDOSFSRSEM semaphore, LONG timeout)
{
    TRACE_NATIVE("Dos16FSRamSemRequest(%p, %d)", semaphore, (int)timeout);
    uint8 *sem = semaphore;
    const uint32 pid = fsRamSemPid();
    /* An exit list runs as TID 0xffff so it can recover its own threads' semaphores. */
    const uint32 tid = linfo && (linfo->rfProcStatus & 1) ? 0xffff : (uint16)current_tid();
    const uint32 owner = pid | (tid << 16);
    const uint64 deadline =
        timeout < 0 ? UINT64_MAX : lx_host_monotonic_us() + (uint64)timeout * 1000;
    for (;;)
    {
        uint32 word;
        if (!lockFSRamSem(sem, &word))
            return ERROR_INVALID_PARAMETER;
        const uint32 held = lx_guest_load_u32(sem + FSRS_OWNER);
        if (!(held & 0xffff))
        {
            lx_guest_store_u32(sem + FSRS_OWNER, owner);
            lx_guest_store_u16(sem + FSRS_USAGE, 1);
            unlockFSRamSem(sem, word);
            return NO_ERROR;
        }
        if (held == owner)
        {
            lx_guest_store_u16(sem + FSRS_USAGE, lx_guest_load_u16(sem + FSRS_USAGE) + 1);
            unlockFSRamSem(sem, word);
            return NO_ERROR;
        }
        if ((held & 0xffff) == pid && tid == 0xffff)
        {
            lx_guest_store_u16(sem + FSRS_TID, 0xffff);
            lx_guest_store_u16(sem + FSRS_USAGE, 1);
            unlockFSRamSem(sem, word);
            return ERROR_SEM_OWNER_DIED;
        }
        unlockFSRamSem(sem, word | FSRS_WAITERS);
        if (!timeout || lx_host_monotonic_us() >= deadline)
            return ERROR_SEM_TIMEOUT;
        lx_execution_poll_termination(GProcessState.execution);
        lx_host_sleep_ms(1);
    }
}

APIRET16 Dos16FSRamSemClear(PDOSFSRSEM semaphore)
{
    TRACE_NATIVE("Dos16FSRamSemClear(%p)", semaphore);
    uint8 *sem = semaphore;
    const uint32 owner = fsRamSemPid() | ((uint32)(uint16)current_tid() << 16);
    uint32 word;
    if (!lockFSRamSem(sem, &word))
        return ERROR_INVALID_PARAMETER;
    const uint32 difference = owner ^ lx_guest_load_u32(sem + FSRS_OWNER);
    if (difference && ((difference & 0xffff) || lx_guest_load_u16(sem + FSRS_TID) != 0xffff))
    {
        unlockFSRamSem(sem, word);
        return ERROR_EXCL_SEM_ALREADY_OWNED;
    }
    const uint16 usage = (uint16)(lx_guest_load_u16(sem + FSRS_USAGE) - 1);
    lx_guest_store_u16(sem + FSRS_USAGE, usage);
    if (!usage)
    {
        lx_guest_store_u32(sem + FSRS_OWNER, 0);
        lx_guest_store_u16(sem + FSRS_CLIENT, 0);
        word &= FSRS_KEPT_ON_RELEASE;
    }
    unlockFSRamSem(sem, word);
    return NO_ERROR;
}
