/* Public domain. No rights reserved. Wholly AI-authored. */
/* Add installed component directories to OS/2's configuration search lists. */
#include "ball/store.h"
#include "os2/config/config_sys.h"
#include "environment/components/search_paths.h"

static char *os2_path_append(TbBall *c, const char *value, const char *directory)
{
    if (!value)
        value = "";
    if (os2_search_list_contains(value, directory))
        return (char *)value;
    size_t n = strlen(value);
    return tb_ball_format(c, "%s%s%s", value, n && value[n - 1] != ';' ? ";" : "", directory);
}

static void os2_path_want(TbBall *c, cJSON *wanted, const char *key, const char *directory)
{
    char *value = os2_path_append(c, field(wanted, key), directory);
    if (value)
        put(wanted, key, cJSON_CreateString(value));
}

static int os2_component_directory(const char *directory)
{
    static const char *normal[] = {"C:\\OS2",
                                   "C:\\OS2\\SYSTEM",
                                   "C:\\OS2\\INSTALL",
                                   "C:\\OS2\\APPS",
                                   "C:\\OS2\\APPS\\DLL",
                                   "C:\\OS2\\DLL",
                                   "C:\\OS2\\HELP",
                                   "C:\\OS2\\BOOK",
                                   "C:\\OS2\\BITMAP",
                                   "C:\\OS2\\MDOS",
                                   "C:\\OS2\\MDOS\\WINOS2",
                                   "C:\\OS2\\MDOS\\WINOS2\\SYSTEM",
                                   "C:\\MMOS2",
                                   "C:\\MMOS2\\DLL",
                                   "C:\\MMOS2\\HELP",
                                   "C:\\MMOS2\\INSTALL",
                                   "C:\\TCPIP\\BIN",
                                   "C:\\TCPIP\\DLL",
                                   "C:\\TCPIP\\HELP",
                                   "C:\\TCPIP\\BOOK",
                                   "C:\\MPTN\\BIN",
                                   "C:\\MPTN\\DLL",
                                   "C:\\MPTN\\HELP",
                                   "C:\\MPTN\\BOOK",
                                   "C:\\IBMLAN\\NETPROG",
                                   "C:\\IBMLAN\\NETLIB",
                                   "C:\\IBMLAN\\HELP",
                                   "C:\\IBMLAN\\BOOK",
                                   "C:\\IBMCOM",
                                   "C:\\IBMCOM\\DLL",
                                   NULL};
    for (int i = 0; normal[i]; i++)
        if (equal_case(directory, normal[i]))
            return 1;
    return 0;
}

static cJSON *os2_component_paths(TbBall *c, const cJSON *files)
{
    cJSON *wanted = cJSON_CreateObject();
    const cJSON *file;
    cJSON_ArrayForEach(file, files)
    {
        const char *path = field(file, "path");
        if (!path || field(file, "originalPath"))
            continue;
        char *host = tb_ball_host_path(c, path, 0);
        if (!host || !tb_is_file(host))
            continue;
        char *directory = tb_ball_keep(c, strdup(path));
        if (!directory)
            break;
        for (char *at = directory; *at; at++)
            *at = *at == '/' ? '\\' : (char)toupper((unsigned char)*at);
        char *base = strrchr(directory, '\\');
        if (!base)
            continue;
        *base++ = 0;
        if (!os2_component_directory(directory))
            continue;
        const char *extension = strrchr(base, '.');
        if (!extension)
            continue;
        const int windows = strstr(directory, "\\WINOS2") != NULL;
        if (!strcmp(extension, ".EXE") || !strcmp(extension, ".CMD") ||
            (windows && !strcmp(extension, ".COM")))
        {
            os2_path_want(c, wanted, "PATH", directory);
            os2_path_want(c, wanted, "DPATH", directory);
        }
        else if (!strcmp(extension, ".DLL") && !windows)
            os2_path_want(c, wanted, "LIBPATH", directory);
        else if (!strcmp(extension, ".HLP") && !windows)
            os2_path_want(c, wanted, "HELP", directory);
        else if (!strcmp(extension, ".INF") && !windows)
            os2_path_want(c, wanted, "BOOKSHELF", directory);
        else if (!strcmp(extension, ".MSG") || !strcmp(extension, ".DCP") ||
                 !strcmp(extension, ".BMP") || !strcmp(extension, ".MET") ||
                 !strcmp(extension, ".ICO"))
            os2_path_want(c, wanted, "DPATH", directory);
    }
    return wanted;
}

