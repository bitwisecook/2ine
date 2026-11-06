/* Public domain. No rights reserved. Wholly AI-authored. */
/* A launcher and its child share this record while the child runs. It carries
   the facts a host exit status cannot: the full 32-bit exit code and whether
   the child's message queue has gone idle. The child attaches by the name in
   LX_LAUNCH_RECORD_VARIABLE; a process without that variable has no record. */
#ifndef TWOINE_GUEST_LAUNCH_RECORD_H
#define TWOINE_GUEST_LAUNCH_RECORD_H
#include "hal/host_shared.h"
#include <stdint.h>
#include <stdlib.h>
#define LX_LAUNCH_RECORD_VARIABLE "TWOINE_LAUNCH_RECORD"
typedef struct LxLaunchRecord
{
    uint32_t exit_recorded, exit_code, input_idle;
} LxLaunchRecord;
/* Each module keeps its own view for the life of the process. A caller racing
   the first attachment sees NULL once, so repeatable marks must retry. */
static inline LxLaunchRecord *lx_launch_record(void)
{
    static LxLaunchRecord *record;
    static int attached;
    if (__atomic_exchange_n(&attached, 1, __ATOMIC_ACQ_REL))
        return __atomic_load_n(&record, __ATOMIC_ACQUIRE);
    const char *name = getenv(LX_LAUNCH_RECORD_VARIABLE);
    LxHostShared *shared = NULL;
    if (!name || lx_host_shared_open(name, 0, &shared) != LX_HOST_SHARED_OK)
        return NULL;
    if (lx_host_shared_size(shared) == sizeof(LxLaunchRecord))
        __atomic_store_n(&record, lx_host_shared_map(shared, NULL, 0), __ATOMIC_RELEASE);
    return __atomic_load_n(&record, __ATOMIC_ACQUIRE);
}
static inline void lx_launch_mark_input_idle(void)
{
    LxLaunchRecord *record = lx_launch_record();
    if (record)
        __atomic_store_n(&record->input_idle, 1, __ATOMIC_RELEASE);
}
static inline void lx_launch_record_exit(uint32_t code)
{
    LxLaunchRecord *record = lx_launch_record();
    if (record)
    {
        __atomic_store_n(&record->exit_code, code, __ATOMIC_RELAXED);
        __atomic_store_n(&record->exit_recorded, 1, __ATOMIC_RELEASE);
    }
}
#endif
