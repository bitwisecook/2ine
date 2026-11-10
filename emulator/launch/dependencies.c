/* Public domain. No rights reserved. Wholly AI-authored. */
/* Normal launches read the stored graph. No executable parsing, directory
   enumeration, media inspection, recovery, or database writes happen here. */
#include "ball/store.h"
#include "environment/dependencies/database.h"
#include "emulator/launch/dependencies.h"
#include "emulator/launch/plan.h"
#include "emulator/launch/prerequisites.h"
#include "environment/components/store.h"
#include "environment/dependencies/audit.h"
#include "environment/media/extract.h"
#include "environment/components/system_modules.h"

static const cJSON *dependency_cached_binary(TbBall *c, const cJSON *database, const char *path)
{
    const char *key = component_key(c, path);
    const cJSON *binary;
    cJSON_ArrayForEach(binary, get(database, "binaries")) if (equal_case(field(binary, "path"),
                                                                         key)) return binary;
    return NULL;
}

static char *dependency_cached_join(TbBall *c, const char *directory, const char *name)
{
    return component_key(c, tb_ball_format(c, "%s%s%s", directory,
                                           directory[strlen(directory) - 1] == '\\' ? "" : "\\",
                                           name));
}

static char *dependency_cached_program(TbBall *c, const cJSON *database, const char *program,
                                       const cJSON *env, const char *cwd, int host_program)
{
    if (host_program)
        program = tb_ball_guest_path(c, program);
    if (!program || !*program)
        return NULL;
    if (strlen(program) >= 3 && program[1] == ':' && (program[2] == '\\' || program[2] == '/'))
        return component_key(c, program);
    char *path = dependency_cached_join(c, cwd, program);
    if (dependency_cached_binary(c, database, path))
        return path;
    cJSON *paths = cJSON_CreateArray();
    dependency_split_paths(c, paths, field(env, "PATH"));
    dependency_split_paths(c, paths, field(env, "OS2PATH"));
    const cJSON *directory;
    cJSON_ArrayForEach(directory, paths)
    {
        path = dependency_cached_join(c, str(directory), program);
        if (dependency_cached_binary(c, database, path))
            break;
        path = NULL;
    }
    cJSON_Delete(paths);
    return path;
}

static const cJSON *dependency_cached_find(TbBall *c, const cJSON *database, const char *binary,
                                           const char *module, const cJSON *paths)
{
    char *folder = tb_ball_keep(c, strdup(binary)), *slash = folder ? strrchr(folder, '\\') : NULL;
    if (!slash)
        return NULL;
    *slash = 0;
    char *name = tb_ball_format(c, "%s%s", module, extract_extension(module, ".dll") ? "" : ".DLL");
    const cJSON *found =
        dependency_cached_binary(c, database, dependency_cached_join(c, folder, name));
    const cJSON *directory;
    cJSON_ArrayForEach(directory, paths)
    {
        if (found)
            break;
        found =
            dependency_cached_binary(c, database, dependency_cached_join(c, str(directory), name));
    }
    return found;
}

