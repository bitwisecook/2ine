/* Public domain. No rights reserved. Wholly AI-authored. */
/* A launcher and its child share this record while the child runs. It carries
   what a host spawn cannot: the full 32-bit exit code, whether the child's
   message queue has gone idle, and how its first frame should appear. The child
   attaches by the name in LX_LAUNCH_RECORD_VARIABLE; a process without that
   variable has no record. */
#ifndef TWOINE_EMULATOR_LAUNCH_RECORD_H
#define TWOINE_EMULATOR_LAUNCH_RECORD_H
#include "host/shared_memory.h"
#include <stddef.h>
#include <stdint.h>
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

/* Create a uniquely named record for a child. The tag keeps launchers in
   different modules from sharing a name sequence. */
LxLaunchRecord *lx_launch_record_create(char tag, LxHostShared **shared, char *name,
                                        size_t capacity);
void lx_launch_record_close(LxHostShared *shared, LxLaunchRecord *record);
/* The launcher publishes this before the child can start. */
void lx_launch_record_present(LxLaunchRecord *record, const LxLaunchPresentation *presentation);
/* The child's view of its record, attached on first use and kept for the life of
   the process; NULL without a record. A caller racing the first attachment sees
   NULL once, so repeatable marks must retry. */
LxLaunchRecord *lx_launch_record(void);
void lx_launch_mark_input_idle(void);
void lx_launch_record_exit(uint32_t code);
int lx_launch_presentation_pending(void);
/* A VIO session keeps its screen after the program ends until the user answers. */
int lx_launch_no_autoclose(void);
/* Only the first caller receives the presentation. */
int lx_launch_take_presentation(LxLaunchPresentation *output);
#endif
