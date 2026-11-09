/* Public domain. No rights reserved. Wholly AI-authored. */
/* The JSON tb_request entry point and its action dispatch. */
#include "ball/ball_internal.h"
#include "ball/include/twoine_ball.h"
#include "formats/encoding/base64.h"
#include "storage/container.h"
#include "emulator/launch/argv.h"
#include "emulator/launch/desktop_identity.h"
#include "environment/audio.h"
#include "environment/install_records.h"
#include "ball/facade/facade.h"
#include "os2/config/config_sys.h"
#include "emulator/launch/host_environment.h"
#include "emulator/launch/dependencies.h"
#include "emulator/launch/prerequisites.h"
#include "environment/components/operations.h"
#include "environment/dependencies/audit.h"
#include "environment/media/extract.h"
#include "environment/media/support.h"
#include "emulator/launch/plan.h"
#include "emulator/launch/runtime.h"
#include "emulator/launch/runtime_config.h"
#include "emulator/launch/features.h"
#include "emulator/launch/runtime_selection.h"
#include "environment/wps_sync.h"
#include "ball/facade/project_sources.h"

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
        return launch_effective_runtime(c, request);
    if (!strcmp(action, "effective-backend"))
        return launch_effective_backend(c, request);
    if (!strcmp(action, "format-arguments"))
        return launch_format_arguments(c, request);
    if (!strcmp(action, "parse-arguments"))
        return launch_parse_arguments(c, request);
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
        return launch_unset_environment();
    if (!strcmp(action, "runtime-files"))
        return runtime_files(c, request);
    if (!strcmp(action, "sync-runtime"))
        return runtime_sync_request(c, request);
    if (!strcmp(action, "discover"))
        return tb_ball_discover(c);
    if (!strcmp(action, "reference-remaps"))
        return tb_ball_reference_remaps(c, request);
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
        return tb_ball_config_sys(c);
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
    if (!strcmp(action, "reference-status") || !strcmp(action, "search-references"))
    {
        if (!strcmp(action, "search-references") && !cJSON_IsArray(get(request, "searchRoots")))
        {
            tb_ball_fail(c, "Choose folders to search");
            return NULL;
        }
        return tb_ball_reference_status(c, request);
    }
    if (!strcmp(action, "relink-reference"))
    {
        const char *id = field(request, "id");
        if (!tb_ball_relink_reference(c, request))
            return NULL;
        if (id && !strncmp(id, "media-", 6) && !os2_media_follow_references(c))
            return NULL;
        return facade_snapshot(c);
    }
    if (!strcmp(action, "bundle-reference"))
        return tb_ball_bundle_reference(c, request) ? facade_snapshot(c) : NULL;
    if (!strcmp(action, "unbundle-reference"))
        return tb_ball_unbundle_reference(c, request) ? facade_snapshot(c) : NULL;
    if (!strcmp(action, "set-reference-host-data"))
        return tb_ball_set_reference_host_data(c, request) ? tb_ball_reference_status(c, request)
                                                           : NULL;
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
        return tb_ball_set_working_directory(c, field(request, "path")) ? facade_snapshot(c) : NULL;
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
        return tb_ball_set_runtime_build(c, build) ? facade_snapshot(c) : NULL;
    }
    if (!strcmp(action, "save-config-sys"))
        return tb_ball_save_config_sys(c, request) ? facade_snapshot(c) : NULL;
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
        return environment_record_install(c, request) ? facade_snapshot(c) : NULL;
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
        return launch_environment(c, request);
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
