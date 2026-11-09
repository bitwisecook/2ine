/* Public domain. No rights reserved. Wholly AI-authored. */
/* The JSON tb_request entry point and its action dispatch. */
#include "ball/ball_internal.h"
#include "ball/include/twoine_ball.h"
#include "formats/encoding/base64.h"
#include "storage/container.h"
#include "ball/application_icon.h"
#include "environment/audio.h"
#include "ball/facade/facade.h"
#include "ball/host_environment.h"
#include "ball/launch_dependencies.h"
#include "ball/launch_prerequisites.h"
#include "environment/components/operations.h"
#include "environment/dependencies/audit.h"
#include "environment/media/extract.h"
#include "environment/media/support.h"
#include "ball/prepare.h"
#include "ball/runtime.h"
#include "ball/runtime_config.h"
#include "ball/runtime_features.h"
#include "ball/runtime_selection.h"
#include "environment/wps_sync.h"
#include "ball/project_sources.h"

static void dependency_refresh_mutation(TbBall *c, const cJSON *request)
{
    cJSON *audit = audit_dependencies(c, request, 0);
    cJSON_Delete(audit);
}

static cJSON *dispatch(TbBall *c, const cJSON *request)
{
    const char *action = field(request, "action");
    if (!action)
    {
        tb_ball_fail(c, "An action is required");
        return NULL;
    }
    if (!strcmp(action, "effective-runtime"))
    {
        const char *candidates[] = {field(request, "path"), field(request, "build")};
        for (unsigned i = 0; i < 2; i++)
            if (candidates[i] && *candidates[i] && native_host_path(candidates[i]) &&
                (tb_host_executable(tb_ball_join(c, candidates[i], "lx_loader")) ||
                 tb_host_executable(tb_ball_join(c, candidates[i], "lx_loader.exe"))))
                return cJSON_CreateString(candidates[i]);
        tb_ball_fail(
            c, "No compatible host runtime is available. Choose a runtime for this computer.");
        return NULL;
    }
    if (!strcmp(action, "effective-backend"))
    {
        const char *build = field(request, "build");
        if (!build || !*build)
        {
            tb_ball_fail(c, "A receiving host runtime is required");
            return NULL;
        }
        const char *backend = runtime_effective_backend(c, build, field(request, "path"), 1);
        return backend ? cJSON_CreateString(backend) : NULL;
    }
    if (!strcmp(action, "format-arguments"))
    {
        const cJSON *args = get(request, "arguments");
        if (!tb_ball_array_strings(c, args))
            return NULL;
        char *out = tb_ball_keep(c, strdup(""));
        cJSON *item;
        cJSON_ArrayForEach(item, args)
        {
            const char *value = str(item);
            char *quoted = tb_ball_keep(c, strdup("\""));
            for (const char *p = value; *p && quoted; p++)
                quoted = tb_ball_format(c, "%s%c%s", quoted, *p, *p == '\"' ? "\"" : "");
            out = tb_ball_format(c, "%s%s%s\"", out, *out ? " " : "", quoted);
            if (!out)
                return NULL;
        }
        return cJSON_CreateString(out);
    }
    if (!strcmp(action, "parse-arguments"))
    {
        const char *text = field(request, "text");
        if (!text)
        {
            tb_ball_fail(c, "Argument text is required");
            return NULL;
        }
        cJSON *args = cJSON_CreateArray();
        char *word = tb_ball_keep(c, strdup(""));
        int started = 0;
        char quote = 0;
        for (const char *p = text; *p; p++)
        {
            if (quote)
            {
                if (*p == quote)
                {
                    if (p[1] == quote)
                    {
                        word = tb_ball_format(c, "%s%c", word, *p);
                        p++;
                    }
                    else
                        quote = 0;
                }
                else
                    word = tb_ball_format(c, "%s%c", word, *p);
            }
            else if (*p == '\"' || *p == '\'')
            {
                quote = *p;
                started = 1;
            }
            else if (isspace((unsigned char)*p))
            {
                if (started)
                {
                    cJSON_AddItemToArray(args, cJSON_CreateString(word));
                    word = tb_ball_keep(c, strdup(""));
                    started = 0;
                }
            }
            else
            {
                word = tb_ball_format(c, "%s%c", word, *p);
                started = 1;
            }
            if (!word)
                break;
        }
        if (quote)
            tb_ball_fail(c, "Close the quote in the arguments field.");
        if (c->error)
        {
            cJSON_Delete(args);
            return NULL;
        }
        if (started)
            cJSON_AddItemToArray(args, cJSON_CreateString(word));
        return args;
    }
    if (!strcmp(action, "image-preview"))
    {
        const char *path = field(request, "path");
        if (!path)
        {
            tb_ball_fail(c, "An image path is required");
            return NULL;
        }
        size_t size = 0;
        char *bytes = tb_ball_read_file(c, path, &size, 0);
        if (!bytes)
            return NULL;
        cJSON *result = cJSON_CreateObject();
        char *encoded = tb_base64((const uint8_t *)bytes, size);
        if (!encoded)
        {
            cJSON_Delete(result);
            tb_ball_fail(c, "Image is too large");
            return NULL;
        }
        cJSON_AddStringToObject(result, "source", "custom");
        cJSON_AddStringToObject(result, "image", encoded);
        free(encoded);
        return result;
    }
    if (!strcmp(action, "project-sources"))
    {
        const char *build = field(request, "build");
        if (build && *build)
        {
            char *path = tb_ball_join(c, build, "project-sources.json");
            if (tb_is_file(path))
            {
                char *text = tb_ball_read_file(c, path, NULL, 0);
                cJSON *catalogue = text ? cJSON_Parse(text) : NULL;
                if (!catalogue || !cJSON_IsArray(get(catalogue, "projects")))
                {
                    cJSON_Delete(catalogue);
                    tb_ball_fail(c, "Invalid packaged source catalogue");
                    return NULL;
                }
                return catalogue;
            }
        }
        cJSON *catalogue = cJSON_Parse(tb_project_sources);
        cJSON *project = cJSON_GetArrayItem(get(catalogue, "projects"), 0);
        if (project && strlen(TB_SOURCE_REVISION) == 40)
        {
            put(project, "version", cJSON_CreateString(TB_SOURCE_VERSION));
            put(project, "source",
                cJSON_CreateString(tb_ball_format(c, "https://github.com/bitwisecook/2ine/tree/%s",
                                                  TB_SOURCE_REVISION)));
            put(project, "download",
                cJSON_CreateString(
                    tb_ball_format(c, "https://github.com/bitwisecook/2ine/archive/%s.tar.gz",
                                   TB_SOURCE_REVISION)));
        }
        return catalogue;
    }
    if (!strcmp(action, "defaults"))
    {
        cJSON *v = cJSON_CreateObject();
        for (int i = 0; os2_default_environment_names[i]; i++)
            cJSON_AddStringToObject(v, os2_default_environment_names[i],
                                    os2_default_environment_values[i]);
        return v;
    }
    if (!strcmp(action, "unset-environment"))
    {
        cJSON *v = cJSON_CreateArray();
        for (int i = 0; launch_unset_environment_keys[i]; i++)
            cJSON_AddItemToArray(v, cJSON_CreateString(launch_unset_environment_keys[i]));
        return v;
    }
    if (!strcmp(action, "runtime-files"))
    {
        const char *build = field(request, "build");
        if (!build)
        {
            tb_ball_fail(c, "A build directory is required");
            return NULL;
        }
        size_t n = 0;
        char **names = runtime_names(build, &n);
        cJSON *v = cJSON_CreateArray();
        for (size_t i = 0; i < n; i++)
            cJSON_AddItemToArray(v, cJSON_CreateString(tb_ball_join(c, build, names[i])));
        tb_names_free(names, n);
        return v;
    }
    if (!strcmp(action, "sync-runtime"))
    {
        const char *build = field(request, "build"), *destination = field(request, "directory");
        if (!build || !destination)
        {
            tb_ball_fail(c, "Build and destination are required");
            return NULL;
        }
        return runtime_sync(c, build, destination) ? cJSON_CreateBool(1) : NULL;
    }
    if (!strcmp(action, "discover"))
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
        char *base =
            xdg && *xdg ? tb_ball_keep(c, strdup(xdg)) : tb_ball_join(c, home, ".local/share");
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
    if (!strcmp(action, "import-ball"))
        return tb_ball_import(c, request);
    if (!strcmp(action, "rename-ball"))
        return tb_ball_rename(c, request);
    cJSON *args = get(request, "arguments");
    const char *first = str(cJSON_GetArrayItem(args, 0));
    if (!strcmp(action, "command") && first && !strcmp(first, "init"))
    {
        if (!tb_ball_create(c, field(request, "ball")))
            return NULL;
        return facade_snapshot(c);
    }
    if (!tb_ball_load(c, request))
        return NULL;
    if ((!strcmp(action, "command") || !strcmp(action, "save-application") ||
         !strcmp(action, "application-icon")) &&
        !wps_sync(c))
        return NULL;
    if (!strcmp(action, "audit-dependencies") || !strcmp(action, "record-install") ||
        !strcmp(action, "prepare-launch") || !strcmp(action, "launch-dependency-status") ||
        !strcmp(action, "ensure-launch-dependencies") || !strcmp(action, "open-os2-source") ||
        !strcmp(action, "plan-os2-extraction") || !strcmp(action, "import-os2-support"))
    {
        runtime_feature_policy(c, field(request, "build"));
        if (c->error)
            return NULL;
    }
    if (!strcmp(action, "runtime-features"))
        return runtime_features(c, field(request, "build"));
    if (!strcmp(action, "config-sys"))
    {
        size_t n = 0;
        char *bytes =
            tb_ball_read_file(c, tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "CONFIG.SYS"), &n, 1);
        return bytes ? cJSON_CreateString(tb_ball_latin1_to_utf8(c, bytes, n)) : NULL;
    }
    if (!strcmp(action, "configuration-files"))
        return configuration_files(c, request);
    if (!strcmp(action, "configuration-file-read"))
        return configuration_file_read(c, request);
    if (!strcmp(action, "configuration-file-write"))
        return configuration_file_write(c, request);
    if (!strcmp(action, "configuration-read"))
        return configuration_read(c, request);
    if (!strcmp(action, "configuration-write"))
        return configuration_write(c, request);
    if (!strcmp(action, "audio-snapshot"))
        return audio_snapshot(c);
    if (!strcmp(action, "save-audio"))
        return audio_save(c, request);
    if (!strcmp(action, "import-soundfont"))
        return audio_import_archive(c, request);
    if (!strcmp(action, "export-ball"))
        return tb_ball_export(c, request);
    if (!strcmp(action, "os2-support"))
        return os2_support(c);
    if (!strcmp(action, "launch-prerequisites"))
        return launch_prerequisites(c, request);
    if (!strcmp(action, "ball-navigation-state"))
        return ball_navigation_state(c);
    if (!strcmp(action, "remember-os2-media"))
        return remember_os2_media(c, request);
    if (!strcmp(action, "browse-media"))
        return browse_media(c, field(request, "path"));
    if (!strcmp(action, "open-os2-source"))
        return open_os2_source(c, request);
    if (!strcmp(action, "validate-os2-media"))
        return validate_os2_media(c, request);
    if (!strcmp(action, "plan-os2-extraction"))
        return plan_os2_extraction(c, request);
    if (!strcmp(action, "import-os2-support"))
    {
        cJSON *result = component_import(c, request);
        dependency_refresh_mutation(c, request);
        return result;
    }
    if (!strcmp(action, "os2-component-state"))
        return os2_component_state(c, request);
    if (!strcmp(action, "remove-os2-components"))
    {
        cJSON *result = remove_os2_components(c, request);
        dependency_refresh_mutation(c, request);
        return result;
    }
    if (!strcmp(action, "cancel-os2-component-operation"))
        return cancel_os2_component_operation(c, request);
    if (!strcmp(action, "snapshot"))
        return facade_snapshot(c);
    if (!strcmp(action, "browse"))
        return tb_ball_browse(c, field(request, "path")
                                     ? field(request, "path")
                                     : tb_ball_format(c, "%c:", 'A' + tb_ball_current_drive(c)));
    if (!strcmp(action, "import-files"))
    {
        cJSON *result = tb_ball_import_files(c, request);
        dependency_refresh_mutation(c, request);
        return result;
    }
    if (!strcmp(action, "guest-environment"))
        return tb_ball_guest_environment(c, get(request, "environment"));
    if (!strcmp(action, "save-application"))
    {
        const char *name = field(request, "originalName");
        if (!name || !*name)
            name = field(request, "name");
        const cJSON *previous = name ? get(get(c->manifest, "applications"), name) : NULL;
        int changed =
            !previous || !equal_case(field(previous, "program"), field(request, "program"));
        if (!tb_ball_save_application(c, request))
            return NULL;
        if (changed)
            dependency_refresh_mutation(c, request);
        return facade_snapshot(c);
    }
    if (!strcmp(action, "application-icon"))
        return launch_application_icon(c, request);
    if (!strcmp(action, "set-working-directory"))
    {
        const char *path = field(request, "path");
        if (!path || strlen(path) < 3 || path[1] != ':' || path[2] != '\\')
        {
            tb_ball_fail(c, "Use an absolute drive-letter directory");
            return NULL;
        }
        char key[2] = {path[0], 0};
        cJSON *check = cJSON_CreateObject();
        cJSON_AddStringToObject(check, key, path);
        int valid = tb_ball_validate_directories(c, check);
        cJSON_Delete(check);
        if (!valid)
            return NULL;
        cJSON *dirs = object(c->manifest, "workingDirectories");
        if (!dirs)
        {
            tb_ball_fail(c, "Invalid working directories");
            return NULL;
        }
        char *host = tb_ball_host_path(c, path, 0);
        put(dirs, key, cJSON_CreateString(tb_ball_guest_for_input(c, host, path)));
        put(c->manifest, "currentDrive", cJSON_CreateString(key));
        return tb_ball_save_manifest(c) ? facade_snapshot(c) : NULL;
    }
    if (!strcmp(action, "command"))
    {
        if (!tb_ball_command(c, request))
            return NULL;
        if (first && (!strcmp(first, "add-app") || !strcmp(first, "restore-app")))
            dependency_refresh_mutation(c, request);
        return facade_snapshot(c);
    }
    if (!strcmp(action, "runtime-build") || !strcmp(action, "restore-runtime"))
    {
        int restore = !strcmp(action, "restore-runtime");
        const char *name = field(request, "build");
        const char *build = restore ? name : name ? tb_ball_resolve_path(c, name, 1) : NULL;
        if (!restore && name && (!build || !runtime_loader_name(c, build)))
            return NULL;
        cJSON *runtime = object(c->manifest, "runtime");
        if (!runtime)
        {
            tb_ball_fail(c, "Invalid runtime object");
            return NULL;
        }
        if (build)
            put(runtime, "build", cJSON_CreateString(build));
        else
            cJSON_DeleteItemFromObjectCaseSensitive(runtime, "build");
        if (!tb_ball_save_manifest(c))
            return NULL;
        return facade_snapshot(c);
    }
    if (!strcmp(action, "save-config-sys"))
    {
        const char *expected = field(request, "expected"), *text = field(request, "text");
        if (!expected || !text)
        {
            tb_ball_fail(c, "Expected and edited CONFIG.SYS text are required");
            return NULL;
        }
        char *path = tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "CONFIG.SYS");
        size_t n = 0;
        char *bytes = tb_ball_read_file(c, path, &n, 1);
        if (!bytes)
            return NULL;
        char *current = tb_ball_latin1_to_utf8(c, bytes, n);
        if (!current || strcmp(expected, current))
        {
            tb_ball_fail(c, "CONFIG.SYS changed on disk. Reload it before saving your edits.");
            return NULL;
        }
        char *edited = tb_ball_utf8_to_latin1(c, text, &n);
        if (!edited || !tb_atomic_write(path, edited, n))
        {
            tb_ball_fail(c, "Cannot save CONFIG.SYS");
            return NULL;
        }
        return facade_snapshot(c);
    }
    if (!strcmp(action, "prepare-launch"))
    {
        const cJSON *resolve = get(request, "resolve");
        if (resolve && !cJSON_IsBool(resolve))
        {
            tb_ball_fail(c, "resolve must be a boolean");
            return NULL;
        }
        /* WPS projection is launch registration, independent of dependency
           discovery. Resolve newly registered names before the cached query. */
        if (!wps_sync(c))
            return NULL;
        cJSON *audit = launch_dependencies(c, request);
        if (!audit)
            return NULL;
        cJSON *plan = launch_prepare(c, request);
        if (plan)
            cJSON_AddItemToObject(plan, "dependencyAudit", dependency_compact(audit));
        cJSON_Delete(audit);
        return plan;
    }
    if (!strcmp(action, "ensure-launch-dependencies") ||
        !strcmp(action, "launch-dependency-status"))
        return launch_dependencies(c, request);
    if (!strcmp(action, "ensure-dependencies"))
        return ensure_dependencies(c, request);
    if (!strcmp(action, "audit-dependencies"))
    {
        const cJSON *resolve = get(request, "resolve");
        if (resolve && !cJSON_IsBool(resolve))
        {
            tb_ball_fail(c, "resolve must be a boolean");
            return NULL;
        }
        return audit_dependencies(c, request, !resolve || cJSON_IsTrue(resolve));
    }
    if (!strcmp(action, "record-install"))
    {
        cJSON *receipt = get(request, "receipt"), *exit = get(request, "exitCode");
        const char *program = field(request, "programHost");
        if (!cJSON_IsObject(receipt) || !cJSON_IsNumber(exit) || !program)
        {
            tb_ball_fail(c, "A launch receipt, program and actual exit code are required");
            return NULL;
        }
        cJSON *saved = cJSON_Duplicate(receipt, 1);
        unsigned char digest[32];
        if (!field(saved, "program_sha256") && !tb_ball_digest_file(c, program, digest))
        {
            cJSON_Delete(saved);
            return NULL;
        }
        if (!field(saved, "program_sha256"))
            put(saved, "program_sha256", cJSON_CreateString(tb_ball_hex(c, digest)));
        put(saved, "exit_code", cJSON_Duplicate(exit, 1));
        put(saved, "finished", cJSON_CreateString(tb_ball_now(c)));
        if (!wps_sync(c))
        {
            cJSON_Delete(saved);
            return NULL;
        }
        cJSON *registered = wps_latest(c);
        if (registered)
            put(saved, "applicationRegistration", registered);
        cJSON *audit = audit_dependencies(c, request, exit->valuedouble == 0);
        if (audit)
        {
            put(saved, "dependencyAudit", dependency_compact(audit));
            cJSON_Delete(audit);
        }
        int ok = tb_ball_write_json(c, tb_ball_join(c, c->ball, "logs/last-install.json"), saved);
        cJSON_Delete(saved);
        return ok ? facade_snapshot(c) : NULL;
    }
    /* Small operations exposed for CLI/native frontends and interoperability tests. */
    if (!strcmp(action, "host-path") || !strcmp(action, "guest-path"))
    {
        const char *name = field(request, "path");
        char *path = name ? tb_ball_host_path(c, name, !strcmp(action, "guest-path")) : NULL;
        return path ? cJSON_CreateString(!strcmp(action, "host-path") ? path
                                                                      : tb_ball_guest_path(c, path))
                    : NULL;
    }
    if (!strcmp(action, "environment"))
    {
        const char *b = field(request, "build");
        char *build = tb_ball_resolve_path(c, b && *b ? b : ".", 0);
        return build ? launch_host_environment(c, build, field(request, "backend"),
                                               get(c->application, "environment"))
                     : NULL;
    }
    if (!strcmp(action, "write-config"))
        return launch_write_runtime_config(c) ? facade_snapshot(c) : NULL;
    tb_ball_fail(c, "Unsupported ball action: %s", action);
    return NULL;
}

unsigned tb_abi_version(void)
{
    return 1;
}

void tb_free(void *p)
{
    free(p);
}

int tb_request(const char *text, char **result, char **error)
{
    if (!result || !error)
        return 2;
    *result = NULL;
    *error = NULL;
    TbBall c = {0};
    c.hooks = &facade_hooks;
    tb_media_session_begin();
    cJSON *request = text ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
    cJSON *response = NULL;
    if (!cJSON_IsObject(request))
        tb_ball_fail(&c, "Request must be a JSON object");
    else
        response = dispatch(&c, request);
    if (response && !c.error)
        *result = cJSON_PrintUnformatted(response);
    if (!*result && !c.error)
        tb_ball_fail(&c, "Ball operation failed");
    cJSON_Delete(response);
    cJSON_Delete(request);
    cJSON_Delete(c.manifest);
    for (size_t i = 0; i < c.count; i++)
        free(c.allocations[i]);
    free(c.allocations);
    *error = c.error;
    tb_media_session_end();
    return c.error || !*result ? 2 : 0;
}
