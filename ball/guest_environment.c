/* Public domain. No rights reserved. Wholly AI-authored. */
/* The guest environment: CONFIG.SYS assignments and ball overrides. */
#include "ball/ball_internal.h"
#include "os2/config/config_sys.h"

static int valid_env_key(const char *key)
{
    if (!key || (!isalpha((unsigned char)*key) && *key != '_'))
        return 0;
    for (key++; *key; key++)
        if (!isalnum((unsigned char)*key) && *key != '_')
            return 0;
    return 1;
}

static int reserved_env(const char *key)
{
    const char *reserved[] = {"HOME",
                              "TWOINE_DESKTOP_AUXILIARY",
                              "TWOINE_DESKTOP_SESSION",
                              "TWOINE_DESKTOP_NAME",
                              "TWOINE_DESKTOP_ICON",
                              "TWOINE_DESKTOP_BUNDLE",
                              "TWOINE_DESKTOP_CACHE",
                              "TWOINE_DESKTOP_STANDALONE",
                              "PATH",
                              "IS_2INE",
                              "TWOINE_CHILD_ARGUMENTS",
                              "TWOINE_OPERATION_LEASE_FD",
                              "TWOINE_OPERATION_OWNER_FD",
                              "TWOINE_CONFIG",
                              "TWOINE_BALL",
                              "TWOINE_IPC_DIR",
                              "TWOINE_USER_PROFILE",
                              "TWOINE_SYSTEM_PROFILE",
                              "TWOINE_REGISTRY",
                              "TWOINE_OBJECTS",
                              "TWOINE_LIBPATH",
                              "TWOINE_BACKEND",
                              NULL};
    if (!strncmp(key, "LD_", 3) || !strncmp(key, "DYLD_", 5))
        return 1;
    for (int i = 0; reserved[i]; i++)
        if (!strcmp(key, reserved[i]))
            return 1;
    return 0;
}

/* Overlay expressions resolve against the complete inherited layer. A
   self-reference appends to the inherited value; other overrides resolve
   recursively so JSON key ordering cannot affect results. */
static char *expand_env(TbBall *c, const cJSON *base, const cJSON *overlay, const char *key,
                        const char **stack, unsigned depth)
{
    if (depth >= 64)
    {
        tb_ball_fail(c, "Environment expansion is too deep");
        return NULL;
    }
    for (unsigned i = 0; i < depth; i++)
        if (!strcmp(stack[i], key))
        {
            tb_ball_fail(c, "Environment expansion cycle at %s", key);
            return NULL;
        }
    const char *raw = field(overlay, key);
    if (!raw)
        return tb_ball_keep(c, strdup(field(base, key) ? field(base, key) : ""));
    stack[depth] = key;
    char *out = tb_ball_keep(c, strdup(""));
    for (const char *at = raw; *at && out;)
    {
        const char *begin = strchr(at, '%');
        if (!begin)
            return tb_ball_format(c, "%s%s", out, at);
        const char *end = strchr(begin + 1, '%');
        if (!end)
            return tb_ball_format(c, "%s%s", out, at);
        char *ref = tb_ball_keep(c, strdup(begin + 1));
        if (!ref)
            return NULL;
        ref[end - begin - 1] = 0;
        for (char *p = ref; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        if (!strcmp(ref, "PATH"))
            ref = "OS2PATH";
        char *value;
        if (!strcmp(ref, key))
            value = tb_ball_keep(c, strdup(field(base, key) ? field(base, key) : ""));
        else if (field(overlay, ref))
            value = expand_env(c, base, overlay, ref, stack, depth + 1);
        else
            value = tb_ball_keep(c, strdup(field(base, ref) ? field(base, ref) : ""));
        if (!value)
            return NULL;
        out = tb_ball_format(c, "%s%.*s%s", out, (int)(begin - at), at, value);
        if (out && strlen(out) > 1024 * 1024)
        {
            tb_ball_fail(c, "Expanded environment value is too large");
            return NULL;
        }
        at = end + 1;
    }
    return out;
}

static int overlay_environment(TbBall *c, cJSON *env, const cJSON *settings)
{
    if (!settings)
        return 1;
    if (!cJSON_IsObject(settings))
        return tb_ball_fail(c, "Invalid environment object");
    cJSON *normalized = cJSON_CreateObject(), *entry;
    cJSON_ArrayForEach(entry, settings)
    {
        if (!entry->string || !valid_env_key(entry->string) || !str(entry))
        {
            tb_ball_fail(c, "Invalid environment entry");
            break;
        }
        char *key = tb_ball_keep(c, strdup(entry->string));
        if (!key)
            break;
        for (char *p = key; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        if (!strcmp(key, "PATH"))
            key = "OS2PATH";
        if (reserved_env(key) || !strncmp(key, "TWOINE_DRIVE_CWD_", 17) ||
            !strcmp(key, "TWOINE_CURRENT_DRIVE"))
        {
            tb_ball_fail(c, "Reserved host environment variable: %s", key);
            break;
        }
        if (get(normalized, key))
        {
            tb_ball_fail(c, "Duplicate environment variable: %s", key);
            break;
        }
        put(normalized, key, cJSON_CreateString(str(entry)));
    }
    cJSON *inherited = cJSON_Duplicate(env, 1);
    if (!c->error)
        cJSON_ArrayForEach(entry, normalized)
        {
            const char *stack[64];
            char *value = expand_env(c, inherited, normalized, entry->string, stack, 0);
            if (!value)
                break;
            put(env, entry->string, cJSON_CreateString(value));
        }
    cJSON_Delete(inherited);
    cJSON_Delete(normalized);
    return !c->error;
}

cJSON *tb_ball_guest_environment(TbBall *c, const cJSON *overrides)
{
    cJSON *env = cJSON_CreateObject();
    for (int i = 0; os2_default_environment_names[i]; i++)
        cJSON_AddStringToObject(env, os2_default_environment_names[i],
                                os2_default_environment_values[i]);
    char *interpreter = tb_ball_host_path(c, "C:\\OS2\\CMD.EXE", 0);
    if (!interpreter)
        goto failed;
    if (tb_is_file(interpreter))
        cJSON_AddStringToObject(env, "COMSPEC", "C:\\OS2\\CMD.EXE");
    size_t n = 0;
    char *bytes =
        tb_ball_read_file(c, tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "CONFIG.SYS"), &n, 1);
    if (!bytes)
        goto failed;
    char *text = tb_ball_latin1_to_utf8(c, bytes, n);
    if (!text)
        goto failed;
    char *line = text;
    while (line && *line)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *name, *value;
        int kind = os2_config_sys_statement(line, &name, &value);
        if (kind == OS2_CONFIG_SYS_SET ||
            (kind == OS2_CONFIG_SYS_COMMAND && !strcmp(name, "LIBPATH")))
            put(env, !strcmp(name, "PATH") ? "OS2PATH" : name, cJSON_CreateString(value));
        line = next;
    }
    if (!overlay_environment(c, env, get(c->manifest, "environment")) ||
        !overlay_environment(c, env, overrides))
        goto failed;
    cJSON *entry;
    cJSON_ArrayForEach(entry, env)
    {
        if (!valid_env_key(entry->string) || !str(entry) || reserved_env(entry->string))
        {
            tb_ball_fail(c, "Invalid or reserved environment entry: %s", entry->string);
            goto failed;
        }
    }
    return env;
failed:
    cJSON_Delete(env);
    return NULL;
}
