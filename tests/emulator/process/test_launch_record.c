/* Public domain. No rights reserved. Wholly AI-authored. */
/* The launcher creates a launch record; the child (here, the same process
   through LX_LAUNCH_RECORD_VARIABLE) attaches once and reports through it. */
#define _POSIX_C_SOURCE 200809L
#include "emulator/launch_record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    LxHostShared *shared = NULL, *second_shared = NULL;
    char name[64], second[64];
    LxLaunchRecord *launcher = lx_launch_record_create('t', &shared, name, sizeof name);
    CHECK(launcher && shared);
    LxLaunchRecord *other = lx_launch_record_create('t', &second_shared, second, sizeof second);
    CHECK(other && strcmp(name, second));
    lx_launch_record_close(second_shared, other);

    LxLaunchPresentation presentation = {
        LX_LAUNCH_NO_AUTOCLOSE | LX_LAUNCH_SET_POSITION, 10, 20, 0, 0, "title"};
    lx_launch_record_present(launcher, &presentation);
    CHECK(setenv(LX_LAUNCH_RECORD_VARIABLE, name, 1) == 0);

    LxLaunchRecord *child = lx_launch_record();
    CHECK(child && child != launcher);
    CHECK(lx_launch_record() == child);
    CHECK(lx_launch_presentation_pending());
    CHECK(lx_launch_no_autoclose());
    LxLaunchPresentation taken;
    CHECK(lx_launch_take_presentation(&taken));
    CHECK(taken.x == 10 && taken.y == 20 && !strcmp(taken.title, "title"));
    CHECK(!lx_launch_take_presentation(&taken));
    CHECK(!lx_launch_presentation_pending());

    lx_launch_mark_input_idle();
    lx_launch_record_exit(0x12345678u);
    CHECK(launcher->input_idle == 1);
    CHECK(launcher->exit_recorded == 1 && launcher->exit_code == 0x12345678u);
    lx_launch_record_close(shared, launcher);
    puts("launch record ok");
    return 0;
}
