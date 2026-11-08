/* Public domain. No rights reserved. Wholly AI-authored. */
/* A launcher and its child share this record while the child runs. It carries
   what a host spawn cannot: the full 32-bit exit code, whether the child's
   message queue has gone idle, and how its first frame should appear. The child
   attaches by the name in LX_LAUNCH_RECORD_VARIABLE; a process without that
   variable has no record. */
#ifndef TWOINE_GUEST_LAUNCH_RECORD_H
#define TWOINE_GUEST_LAUNCH_RECORD_H
#include "host/system.h"
#include "host/shared_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LX_LAUNCH_RECORD_VARIABLE "TWOINE_LAUNCH_RECORD"

enum
{
    LX_LAUNCH_INVISIBLE = 0x00001,
    LX_LAUNCH_MAXIMIZE = 0x00002,
    LX_LAUNCH_MINIMIZE = 0x00004,
    LX_LAUNCH_NO_AUTOCLOSE = 0x00008,
    LX_LAUNCH_SET_POSITION = 0x08000,
    LX_LAUNCH_BACKGROUND = 0x10000,
    LX_LAUNCH_SET_SIZE = 0x20000
};

typedef struct LxLaunchPresentation
{
    uint32_t control;
    int32_t x, y, cx, cy;
    char title[64];
} LxLaunchPresentation;

typedef struct LxLaunchRecord
{
    uint32_t exit_recorded, exit_code, input_idle, presentation_pending;
    LxLaunchPresentation presentation;
} LxLaunchRecord;

/* The tag keeps launchers in different modules from sharing a name sequence. */
static inline LxLaunchRecord *lx_launch_record_create(char tag, LxHostShared **shared, char *name,
                                                      size_t capacity)
{
    static uint32_t sequence;
    uint32_t process, parent;
    lx_host_process_ids(&process, &parent);
    for (unsigned attempt = 0; attempt < 16; attempt++)
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

static inline void lx_launch_record_close(LxHostShared *shared, LxLaunchRecord *record)
{
    if (record)
        lx_host_shared_unmap(record, sizeof *record);
    if (shared)
        lx_host_shared_close(shared);
}

/* The launcher publishes this before the child can start. */
static inline void lx_launch_record_present(LxLaunchRecord *record,
                                            const LxLaunchPresentation *presentation)
{
    record->presentation = *presentation;
    __atomic_store_n(&record->presentation_pending, presentation->control != 0, __ATOMIC_RELEASE);
}

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

static inline int lx_launch_presentation_pending(void)
{
    LxLaunchRecord *record = lx_launch_record();
    return record && __atomic_load_n(&record->presentation_pending, __ATOMIC_ACQUIRE);
}

/* A VIO session keeps its screen after the program ends until the user answers. */
static inline int lx_launch_no_autoclose(void)
{
    LxLaunchRecord *record = lx_launch_record();
    return record && (record->presentation.control & LX_LAUNCH_NO_AUTOCLOSE);
}

/* Only the first caller receives the presentation. */
static inline int lx_launch_take_presentation(LxLaunchPresentation *output)
{
    LxLaunchRecord *record = lx_launch_record();
    if (!record || !__atomic_exchange_n(&record->presentation_pending, 0, __ATOMIC_ACQ_REL))
        return 0;
    *output = record->presentation;
    return 1;
}
#endif
