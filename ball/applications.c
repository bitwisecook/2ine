/* Public domain. No rights reserved. Wholly AI-authored. */
/* Saving launcher records and their custom icon assets. */
#include "ball/ball_internal.h"

int tb_ball_save_application(TbBall *c, const cJSON *request)
{
    const char *name = field(request, "name"), *program = field(request, "program"),
               *previous = field(request, "originalName");
    if (!name || !*name || !program || !tb_ball_array_strings(c, get(request, "arguments")))
        return tb_ball_fail(c, "Application name, executable and arguments are required");
    cJSON *apps = object(c->manifest, "applications");
    if (!apps)
        return tb_ball_fail(c, "Invalid applications object");
    if (previous && strcmp(previous, name) && get(apps, name))
        return tb_ball_fail(c, "An application with this name already exists");
    cJSON *existing = get(apps, previous ? previous : name);
    cJSON *app = cJSON_IsObject(existing) ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
    if (!app)
        return tb_ball_fail(c, "Out of memory");
    cJSON *dirs = get(request, "workingDirectories"), *env = get(request, "environment");
    if (!tb_ball_validate_directories(c, dirs))
        goto failed;
    if (dirs)
        put(app, "workingDirectories", cJSON_Duplicate(dirs, 1));
    if (env)
        put(app, "environment", cJSON_Duplicate(env, 1));
    c->application = app;
    char *host = tb_ball_host_path(c, program, 0);
    if (!host || !tb_is_file(host))
    {
        tb_ball_fail(c, "Program does not exist: %s", program);
        goto failed;
    }
    put(app, "program", cJSON_CreateString(tb_ball_guest_for_input(c, host, program)));
    put(app, "arguments", cJSON_Duplicate(get(request, "arguments"), 1));
    const char *cwd = field(request, "cwd");
    if (cwd && *cwd)
    {
        char *host_cwd = tb_ball_host_path(c, cwd, 0);
        if (!host_cwd || !tb_is_dir(host_cwd))
        {
            tb_ball_fail(c, "Working directory does not exist");
            goto failed;
        }
        put(app, "cwd", cJSON_CreateString(tb_ball_guest_for_input(c, host_cwd, cwd)));
    }
    else
        cJSON_DeleteItemFromObjectCaseSensitive(app, "cwd");
    cJSON *checked = tb_ball_guest_environment(c, get(app, "environment"));
    if (!checked)
        goto failed;
    cJSON_Delete(checked);
    if (get(request, "iconSource"))
    {
        cJSON_DeleteItemFromObjectCaseSensitive(app, "wpsIcon");
        const char *source = field(request, "iconSource");
        if (!source || !*source)
            cJSON_DeleteItemFromObjectCaseSensitive(app, "icon");
        else
        {
            if (!tb_is_file(source))
            {
                tb_ball_fail(c, "Choose an accessible icon image");
                goto failed;
            }
            unsigned char hash[TB_BALL_DIGEST_SIZE];
            if (!tb_ball_digest_file(c, source, hash))
                goto failed;
            const char *relative = tb_ball_format(c, "icons/%s.image", tb_ball_hex(c, hash));
            char *target = tb_ball_join(c, c->ball, relative);
            if (!tb_mkdirs(tb_ball_join(c, c->ball, "icons")) ||
                (!tb_is_file(target) && !tb_copy_new(source, target)))
            {
                tb_ball_fail(c, "Cannot import application icon");
                goto failed;
            }
            put(app, "icon", cJSON_CreateString(relative));
        }
    }
    c->application = NULL;
    tb_ball_detach_application(c, app);
    put(apps, name, app);
    if (previous && strcmp(previous, name))
        cJSON_DeleteItemFromObjectCaseSensitive(apps, previous);
    return tb_ball_save_manifest(c);
failed:
    c->application = NULL;
    cJSON_Delete(app);
    return 0;
}