/* Preserve every original byte and append only changed effective assignments.
   CONFIG.SYS permits later assignments to replace earlier search lists. */
int os2_configure_paths(TbBall *c, const cJSON *files)
{
    cJSON *wanted = os2_component_paths(c, files);
    if (!cJSON_GetArraySize(wanted))
    {
        cJSON_Delete(wanted);
        return !c->error;
    }
    char *path = tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "CONFIG.SYS");
    size_t n = 0;
    char *bytes = tb_ball_read_file(c, path, &n, 1);
    char *copy = bytes ? tb_ball_keep(c, strdup(bytes)) : NULL;
    cJSON *configured = cJSON_CreateObject(), *defaults = tb_ball_guest_environment(c, NULL);
    if (!copy || !defaults)
        goto done;
    for (char *line = copy; line && *line;)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *name, *value;
        int kind = os2_config_sys_statement(line, &name, &value);
        if ((kind == OS2_CONFIG_SYS_SET ||
             (kind == OS2_CONFIG_SYS_COMMAND && !strcmp(name, "LIBPATH"))) &&
            get(wanted, name))
            put(configured, name, cJSON_CreateString(value));
        line = next;
    }
    char *output = bytes;
    cJSON *setting;
    cJSON *overrides = get(c->manifest, "environment");
    int manifest_changed = 0;
    cJSON_ArrayForEach(setting, wanted)
    {
        const char *key = setting->string;
        const char *previous = field(configured, key);
        const char *fallback = field(defaults, !strcmp(key, "PATH") ? "OS2PATH" : key);
        char *value = (char *)(previous ? previous : fallback ? fallback : "");
        for (const char *part = str(setting); *part;)
        {
            const char *end = strchr(part, ';');
            size_t length = end ? (size_t)(end - part) : strlen(part);
            char *directory = tb_ball_format(c, "%.*s", (int)length, part);
            value = os2_path_append(c, value, directory);
            for (cJSON *override = overrides ? overrides->child : NULL, *next; override;
                 override = next)
            {
                next = override->next;
                if (!equal_case(override->string, key) &&
                    (strcmp(key, "PATH") || !equal_case(override->string, "OS2PATH")))
                    continue;
                const char *old = str(override);
                char *updated = old ? os2_path_append(c, old, directory) : NULL;
                if (updated && strcmp(updated, old))
                {
                    char *override_key = tb_ball_keep(c, strdup(override->string));
                    put(overrides, override_key, cJSON_CreateString(updated));
                    manifest_changed = 1;
                }
            }
            part += length;
            if (*part)
                part++;
        }
        if (!previous || strcmp(previous, value))
            output = tb_ball_format(c, "%s%s%s%s=%s\r\n", output,
                                    *output && output[strlen(output) - 1] != '\n' ? "\r\n" : "",
                                    !strcmp(key, "LIBPATH") ? "" : "SET ", key, value);
    }
    if (!c->error && output != bytes)
    {
        size_t current_size = 0;
        char *current = tb_ball_read_file(c, path, &current_size, 1);
        if (!current || current_size != n || memcmp(current, bytes, n))
            tb_ball_fail(c,
                         "CONFIG.SYS changed during extraction; its search paths were not updated");
        else if (!tb_atomic_write(path, output, strlen(output)))
            tb_ball_fail(c, "Cannot update OS/2 component search paths in CONFIG.SYS");
    }
    if (!c->error && manifest_changed)
        tb_ball_save_manifest(c);
done:
    cJSON_Delete(defaults);
    cJSON_Delete(configured);
    cJSON_Delete(wanted);
    return !c->error;
}
