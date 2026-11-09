/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launch planning: the command, environment and receipt of a launch. */
#include "ball/ball_internal.h"
#include "ball/application_icon.h"
#include "ball/commands.h"
#include "ball/drives.h"
#include "ball/host_environment.h"
#include "ball/interpreter.h"
#include "ball/launch_prerequisites.h"
#include "ball/os2_support.h"
#include "ball/paths.h"
#include "ball/prepare.h"
#include "ball/runtime.h"
#include "ball/runtime_config.h"
#include "ball/runtime_features.h"
#include "ball/runtime_selection.h"
#include "ball/wps_applications.h"

cJSON *launch_prepare(Context *c, const cJSON *request)
{
    if (!wps_sync(c))
        return NULL;
    const char *build_path = field(request, "build");
    if (!build_path || !*build_path)
    {
        tb_ball_fail(c, "Choose a runtime build");
        return NULL;
    }
    char *build = tb_ball_resolve_path(c, build_path, 1);
    if (!build)
        return NULL;
    const char *loader = runtime_loader_name(c, build);
    if (!loader)
        return NULL;
    cJSON *args = get(request, "arguments");
    if (!tb_ball_array_strings(c, args) || !cJSON_GetArraySize(args))
        return NULL;
    const char *action = str(cJSON_GetArrayItem(args, 0));
    c->application = request;
    if (!tb_ball_validate_directories(c, get(request, "workingDirectories")))
        return NULL;
    int count = cJSON_GetArraySize(args), at = 1;
    const char *cwd = NULL, *program = NULL;
    cJSON *argv = cJSON_CreateArray();
    if (!strcmp(action, "launch"))
    {
        const char *name = str(cJSON_GetArrayItem(args, at++));
        cJSON *app = name ? get(get(c->manifest, "applications"), name) : NULL;
        if (!cJSON_IsObject(app))
        {
            tb_ball_fail(c, "Saved application does not exist: %s", name ? name : "");
            goto failed;
        }
        c->application = app;
        program = field(app, "program");
        cwd = field(app, "cwd");
        cJSON *a;
        cJSON_ArrayForEach(a, get(app, "arguments"))
            cJSON_AddItemToArray(argv, cJSON_Duplicate(a, 1));
    }
    else if (!strcmp(action, "run") || !strcmp(action, "install"))
    {
        if (at < count && !strcmp(str(cJSON_GetArrayItem(args, at)), "--cwd"))
        {
            cwd = str(cJSON_GetArrayItem(args, at + 1));
            at += 2;
        }
        program = str(cJSON_GetArrayItem(args, at++));
    }
    else
    {
        tb_ball_fail(c, "Use run, install or launch");
        goto failed;
    }
    for (; at < count; at++)
        cJSON_AddItemToArray(argv, cJSON_Duplicate(cJSON_GetArrayItem(args, at), 1));
    char *host = tb_ball_host_path(c, program, cJSON_IsTrue(get(request, "hostProgram")));
    if (!host)
        goto failed;
    if (!tb_is_file(host))
    {
        tb_ball_fail(c, "Program does not exist: %s", program);
        goto failed;
    }
    /* SmartSuite's executable installer invokes CMD.EXE for child commands. */
    if (!strcmp(action, "install") && installer_support_required(c, host))
    {
        cJSON *support = os2_support(c);
        int complete = support && cJSON_IsTrue(get(support, "complete"));
        cJSON_Delete(support);
        if (!complete)
        {
            tb_ball_fail(
                c,
                "SmartSuite needs original OS/2 support files, including CMD.EXE. Select CMD and REXX Support in OS/2 Components and wait for it to finish installing before retrying the installer.");
            goto failed;
        }
    }
    /* Installers usually open companion files relative to their own directory. */
    if (!strcmp(action, "install") && !cwd)
    {
        const char *guest = tb_ball_guest_for_input(c, host, program);
        if (guest &&
            !field(get(c->application, "workingDirectories"), tb_ball_format(c, "%c", guest[0])))
        {
            const char *slash = strrchr(guest, '\\');
            if (slash)
                cwd =
                    tb_ball_format(c, "%.*s", (int)(slash == guest + 2 ? 3 : slash - guest), guest);
        }
    }
    char *original_host = host;
    const char *extension = strrchr(program, '.');
    if (extension && (equal_case(extension, ".CMD") || equal_case(extension, ".BAT")))
    {
        const char *script = tb_ball_guest_for_input(c, host, program);
        host = launch_command_interpreter(c);
        if (!host || !script)
            goto failed;
        FILE *command = tb_fopen(original_host, "rb");
        int rexx = command && fgetc(command) == '/' && fgetc(command) == '*';
        if (command)
            fclose(command);
        if (rexx)
        {
            cJSON *support = os2_support(c);
            int ready = support && cJSON_IsTrue(get(support, "rexxComplete"));
            cJSON_Delete(support);
            if (!ready)
            {
                tb_ball_fail(
                    c,
                    "This command file needs the original OS/2 REXX interpreter and DLLs. Select CMD and REXX Support in OS/2 Components and wait for it to finish installing before retrying this command file.");
                goto failed;
            }
        }
        cJSON_InsertItemInArray(argv, 0, cJSON_CreateString(script));
        cJSON_InsertItemInArray(argv, 0, cJSON_CreateString("CALL"));
        cJSON_InsertItemInArray(argv, 0, cJSON_CreateString("/C"));
    }
    char *working = NULL;
    if (cwd && *cwd)
        working = tb_ball_host_path(c, cwd, 0);
    else
    {
        const char *guest = tb_ball_guest_for_input(c, original_host, program);
        if (guest)
            working = tb_ball_host_path(c, tb_ball_drive_directory(c, guest[0] - 'A'), 0);
    }
    if (!working || !tb_is_dir(working))
    {
        tb_ball_fail(c, "Working directory must be a mapped directory");
        goto failed;
    }
    if (!runtime_features_validate(c, build) || !launch_write_runtime_config(c))
        goto failed;
    const char *override = field(request, "backend");
    const char *saved_backend = field(get(c->manifest, "runtime"), "backend");
    const char *wanted = override        ? override
                         : saved_backend ? saved_backend
                                         : getenv("TWOINE_BACKEND");
    const char *backend =
        runtime_effective_backend(c, build, wanted, !override && saved_backend != NULL);
    if (!backend)
        goto failed;
    cJSON *env = launch_host_environment(c, build, !strcmp(backend, "default") ? NULL : backend,
                                         get(c->application, "environment"));
    if (!env)
        goto failed;
    for (int i = 0; i < 26; i++)
        if (c->roots[i])
        {
            const char *directory = tb_ball_drive_directory(c, i);
            char *mapped = directory ? tb_ball_host_path(c, directory, 0) : NULL;
            if (!mapped || !tb_is_dir(mapped))
            {
                cJSON_Delete(env);
                tb_ball_fail(c, "Working directory for %c: is not accessible", 'A' + i);
                goto failed;
            }
            put(env, tb_ball_format(c, "TWOINE_DRIVE_CWD_%c", 'A' + i),
                cJSON_CreateString(directory + 3));
        }
    const char *guest_working = tb_ball_guest_for_input(c, working, cwd && *cwd ? cwd : program);
    if (!guest_working)
    {
        cJSON_Delete(env);
        goto failed;
    }
    put(env, "TWOINE_CURRENT_DRIVE", cJSON_CreateString(tb_ball_format(c, "%c", guest_working[0])));
    put(env, tb_ball_format(c, "TWOINE_DRIVE_CWD_%c", guest_working[0]),
        cJSON_CreateString(guest_working + 3));
    const char *identity = field(env, "TWOINE_RUNTIME_ID");
    const char *selected = field(env, "TWOINE_BACKEND");
    if (!selected)
        selected = getenv("TWOINE_BACKEND");
    if (!selected || !*selected)
        selected = "default";
    char *runtime = tb_ball_format(c, "%s/OS2/DLL/2INE/%s/%.16s", c->roots[2], selected, identity);
    if (!runtime || !runtime_sync(c, build, runtime))
    {
        cJSON_Delete(env);
        goto failed;
    }
    cJSON *command = cJSON_CreateArray();
    cJSON_AddItemToArray(command, cJSON_CreateString(tb_ball_join(c, runtime, loader)));
    cJSON *capabilities = runtime_feature_metadata(c, build);
    if (!capabilities)
    {
        cJSON_Delete(command);
        cJSON_Delete(env);
        goto failed;
    }
    int supervised = (!strcmp(action, "install") || cJSON_IsTrue(get(request, "startupService"))) &&
                     cJSON_IsTrue(get(capabilities, "operationSupervisor"));
    cJSON_Delete(capabilities);
    if (supervised)
        cJSON_AddItemToArray(command, cJSON_CreateString("--operation"));
    cJSON_AddItemToArray(command, cJSON_CreateString(tb_ball_guest_for_input(c, host, program)));
    cJSON *a;
    cJSON_ArrayForEach(a, argv) cJSON_AddItemToArray(command, cJSON_Duplicate(a, 1));
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "operationSupervised", supervised);
    cJSON_AddItemToObject(result, "command", command);
    cJSON_AddStringToObject(result, "cwd", tb_image_path(working) ? c->roots[2] : working);
    cJSON_AddStringToObject(result, "programHost", original_host);
    cJSON_AddItemToObject(
        result, "desktopIdentity",
        launch_desktop_identity(
            c, program, !strcmp(action, "launch") ? str(cJSON_GetArrayItem(args, 1)) : NULL));
    cJSON_AddItemToObject(result, "environment", env);
    cJSON *unset = cJSON_AddArrayToObject(result, "unsetEnvironment");
    for (int i = 0; launch_unset_environment_keys[i]; i++)
        cJSON_AddItemToArray(unset, cJSON_CreateString(launch_unset_environment_keys[i]));
    cJSON *receipt = cJSON_AddObjectToObject(result, "receipt");
    cJSON_AddItemToObject(receipt, "command", cJSON_Duplicate(command, 1));
    cJSON_AddStringToObject(receipt, "runtime_id", identity);
    cJSON_AddStringToObject(receipt, "cwd", working);
    cJSON_AddStringToObject(receipt, "started", tb_ball_now(c));
    if (!strcmp(action, "install"))
    {
        unsigned char digest[32];
        if (!tb_ball_digest_file(c, original_host, digest))
        {
            cJSON_Delete(result);
            goto failed;
        }
        cJSON_AddStringToObject(receipt, "program_sha256", tb_ball_hex(c, digest));
        cJSON_AddStringToObject(receipt, "program",
                                tb_ball_guest_for_input(c, original_host, program));
        if (tb_image_path(original_host))
        {
            char *source = tb_ball_keep(c, tb_image_source(original_host));
            if (!source)
            {
                cJSON_Delete(result);
                goto failed;
            }
            cJSON_AddStringToObject(receipt, "media_image", source);
        }
    }
    cJSON_AddBoolToObject(result, "installer", !strcmp(action, "install"));
    cJSON *startup = cJSON_AddArrayToObject(result, "startup");
    cJSON *services = get(c->manifest, "startupPrograms");
    if (!cJSON_IsFalse(get(request, "includeStartup")) && services)
    {
        if (!cJSON_IsArray(services) || cJSON_GetArraySize(services) > 16)
        {
            tb_ball_fail(c, "startupPrograms must contain at most 16 application records");
            cJSON_Delete(result);
            goto failed;
        }
        cJSON *service;
        cJSON_ArrayForEach(service, services)
        {
            const char *path = field(service, "program");
            if (!path || !*path || !cJSON_IsObject(service))
            {
                tb_ball_fail(c, "A startup program needs an executable path");
                cJSON_Delete(result);
                goto failed;
            }
            if (equal_case(path, program))
                continue;
            cJSON *launch = cJSON_Duplicate(service, 1);
            cJSON *arguments = cJSON_CreateArray();
            cJSON_AddItemToArray(arguments, cJSON_CreateString("run"));
            const char *directory = field(service, "cwd");
            if (directory && *directory)
            {
                cJSON_AddItemToArray(arguments, cJSON_CreateString("--cwd"));
                cJSON_AddItemToArray(arguments, cJSON_CreateString(directory));
            }
            cJSON_AddItemToArray(arguments, cJSON_CreateString(path));
            cJSON *extra = get(service, "arguments");
            if (extra && !tb_ball_array_strings(c, extra))
            {
                cJSON_Delete(arguments);
                cJSON_Delete(launch);
                cJSON_Delete(result);
                goto failed;
            }
            cJSON_ArrayForEach(a, extra) cJSON_AddItemToArray(arguments, cJSON_Duplicate(a, 1));
            put(launch, "arguments", arguments);
            put(launch, "includeStartup", cJSON_CreateFalse());
            put(launch, "startupService", cJSON_CreateTrue());
            put(launch, "build", cJSON_CreateString(build));
            put(launch, "backend", cJSON_CreateString(backend));
            const cJSON *application = c->application;
            cJSON *prepared = launch_prepare(c, launch);
            c->application = application;
            cJSON_Delete(launch);
            if (!prepared)
            {
                cJSON_Delete(result);
                goto failed;
            }
            cJSON_AddStringToObject(prepared, "program", path);
            if (field(service, "owner"))
                cJSON_AddStringToObject(prepared, "owner", field(service, "owner"));
            cJSON_AddItemToArray(startup, prepared);
        }
    }
    cJSON_Delete(argv);
    return result;
failed:
    cJSON_Delete(argv);
    return NULL;
}
