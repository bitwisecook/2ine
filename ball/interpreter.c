/* Public domain. No rights reserved. Wholly AI-authored. */
/* Locating the original OS/2 command interpreter. */
#include "ball/store.h"
#include "ball/interpreter.h"

char *launch_find_command_interpreter(TbBall *c)
{
    cJSON *env = tb_ball_guest_environment(c, get(c->application, "environment"));
    if (!env)
        return NULL;
    const char *comspec = field(env, "COMSPEC");
    if (comspec && *comspec)
    {
        char *host = tb_ball_host_path(c, comspec, 0);
        cJSON_Delete(env);
        if (host && tb_is_file(host))
            return host;
        tb_ball_fail(c, "COMSPEC must name an accessible original OS/2 command interpreter");
        return NULL;
    }
    cJSON_Delete(env);
    for (int i = 0; i < TB_BALL_DRIVE_COUNT; i++)
    {
        int drive = i == 0 ? 2 : i <= 2 ? i - 1 : i;
        if (!c->roots[drive])
            continue;
        const char *suffixes[] = {"OS2/CMD.EXE", "CMD.EXE", "OS2IMAGE/DISK_2/CMD.EXE",
                                  "OS2IMAGE/DISK_1/CMD.EXE", NULL};
        for (int j = 0; suffixes[j]; j++)
        {
            char *host = tb_ball_case_path(c, tb_ball_join(c, c->roots[drive], suffixes[j]));
            if (host && tb_is_file(host))
                return host;
            if (errno == ENOMEM)
                return NULL;
            free(c->error);
            c->error = NULL;
        }
    }
    return NULL;
}

char *launch_command_interpreter(TbBall *c)
{
    char *host = launch_find_command_interpreter(c);
    if (!host && !c->error)
        tb_ball_fail(
            c,
            "This command file needs the original OS/2 CMD.EXE, which is not installed. Select CMD and REXX Support in OS/2 Components and wait for it to finish installing before retrying this command file.");
    return host;
}
