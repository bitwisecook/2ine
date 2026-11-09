/* Public domain. No rights reserved. Wholly AI-authored. */
/* Ball settings outside the commands: the working directory and the runtime reference. */
#include "ball/ball_internal.h"

int tb_ball_set_working_directory(TbBall *c, const char *path)
{
    if (!path || strlen(path) < 3 || path[1] != ':' || path[2] != '\\')
    {
        tb_ball_fail(c, "Use an absolute drive-letter directory");
        return 0;
    }
    char key[2] = {path[0], 0};
    cJSON *check = cJSON_CreateObject();
    cJSON_AddStringToObject(check, key, path);
    int valid = tb_ball_validate_directories(c, check);
    cJSON_Delete(check);
    if (!valid)
        return 0;
    cJSON *dirs = object(c->manifest, "workingDirectories");
    if (!dirs)
    {
        tb_ball_fail(c, "Invalid working directories");
        return 0;
    }
    char *host = tb_ball_host_path(c, path, 0);
    put(dirs, key, cJSON_CreateString(tb_ball_guest_for_input(c, host, path)));
    put(c->manifest, "currentDrive", cJSON_CreateString(key));
    return tb_ball_save_manifest(c);
}

int tb_ball_set_runtime_build(TbBall *c, const char *build)
{
    cJSON *runtime = object(c->manifest, "runtime");
    if (!runtime)
    {
        tb_ball_fail(c, "Invalid runtime object");
        return 0;
    }
    if (build)
        put(runtime, "build", cJSON_CreateString(build));
    else
        cJSON_DeleteItemFromObjectCaseSensitive(runtime, "build");
    return tb_ball_save_manifest(c);
}
