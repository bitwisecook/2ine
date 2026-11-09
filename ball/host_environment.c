/* Public domain. No rights reserved. Wholly AI-authored. */
/* The host environment for the loader of a launch. */
#include "ball/ball_internal.h"
#include "ball/audio.h"
#include "ball/drives.h"
#include "ball/guest_environment.h"
#include "ball/host_environment.h"
#include "ball/paths.h"
#include "ball/runtime.h"
#include "ball/settings_schema.h"

const char *launch_unset_environment_keys[] = {"IS_2INE",
                                               "TWOINE_DESKTOP_AUXILIARY",
                                               "TWOINE_DESKTOP_SESSION",
                                               "TWOINE_DESKTOP_NAME",
                                               "TWOINE_DESKTOP_ICON",
                                               "TWOINE_DESKTOP_BUNDLE",
                                               "TWOINE_DESKTOP_CACHE",
                                               "TWOINE_DESKTOP_STANDALONE",
                                               "TWOINE_CHILD_ARGUMENTS",
                                               "TWOINE_OPERATION_LEASE_FD",
                                               "TWOINE_OPERATION_OWNER_FD",
                                               "TWOINE_LIBPATH",
                                               "TWOINE_CONFIG",
                                               "TWOINE_BALL",
                                               "TWOINE_USER_PROFILE",
                                               "TWOINE_SYSTEM_PROFILE",
                                               "TWOINE_REGISTRY",
                                               "TWOINE_OBJECTS",
                                               "TWOINE_DISPLAY_DPI",
                                               "TWOINE_DISPLAY_DEPTH",
                                               "TWOINE_IPC_DIR",
                                               "DYLD_LIBRARY_PATH",
                                               NULL};

cJSON *launch_host_environment(Context *c, const char *build, const char *override,
                               const cJSON *app_env)
{
    cJSON *env = tb_ball_guest_environment(c, app_env);
    if (!env)
        return NULL;
    const char *assets[][2] = {{"TWOINE_FONT_DIRECTORY", "fonts"},
                               {"TWOINE_SOUNDFONT", "soundfonts/FluidR3_GM.sf2"},
                               {"TWOINE_KOSMICKRISP_ICD", "vulkan/icd.d/kosmickrisp.json"}};
    for (int i = 0; i < 3; i++)
    {
        char *asset = tb_ball_join(c, build, assets[i][1]);
        if (tb_is_file(asset) || tb_is_dir(asset))
            put(env, assets[i][0], cJSON_CreateString(asset));
    }
    if (!audio_config(c, build, env))
        goto failed;
    const char *backend = override ? override : field(get(c->manifest, "runtime"), "backend");
    const char *inherited = getenv("TWOINE_BACKEND");
    const char *selected = backend ? backend : (inherited && *inherited ? inherited : "default");
    if (strcmp(selected, "native") && strcmp(selected, "unicorn") && strcmp(selected, "default"))
    {
        tb_ball_fail(c, "runtime.backend must be native or unicorn");
        goto failed;
    }
    char *identity = runtime_id(c, build);
    if (!identity)
        goto failed;
    char *runtime = tb_ball_format(c, "%s/OS2/DLL/2INE/%s/%.16s", c->roots[2], selected, identity);
    if (!runtime)
        goto failed;
    char *libpath = tb_ball_format(c, "%s%s%s", runtime, PATH_SEPARATOR, build);
    char *guest_lib = tb_ball_keep(c, strdup(field(env, "LIBPATH") ? field(env, "LIBPATH") : ""));
    if (!guest_lib)
        goto failed;
    for (char *part = guest_lib; part && *part;)
    {
        char *next = strchr(part, ';');
        if (next)
            *next++ = 0;
        if (*part)
        {
            if (isalpha((unsigned char)part[0]) && part[1] == ':' &&
                c->unavailable[toupper((unsigned char)part[0]) - 'A'])
            {
                part = next;
                continue;
            }
            char *path = !strcmp(part, ".") ? part : tb_ball_host_path(c, part, 0);
            if (!path)
                goto failed;
            libpath = tb_ball_format(c, "%s%s%s", libpath, PATH_SEPARATOR, path);
        }
        part = next;
    }
    const char *keys[] = {"TWOINE_IPC_DIR",      "TWOINE_BALL",           "TWOINE_CONFIG",
                          "TWOINE_USER_PROFILE", "TWOINE_SYSTEM_PROFILE", "TWOINE_REGISTRY",
                          "TWOINE_OBJECTS",      "TWOINE_LIBPATH",        "TWOINE_RUNTIME_ID",
                          "TWOINE_DISPLAY_DPI",  "TWOINE_DISPLAY_DEPTH",  NULL};
    const char *values[] = {
        tb_ball_join(c, c->ball, "ipc"),
        c->ball,
        tb_ball_join(c, c->ball, "2ine.cfg"),
        tb_ball_host_path(c, field(env, "USER_INI"), 0),
        tb_ball_host_path(c, field(env, "SYSTEM_INI"), 0),
        tb_ball_join(c, c->roots[2], "OS2/REGISTRY.INI"),
        tb_ball_join(c, c->roots[2], "OS2/OBJECTS.INI"),
        libpath,
        identity,
        get(get(c->manifest, "system"), "display_dpi")
            ? launch_setting_text(c, get(get(c->manifest, "system"), "display_dpi"))
            : "96",
        get(get(c->manifest, "system"), "display_depth")
            ? launch_setting_text(c, get(get(c->manifest, "system"), "display_depth"))
            : "24"};
    if (c->error)
        goto failed;
    for (int i = 0; keys[i]; i++)
        put(env, keys[i], cJSON_CreateString(values[i]));
    const char *ld = getenv("LD_LIBRARY_PATH");
    put(env, "LD_LIBRARY_PATH",
        cJSON_CreateString(tb_ball_format(c, "%s%s%s%s%s", runtime, PATH_SEPARATOR, build,
                                          ld && *ld ? PATH_SEPARATOR : "", ld ? ld : "")));
    if (tb_host_uses_dyld())
    {
        const char *dyld = getenv("DYLD_LIBRARY_PATH");
        put(env, "DYLD_LIBRARY_PATH",
            cJSON_CreateString(tb_ball_format(c, "%s%s%s%s%s", runtime, PATH_SEPARATOR, build,
                                              dyld && *dyld ? PATH_SEPARATOR : "",
                                              dyld ? dyld : "")));
    }
    if (backend)
        put(env, "TWOINE_BACKEND", cJSON_CreateString(backend));
    return env;
failed:
    cJSON_Delete(env);
    return NULL;
}