static void dependency_cached_process(TbBall *c, const cJSON *database, const cJSON *application,
                                      const cJSON *request, const char *program, const char *cwd,
                                      cJSON *report)
{
    cJSON *env = tb_ball_guest_environment(c, get(application, "environment"));
    if (!env)
    {
        dependency_take_error(c, get(report, "errors"), "Launch environment");
        return;
    }
    if (!cwd || !*cwd)
        cwd = tb_ball_drive_directory(c, strlen(program) > 1 && program[1] == ':'
                                             ? toupper((unsigned char)program[0]) - 'A'
                                             : 2);
    if (!cwd)
        cwd = "C:\\";
    char *selected = dependency_cached_program(c, database, program, env, cwd,
                                               cJSON_IsTrue(get(request, "hostProgram")));
    cJSON *roots = cJSON_CreateArray(), *queue = cJSON_CreateArray(), *seen = cJSON_CreateObject(),
          *paths = cJSON_CreateArray();
    dependency_split_paths(c, paths, field(env, "BEGINLIBPATH"));
    dependency_array_unique(paths, component_key(c, cwd));
    dependency_split_paths(c, paths, field(env, "LIBPATH"));
    dependency_split_paths(c, paths, field(env, "ENDLIBPATH"));
    const cJSON *path;
    cJSON_ArrayForEach(path, paths) dependency_array_unique(get(report, "searchPaths"), str(path));
    if (!selected)
        put(report, "unknown", cJSON_CreateTrue());
    else if (launch_command_file(program))
    {
        const cJSON *script = dependency_cached_binary(c, database, selected);
        const char *interpreter = field(env, "COMSPEC");
        if (!interpreter || !*interpreter)
            interpreter = "C:\\OS2\\CMD.EXE";
        dependency_array_unique(roots, component_key(c, interpreter));
        if (!script || !equal_case(field(script, "format"), "command-file"))
            put(report, "unknown", cJSON_CreateTrue());
        else if (cJSON_IsTrue(get(script, "rexx")))
        {
            dependency_array_unique(roots, "C:\\OS2\\DLL\\REXX.DLL");
            dependency_array_unique(roots, "C:\\OS2\\DLL\\REXXAPI.DLL");
        }
    }
    else
        dependency_array_unique(roots, selected);
    cJSON_ArrayForEach(path, roots)
    {
        dependency_array_unique(get(report, "roots"), str(path));
        dependency_array_unique(queue, str(path));
    }
    for (int at = 0; at < cJSON_GetArraySize(queue) && at < (int)DEPENDENCY_BINARY_LIMIT; at++)
    {
        const char *binary_path = str(cJSON_GetArrayItem(queue, at));
        const char *key = component_key(c, binary_path);
        if (get(seen, key))
            continue;
        cJSON_AddBoolToObject(seen, key, 1);
        const cJSON *binary = dependency_cached_binary(c, database, key);
        if (!binary || !cJSON_IsArray(get(binary, "modules")))
        {
            put(report, "unknown", cJSON_CreateTrue());
            continue;
        }
        if (cJSON_GetArraySize(get(report, "binaries")) >= (int)DEPENDENCY_BINARY_LIMIT)
        {
            put(report, "unknown", cJSON_CreateTrue());
            break;
        }
        cJSON *record = cJSON_Duplicate(binary, 1);
        cJSON_AddStringToObject(record, "processRoot", selected ? selected : program);
        cJSON_AddItemToArray(get(report, "binaries"), record);
        if (!cJSON_IsTrue(get(binary, "valid")))
            dependency_error(
                get(report, "errors"), key,
                "Cached executable metadata is invalid; rescan dependencies for details.");
        const cJSON *module;
        cJSON_ArrayForEach(module, get(binary, "modules"))
        {
            const char *name = str(module);
            if (!name || !dependency_module(c, name))
            {
                put(report, "unknown", cJSON_CreateTrue());
                continue;
            }
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "binary", key);
            cJSON_AddStringToObject(entry, "processRoot", selected ? selected : program);
            cJSON_AddStringToObject(entry, "module", name);
            const char *status = "provided";
            if (os2_module_disabled(c, name))
            {
                status = "disabled";
                dependency_array_unique(get(report, "disabled"), name);
            }
            else if (!os2_module_provided(c, name))
            {
                const cJSON *found = dependency_cached_find(c, database, key, name, paths);
                if (found)
                {
                    const char *format = field(found, "format");
                    status = cJSON_IsTrue(get(found, "valid")) &&
                                     (equal_case(format, "LX") || equal_case(format, "LE") ||
                                      equal_case(format, "NE"))
                                 ? "found"
                                 : "incompatible";
                    cJSON_AddStringToObject(entry, "path", field(found, "path"));
                    dependency_array_unique(queue, field(found, "path"));
                    if (!strcmp(status, "incompatible"))
                        dependency_error(get(report, "errors"), field(found, "path"),
                                         "Cached DLL metadata is not an OS/2 executable");
                }
                else
                {
                    cJSON *locations = cJSON_CreateArray();
                    const cJSON *other;
                    cJSON_ArrayForEach(
                        other,
                        get(database,
                            "binaries")) if (extract_extension(field(other, "path"), ".dll") &&
                                             equal_case(
                                                 dependency_basename(c, field(other, "path")),
                                                 name))
                        dependency_array_unique(locations, field(other, "path"));
                    status = cJSON_GetArraySize(locations) ? "unreachable" : "missing";
                    if (cJSON_GetArraySize(locations))
                    {
                        cJSON *location = cJSON_CreateObject();
                        cJSON_AddStringToObject(location, "module", name);
                        cJSON_AddItemToObject(location, "paths", locations);
                        cJSON_AddItemToArray(get(report, "unreachable"), location);
                    }
                    else
                    {
                        cJSON_Delete(locations);
                        dependency_array_unique(get(report, "missing"), name);
                    }
                }
            }
            cJSON_AddStringToObject(entry, "status", status);
            cJSON_AddItemToArray(get(report, "imports"), entry);
        }
    }
    cJSON_Delete(paths);
    cJSON_Delete(roots);
    cJSON_Delete(queue);
    cJSON_Delete(seen);
    cJSON_Delete(env);
}

