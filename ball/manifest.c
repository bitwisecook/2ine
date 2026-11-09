/* Public domain. No rights reserved. Wholly AI-authored. */
/* Opening, creating and saving a ball's manifest. */
#include "ball/ball_internal.h"
#include "ball/drives.h"
#include "ball/guest_environment.h"
#include "ball/manifest.h"
#include "ball/runtime_config.h"

int tb_ball_save_manifest(Context *c)
{
    return tb_ball_write_json(c, tb_ball_join(c, c->ball, "ball.json"), c->manifest);
}

int tb_ball_load(Context *c, const cJSON *request)
{
    const char *path = field(request, "ball");
    if (!path)
        return tb_ball_fail(c, "A ball path is required");
    c->ball = tb_ball_resolve_path(c, path, 0);
    if (!c->ball)
        return 0;
    const cJSON *provided = get(request, "manifest");
    if (provided)
        c->manifest = cJSON_Duplicate(provided, 1);
    else
    {
        char *text = tb_ball_read_file(c, tb_ball_join(c, c->ball, "ball.json"), NULL, 0);
        if (!text)
            return 0;
        c->manifest = cJSON_ParseWithOpts(text, NULL, 1);
    }
    if (!cJSON_IsObject(c->manifest) || !cJSON_IsNumber(get(c->manifest, "version")) ||
        get(c->manifest, "version")->valuedouble != 1)
        return tb_ball_fail(c, "Unsupported or invalid ball.json");
    const char *action = field(request, "action");
    c->readonly_drives =
        action &&
        (!strcmp(action, "launch-prerequisites") || !strcmp(action, "ensure-launch-dependencies") ||
         !strcmp(action, "launch-dependency-status") || !strcmp(action, "ball-navigation-state") ||
         !strcmp(action, "os2-component-state") || !strcmp(action, "validate-os2-media"));
    return tb_ball_roots(c);
}

int tb_ball_create(Context *c, const char *path)
{
    if (!path)
        return tb_ball_fail(c, "A ball path is required");
    c->ball = tb_ball_resolve_path(c, path, 0);
    if (!c->ball || !tb_mkdirs(c->ball))
        return tb_ball_fail(c, "Cannot create ball directory");
    char *manifest = tb_ball_join(c, c->ball, "ball.json");
    if (tb_is_file(manifest))
    {
        char *s = tb_ball_read_file(c, manifest, NULL, 0);
        c->manifest = s ? cJSON_ParseWithOpts(s, NULL, 1) : NULL;
        if (!cJSON_IsObject(c->manifest) || !cJSON_IsNumber(get(c->manifest, "version")) ||
            get(c->manifest, "version")->valuedouble != 1)
            return tb_ball_fail(c, "Unsupported or invalid ball.json");
        return tb_ball_roots(c);
    }
    size_t n = 0;
    char **names = tb_names(c->ball, &n);
    tb_names_free(names, n);
    if (n)
        return tb_ball_fail(c, "A new ball must be an empty directory");
    const char *dirs[] = {
        "drive_c/OS2/DLL", "drive_c/OS2/SYSTEM", "drive_c/TEMP", "logs", "ipc", NULL};
    for (int i = 0; dirs[i]; i++)
        if (!tb_mkdirs(tb_ball_join(c, c->ball, dirs[i])))
            return tb_ball_fail(c, "Cannot create ball's private directories");
    c->manifest = cJSON_CreateObject();
    cJSON_AddNumberToObject(c->manifest, "version", 1);
    cJSON *drives = cJSON_AddObjectToObject(c->manifest, "drives");
    cJSON_AddStringToObject(drives, "C", "drive_c");
    const char *groups[] = {"environment", "system", "runtime", "applications", NULL};
    for (int i = 0; groups[i]; i++)
        cJSON_AddObjectToObject(c->manifest, groups[i]);
    char *config = tb_ball_keep(c, strdup(""));
    for (int i = 0; os2_default_environment_names[i]; i++)
        config = tb_ball_format(c, "%s%s%s=%s\r\n", config,
                                !strcmp(os2_default_environment_names[i], "LIBPATH") ? "" : "SET ",
                                !strcmp(os2_default_environment_names[i], "OS2PATH")
                                    ? "PATH"
                                    : os2_default_environment_names[i],
                                os2_default_environment_values[i]);
    if (!config || !tb_ball_save_manifest(c) ||
        !tb_atomic_write(tb_ball_join(c, c->ball, "drive_c/CONFIG.SYS"), config, strlen(config)))
        return tb_ball_fail(c, "Cannot initialize ball");
    return launch_write_runtime_config(c);
}
