/* Public domain. No rights reserved. Wholly AI-authored. */
/* Opening, creating and saving a ball's manifest. */
#include "ball/ball_internal.h"
#include "os2/config/config_sys.h"

int tb_ball_save_manifest(TbBall *c)
{
    return tb_ball_write_json(c, tb_ball_join(c, c->ball, "ball.json"), c->manifest);
}

int tb_ball_load(TbBall *c, const cJSON *request)
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

int tb_ball_create(TbBall *c, const char *path)
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
    char *config = tb_ball_keep(c, os2_config_sys_default_text());
    if (!config || !tb_ball_save_manifest(c) ||
        !tb_atomic_write(tb_ball_join(c, c->ball, "drive_c/CONFIG.SYS"), config, strlen(config)))
        return tb_ball_fail(c, "Cannot initialize ball");
    return tb_ball_regenerate(c);
}

cJSON *tb_ball_discover(TbBall *c)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    char *home = tb_home();
    if (!home)
    {
        tb_ball_fail(c, errno == ENOMEM ? "Out of memory" : "Home directory is unavailable");
        return NULL;
    }
    home = tb_ball_keep(c, home);
    if (!home)
        return NULL;
    char *base = xdg && *xdg ? tb_ball_keep(c, strdup(xdg)) : tb_ball_join(c, home, ".local/share");
    char *directory = tb_ball_join(c, base, "2ine/ball");
    cJSON *list = cJSON_CreateArray();
    size_t n = 0;
    char **names = tb_names(directory, &n);
    if (n > 1)
        qsort(names, n, sizeof(*names), compare_names);
    for (size_t i = 0; i < n; i++)
    {
        char *path = tb_ball_join(c, directory, names[i]);
        if (tb_is_file(tb_ball_join(c, path, "ball.json")))
            cJSON_AddItemToArray(list, cJSON_CreateString(path));
    }
    tb_names_free(names, n);
    return list;
}