cJSON *launch_dependencies(TbBall *parent, const cJSON *request)
{
    const cJSON *arguments = get(request, "arguments");
    if (!tb_ball_array_strings(parent, arguments) || !cJSON_GetArraySize(arguments))
        return NULL;
    TbBall storage = *parent;
    storage.allocations = NULL;
    storage.count = 0;
    storage.error = NULL;
    TbBall *c = &storage;
    cJSON *database = dependency_database_load(c), *report = cJSON_CreateObject();
    const char *arrays[] = {"roots",       "binaries",    "imports",  "missing",
                            "unreachable", "disabled",    "imported", "componentFiles",
                            "media",       "searchPaths", "errors",   "limits",
                            NULL};
    for (int i = 0; arrays[i]; i++)
        cJSON_AddArrayToObject(report, arrays[i]);
    cJSON_AddStringToObject(report, "scope", "launch");
    cJSON_AddBoolToObject(report, "cached", 1);
    cJSON_AddBoolToObject(report, "staticOnly", 1);
    cJSON_AddBoolToObject(report, "automaticRecovery", 0);
    cJSON_AddBoolToObject(report, "unknown",
                          database == NULL || cJSON_IsTrue(get(database, "unknown")));
    cJSON_AddBoolToObject(report, "stale", 0);
    cJSON_AddStringToObject(report, "logPath", "");
    cJSON_AddStringToObject(report, "jsonPath", "");
    cJSON_AddStringToObject(report, "indexGenerated", field(database, "generated"));
    const char *action = str(cJSON_GetArrayItem(arguments, 0)), *program = NULL, *cwd = NULL;
    const cJSON *application = request;
    if (!strcmp(action, "launch"))
    {
        const char *name = str(cJSON_GetArrayItem(arguments, 1));
        application = name ? get(get(c->manifest, "applications"), name) : NULL;
        program = field(application, "program");
        cwd = field(application, "cwd");
    }
    else if (!strcmp(action, "run") || !strcmp(action, "install"))
    {
        int at = 1;
        const char *option = str(cJSON_GetArrayItem(arguments, at));
        if (option && !strcmp(option, "--cwd"))
        {
            cwd = str(cJSON_GetArrayItem(arguments, at + 1));
            at += 2;
        }
        program = str(cJSON_GetArrayItem(arguments, at));
    }
    if (!program || !*program)
    {
        tb_ball_fail(parent, "Use run, install or launch with an application");
        cJSON_Delete(report);
        report = NULL;
        goto done;
    }
    cJSON_AddStringToObject(report, "selectedProgram", program);
    c->application = application;
    if (database)
        dependency_cached_process(c, database, application, request, program, cwd, report);
    if (database && !cJSON_IsFalse(get(request, "includeStartup")))
    {
        const cJSON *service;
        int count = 0;
        cJSON_ArrayForEach(service, get(c->manifest, "startupPrograms"))
        {
            if (++count > LAUNCH_MAX_STARTUP_PROGRAMS)
            {
                put(report, "unknown", cJSON_CreateTrue());
                break;
            }
            const char *path = field(service, "program");
            if (!path || !*path || equal_case(path, program))
                continue;
            c->application = service;
            dependency_cached_process(c, database, service, service, path, field(service, "cwd"),
                                      report);
        }
    }
    const cJSON *entry;
    int som = 0;
    cJSON_ArrayForEach(entry, get(report, "imports")) if (os2_module_matches(field(entry, "module"),
                                                                             "SOMD")) som = 1;
    if (som)
    {
        if (!cJSON_IsArray(get(database, "componentFiles")))
            put(report, "unknown", cJSON_CreateTrue());
        else
            put(report, "componentFiles", cJSON_Duplicate(get(database, "componentFiles"), 1));
    }
    cJSON_AddItemToObject(report, "componentGroups", dependency_component_groups(report));
    int missing_files = 0;
    cJSON_ArrayForEach(entry, get(report, "componentFiles")) missing_files +=
        equal_case(field(entry, "status"), "missing");
    int unknown = cJSON_IsTrue(get(report, "unknown"));
    cJSON_AddBoolToObject(report, "complete",
                          !unknown && !missing_files &&
                              !cJSON_GetArraySize(get(report, "missing")) &&
                              !cJSON_GetArraySize(get(report, "unreachable")) &&
                              !cJSON_GetArraySize(get(report, "disabled")) &&
                              !cJSON_GetArraySize(get(report, "errors")));
    cJSON_AddNumberToObject(report, "scannedBinaries", 0);
    cJSON_AddNumberToObject(report, "cachedBinaries", cJSON_GetArraySize(get(report, "binaries")));
    cJSON_AddStringToObject(
        report, "summary",
        unknown
            ? "Dependency status is unknown for part of this launch. Rescan Dependencies to update the saved index; launching does not scan or install files."
            : tb_ball_format(
                  c,
                  "Cached launch dependencies: %d executable/dependency records; %d missing libraries, %d unreachable imports, %d disabled libraries, %d missing package files and %d errors. This checks stored static imports; runtime-loaded APIs can still be missing.",
                  cJSON_GetArraySize(get(report, "binaries")),
                  cJSON_GetArraySize(get(report, "missing")),
                  cJSON_GetArraySize(get(report, "unreachable")),
                  cJSON_GetArraySize(get(report, "disabled")), missing_files,
                  cJSON_GetArraySize(get(report, "errors"))));
    dependency_array_unique(
        get(report, "limits"),
        "Stored static imports and verified companions only. No files or media are scanned at launch; external file changes, other dynamic loads and API/ordinal compatibility remain the loader's responsibility until an installation, added application, or explicit rescan refreshes the index.");
    dependency_take_error(c, get(report, "errors"), "Cached dependency index");
    if (cJSON_GetArraySize(get(report, "errors")))
        put(report, "complete", cJSON_CreateFalse());
done:
    cJSON_Delete(database);
    for (size_t i = 0; i < c->count; i++)
        free(c->allocations[i]);
    free(c->allocations);
    free(c->error);
    return report;
}
