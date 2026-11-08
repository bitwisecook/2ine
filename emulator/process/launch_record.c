/* Public domain. No rights reserved. Wholly AI-authored. */
/* The launcher/child shared launch record over named host shared memory. */
#include "emulator/launch_record.h"
#include "host/process.h"
#include <stdio.h>
#include <stdlib.h>

/* Attempts at a fresh record name before giving up. */
#define LAUNCH_RECORD_NAME_ATTEMPTS 16

LxLaunchRecord *lx_launch_record_create(char tag, LxHostShared **shared, char *name,
                                        size_t capacity)
{
    static uint32_t sequence;
    uint32_t process, parent;
    lx_host_process_ids(&process, &parent);
    for (unsigned attempt = 0; attempt < LAUNCH_RECORD_NAME_ATTEMPTS; attempt++)
    {
        snprintf(name, capacity, "/2ine-lr%c-%08x-%08x", tag, (unsigned)process,
                 (unsigned)__atomic_add_fetch(&sequence, 1, __ATOMIC_RELAXED));
        LxHostSharedResult result = lx_host_shared_open(name, sizeof(LxLaunchRecord), shared);
        if (result == LX_HOST_SHARED_OK)
        {
            LxLaunchRecord *record = lx_host_shared_map(*shared, NULL, 0);
            if (!record)
            {
                lx_host_shared_close(*shared);
                *shared = NULL;
            }
            return record;
        }
        if (result != LX_HOST_SHARED_EXISTS)
            return NULL;
    }
    return NULL;
}

void lx_launch_record_close(LxHostShared *shared, LxLaunchRecord *record)
{
    if (record)
        lx_host_shared_unmap(record, sizeof *record);
    if (shared)
        lx_host_shared_close(shared);
}

void lx_launch_record_present(LxLaunchRecord *record, const LxLaunchPresentation *presentation)
{
    record->presentation = *presentation;
    __atomic_store_n(&record->presentation_pending, presentation->control != 0, __ATOMIC_RELEASE);
}

LxLaunchRecord *lx_launch_record(void)
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

void lx_launch_mark_input_idle(void)
{
    LxLaunchRecord *record = lx_launch_record();
    if (record)
        __atomic_store_n(&record->input_idle, 1, __ATOMIC_RELEASE);
}

void lx_launch_record_exit(uint32_t code)
{
    LxLaunchRecord *record = lx_launch_record();
    if (record)
    {
        __atomic_store_n(&record->exit_code, code, __ATOMIC_RELAXED);
        __atomic_store_n(&record->exit_recorded, 1, __ATOMIC_RELEASE);
    }
}

int lx_launch_presentation_pending(void)
{
    LxLaunchRecord *record = lx_launch_record();
    return record && __atomic_load_n(&record->presentation_pending, __ATOMIC_ACQUIRE);
}

int lx_launch_no_autoclose(void)
{
    LxLaunchRecord *record = lx_launch_record();
    return record && (record->presentation.control & LX_LAUNCH_NO_AUTOCLOSE);
}

int lx_launch_take_presentation(LxLaunchPresentation *output)
{
    LxLaunchRecord *record = lx_launch_record();
    if (!record || !__atomic_exchange_n(&record->presentation_pending, 0, __ATOMIC_ACQ_REL))
        return 0;
    *output = record->presentation;
    return 1;
}
