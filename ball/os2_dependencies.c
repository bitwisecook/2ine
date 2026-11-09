/* Public domain. No rights reserved. Wholly AI-authored. */
/* Static OS/2 import auditing and media-backed dependency recovery. */
#include "ball/store.h"
#include "storage/container.h"
#include "ball/dependency_database.h"
#include "ball/dependency_freshness.h"
#include "ball/os2_component_store.h"
#include "ball/os2_dependencies.h"
#include "ball/os2_extract.h"
#include "ball/os2_groups.h"
#include "ball/os2_som.h"
#include "ball/os2_support.h"
#include "ball/system_modules.h"
#include "formats/executable/os2_executable.h"
#include "ball/vendor/sha256/sha256.h"
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif
#define DEPENDENCY_ROUND_LIMIT 16u

static int dependency_read_at(FILE *file, uint64_t length, uint64_t offset, void *output,
                              size_t count)
{
    return offset <= length && count <= length - offset && offset <= LONG_MAX &&
           !fseek(file, (long)offset, SEEK_SET) && fread(output, 1, count, file) == count;
}

char *dependency_module(TbBall *c, const char *name)
{
    size_t length = strlen(name);
    if (length > 4 && equal_case(name + length - 4, ".dll"))
        length -= 4;
    if (!length || length > 255)
        return NULL;
    char *module = tb_ball_keep(c, malloc(length + 1));
    if (!module)
        return NULL;
    for (size_t i = 0; i < length; i++)
    {
        unsigned ch = (unsigned char)name[i];
        if (ch < 33 || ch > 126 || strchr("/\\:;*?", (int)ch))
            return NULL;
        module[i] = (char)toupper(ch);
    }
    module[length] = 0;
    return module;
}

void dependency_error(cJSON *errors, const char *path, const char *message)
{
    const cJSON *existing;
    const char *where = path ? path : "", *why = message ? message : "Dependency audit failed";
    cJSON_ArrayForEach(existing, errors) if (!strcmp(field(existing, "path"), where) &&
                                             !strcmp(field(existing, "message"), why)) return;
    cJSON *error = cJSON_CreateObject();
    cJSON_AddStringToObject(error, "path", where);
    cJSON_AddStringToObject(error, "message", why);
    cJSON_AddItemToArray(errors, error);
}

void dependency_take_error(TbBall *c, cJSON *errors, const char *path)
{
    if (c->error)
    {
        dependency_error(errors, path, c->error);
        free(c->error);
        c->error = NULL;
    }
}

typedef struct DependencyImports
{
    TbBall *context;
    cJSON *modules;
} DependencyImports;

static int dependency_import(void *opaque, const char *name)
{
    DependencyImports *imports = opaque;
    char *module = dependency_module(imports->context, name);
    if (module)
        cJSON_AddItemToArray(imports->modules, cJSON_CreateString(module));
    return module != NULL;
}

static const char *dependency_import_error(TbExecutableFormat format, TbExecutableStatus status)
{
    int linear = format != TB_EXECUTABLE_NE;
    switch (status)
    {
        case TB_EXECUTABLE_OK:
            return NULL;
        case TB_EXECUTABLE_TRUNCATED_HEADER:
            return linear ? "Truncated or unsupported-endian linear executable header"
                          : "Truncated segmented executable header";
        case TB_EXECUTABLE_IMPORTS_OUTSIDE:
            return linear ? "Linear executable import table is outside the file"
                          : "Segmented executable import table is outside the file";
        case TB_EXECUTABLE_TRUNCATED_REFERENCE:
            return "Truncated segmented module reference";
        case TB_EXECUTABLE_TRUNCATED_IMPORT:
            return linear ? "Truncated linear executable import name"
                          : "Truncated segmented executable import name";
        case TB_EXECUTABLE_STOPPED:
            return "Unsupported module name in executable import table";
    }
    return "Dependency audit failed";
}

/* Only the counted import tables are read; no object pages, relocations or
   guest code are executed. */
static cJSON *dependency_read_binary(TbBall *c, const char *host, const char *guest, cJSON *errors)
{
    FILE *file = tb_media_path(host) ? tb_media_fopen(host) : tb_fopen(host, "rb");
    int64_t length = 0;
    int mode = 0;
    if (!file || !tb_file_info(file, &length, &mode) || length < 0)
    {
        if (file)
            fclose(file);
        dependency_error(errors, guest, "Cannot read executable metadata");
        return NULL;
    }
    cJSON *binary = cJSON_CreateObject(), *imports = cJSON_AddArrayToObject(binary, "modules");
    cJSON_AddStringToObject(binary, "path", guest);
    cJSON_AddStringToObject(binary, "hostPath", host);
    const char *format = "unsupported", *error = NULL;
    TbExecutable exe;
    if (!tb_executable_open_file(&exe, file, (uint64_t)length))
        error = "Cannot read executable header";
    else if (exe.format == TB_EXECUTABLE_DOS)
        format = "DOS";
    else if (!exe.header_size)
        error = "Executable header offset is outside the file";
    else if (exe.format == TB_EXECUTABLE_LX || exe.format == TB_EXECUTABLE_LE)
    {
        int lx = exe.format == TB_EXECUTABLE_LX;
        format = lx ? "LX" : "LE";
        if (tb_executable_linear_header(&exe) && tb_executable_target_os(&exe) != TB_LX_OS_OS2)
            format = lx ? "LX-other" : "LE-other";
        else
            error = dependency_import_error(
                exe.format,
                tb_executable_imports(&exe, dependency_import, &(DependencyImports){c, imports}));
    }
    else if (exe.format == TB_EXECUTABLE_NE)
    {
        format = "NE";
        unsigned target = tb_executable_target_os(&exe);
        if (exe.header_size >= TB_NE_HEADER_SIZE && target > TB_NE_TARGET_OS2)
            format = target == 2 ? "NE-Windows" : "NE-other";
        else
            error = dependency_import_error(
                exe.format,
                tb_executable_imports(&exe, dependency_import, &(DependencyImports){c, imports}));
    }
    else if (exe.format == TB_EXECUTABLE_PE)
        format = "PE";
    fclose(file);
    cJSON_AddStringToObject(binary, "format", format);
    cJSON_AddBoolToObject(binary, "valid", error == NULL);
    if (error)
    {
        dependency_error(errors, guest, error);
        cJSON_DeleteItemFromObjectCaseSensitive(binary, "modules");
        cJSON_AddArrayToObject(binary, "modules");
    }
    const char *base = strrchr(guest, '\\');
    base = base ? base + 1 : guest;
    if (!error && equal_case(base, "HELPMGR.DLL") &&
        (equal_case(format, "LX") || equal_case(format, "LE") || equal_case(format, "NE")))
    {
        /* Original HELPMGR loads this resource DLL by name at runtime. */
        cJSON *companions = cJSON_AddArrayToObject(binary, "companions");
        cJSON_AddItemToArray(companions, cJSON_CreateString("HPMGRMRI"));
    }
    dependency_take_error(c, errors, guest);
    return binary;
}

int dependency_candidate(const char *name)
{
    return extract_extension(name, ".exe") || extract_extension(name, ".dll") ||
           extract_extension(name, ".drv") || extract_extension(name, ".pdr") ||
           (extract_extension(name, ".sys") && !equal_case(name, "CONFIG.SYS")) ||
           extract_extension(name, ".ocx");
}

void dependency_array_unique(cJSON *array, const char *value)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, array) if (equal_case(str(item), value)) return;
    cJSON_AddItemToArray(array, cJSON_CreateString(value));
}

char *dependency_basename(TbBall *c, const char *path)
{
    const char *base = strrchr(path, '\\');
    if (!base)
        base = strrchr(path, '/');
    return dependency_module(c, base ? base + 1 : path);
}

static void dependency_index_binary(TbBall *c, cJSON *binaries, cJSON *index, cJSON *seen,
                                    cJSON *errors, const char *host, const char *guest)
{
    char *identity = tb_ball_keep(c, strdup(guest));
    if (!identity)
        return;
    for (char *p = identity; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    if (get(seen, identity))
        return;
    cJSON_AddBoolToObject(seen, identity, 1);
    cJSON *binary = dependency_read_binary(c, host, guest, errors);
    if (!binary)
        return;
    cJSON_AddItemToArray(binaries, binary);
    char *name = dependency_basename(c, guest);
    if (name && extract_extension(guest, ".dll"))
    {
        cJSON *locations = get(index, name);
        if (!locations)
            locations = cJSON_AddArrayToObject(index, name);
        dependency_array_unique(locations, guest);
    }
}

static void dependency_index_script(TbBall *c, cJSON *binaries, cJSON *seen, cJSON *errors,
                                    const char *host, const char *guest)
{
    const char *key = component_key(c, guest);
    if (get(seen, key))
        return;
    cJSON_AddBoolToObject(seen, key, 1);
    FILE *script = tb_media_path(host) ? tb_media_fopen(host) : tb_fopen(host, "rb");
    if (!script)
    {
        dependency_error(errors, guest, "Cannot index command-file metadata");
        return;
    }
    int rexx = fgetc(script) == '/' && fgetc(script) == '*';
    fclose(script);
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "path", guest);
    cJSON_AddStringToObject(entry, "format", "command-file");
    cJSON_AddBoolToObject(entry, "valid", 1);
    cJSON_AddBoolToObject(entry, "rexx", rexx);
    cJSON_AddArrayToObject(entry, "modules");
    cJSON_AddItemToArray(binaries, entry);
}

static cJSON *dependency_scan(TbBall *c, cJSON *index, cJSON *seen, cJSON *errors, cJSON *limits)
{
    cJSON *binaries = cJSON_CreateArray(), *queue = cJSON_CreateArray();
    cJSON_AddItemToArray(queue, cJSON_CreateString("C:\\"));
    unsigned visited = 0;
    for (int at = 0; at < cJSON_GetArraySize(queue); at++)
    {
        const char *guest = str(cJSON_GetArrayItem(queue, at));
        char *host = tb_ball_host_path(c, guest, 0);
        if (!host)
        {
            dependency_take_error(c, errors, guest);
            continue;
        }
        size_t count = 0;
        char **names = tb_names(host, &count);
        if (count == (size_t)-1)
        {
            dependency_error(errors, guest, "Cannot read installed directory");
            continue;
        }
        for (size_t i = 0; i < count; i++)
        {
            if (++visited > DEPENDENCY_ENTRY_LIMIT ||
                cJSON_GetArraySize(binaries) >= DEPENDENCY_BINARY_LIMIT)
                break;
            char *child_host = tb_ball_join(c, host, names[i]);
            char *child_guest = tb_ball_format(
                c, "%s%s%s", guest, guest[strlen(guest) - 1] == '\\' ? "" : "\\", names[i]);
            if (!child_host || !child_guest)
                break;
            if (tb_is_link(child_host))
            {
                dependency_error(errors, child_guest,
                                 "Symbolic link skipped during installed-file audit");
                continue;
            }
            if (tb_is_dir(child_host))
            {
                if (!equal_case(child_guest, "C:\\OS2\\DLL\\2INE"))
                    cJSON_AddItemToArray(queue, cJSON_CreateString(child_guest));
            }
            else if (dependency_candidate(names[i]))
                dependency_index_binary(c, binaries, index, seen, errors, child_host, child_guest);
            else if (extract_extension(names[i], ".cmd") || extract_extension(names[i], ".bat"))
                dependency_index_script(c, binaries, seen, errors, child_host, child_guest);
        }
        tb_names_free(names, count);
        if (visited > DEPENDENCY_ENTRY_LIMIT ||
            cJSON_GetArraySize(binaries) >= DEPENDENCY_BINARY_LIMIT)
        {
            dependency_array_unique(
                limits, "Installed scan reached its 100000-entry or 30000-binary limit.");
            dependency_error(errors, guest, "Installed scan limit reached; audit is incomplete");
            break;
        }
        dependency_take_error(c, errors, guest);
    }
    cJSON_Delete(queue);
    return binaries;
}

static void dependency_add_path(TbBall *c, cJSON *paths, const char *path)
{
    if (!path || !*path || !strcmp(path, "."))
        return;
    char *copy = tb_ball_keep(c, strdup(path));
    if (!copy)
        return;
    size_t length = strlen(copy);
    while (length > 3 && (copy[length - 1] == '\\' || copy[length - 1] == '/'))
        copy[--length] = 0;
    dependency_array_unique(paths, copy);
}

void dependency_split_paths(TbBall *c, cJSON *paths, const char *value)
{
    char *copy = value ? tb_ball_keep(c, strdup(value)) : NULL;
    for (char *at = copy; at && *at;)
    {
        char *end = strchr(at, ';');
        if (end)
            *end++ = 0;
        dependency_add_path(c, paths, tb_ball_trim(at));
        at = end;
    }
}

cJSON *dependency_search_paths(TbBall *c, cJSON *errors)
{
    cJSON *paths = cJSON_CreateArray();
    cJSON *env = tb_ball_guest_environment(c, NULL);
    if (env)
    {
        dependency_split_paths(c, paths, field(env, "BEGINLIBPATH"));
        dependency_split_paths(c, paths, field(env, "LIBPATH"));
        dependency_split_paths(c, paths, field(env, "ENDLIBPATH"));
        cJSON_Delete(env);
    }
    dependency_take_error(c, errors, "CONFIG.SYS");
    return paths;
}

char *dependency_find(TbBall *c, const char *binary, const char *module, cJSON *paths, cJSON *cache,
                      cJSON *errors)
{
    char *directory = tb_ball_keep(c, strdup(binary));
    if (!directory)
        return NULL;
    char *slash = strrchr(directory, '\\');
    if (slash)
        *slash = 0;
    char *key = tb_ball_format(c, "%s|%s", directory, module);
    if (!key)
        return NULL;
    const cJSON *cached = get(cache, key);
    if (cached)
        return str(cached) ? tb_ball_keep(c, strdup(str(cached))) : NULL;
    cJSON *search = cJSON_CreateArray();
    dependency_array_unique(search, directory);
    const cJSON *item;
    cJSON_ArrayForEach(item, paths) dependency_array_unique(search, str(item));
    char *found = NULL;
    cJSON_ArrayForEach(item, search)
    {
        const char *path = str(item);
        char *guest =
            tb_ball_format(c, "%s%s%s%s", path, path[strlen(path) - 1] == '\\' ? "" : "\\", module,
                           extract_extension(module, ".dll") ? "" : ".DLL");
        char *host = guest ? tb_ball_host_path(c, guest, 0) : NULL;
        if (host && tb_is_file(host))
        {
            found = guest;
            break;
        }
        /* Ejected/missing external library paths do not invalidate an audit;
           the unresolved module is recorded below with its search locations. */
        if (c->error)
        {
            free(c->error);
            c->error = NULL;
        }
    }
    cJSON_Delete(search);
    cJSON_AddItemToObject(cache, key, found ? cJSON_CreateString(found) : cJSON_CreateNull());
    dependency_take_error(c, errors, binary);
    return found;
}

static cJSON *dependency_resolve(TbBall *c, cJSON *binaries, cJSON *index, cJSON *seen,
                                 cJSON *paths, cJSON *errors, cJSON *missing, cJSON *unreachable,
                                 cJSON *unresolved)
{
    cJSON *imports = cJSON_CreateArray();
    cJSON *cache = cJSON_CreateObject();
    /* Appending discovered external dependencies follows their import closure.
       The seen set prevents cycles from duplicating work. */
    for (int at = 0; at < cJSON_GetArraySize(binaries); at++)
    {
        if (at >= (int)DEPENDENCY_BINARY_LIMIT)
        {
            dependency_error(errors, "", "Dependency closure reached its binary limit");
            break;
        }
        const cJSON *binary = cJSON_GetArrayItem(binaries, at), *module;
        const char *binary_path = field(binary, "path");
        cJSON *requirements = cJSON_Duplicate(get(binary, "modules"), 1);
        int static_count = cJSON_GetArraySize(requirements), requirement = 0;
        cJSON_ArrayForEach(module, get(binary, "companions"))
            dependency_array_unique(requirements, str(module));
        cJSON_ArrayForEach(module, requirements)
        {
            const char *name = str(module);
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "binary", binary_path);
            cJSON_AddStringToObject(entry, "module", name);
            if (requirement++ >= static_count)
                cJSON_AddStringToObject(entry, "requirement", "verified-runtime-companion");
            const char *status = "provided";
            if (os2_module_disabled(c, name))
            {
                status = "disabled";
                cJSON_AddStringToObject(
                    entry, "reason",
                    "Disabled in this ball's Graphics & Media settings; media recovery is suppressed.");
            }
            else if (!os2_module_provided(c, name))
            {
                char *found = dependency_find(c, binary_path, name, paths, cache, errors);
                if (found)
                {
                    status = "found";
                    cJSON_AddStringToObject(entry, "path", found);
                    char *host = tb_ball_host_path(c, found, 0);
                    if (host)
                        dependency_index_binary(c, binaries, index, seen, errors, host, found);
                    dependency_take_error(c, errors, found);
                    const cJSON *resolved = NULL, *known;
                    cJSON_ArrayForEach(known, binaries) if (equal_case(field(known, "path"), found))
                    {
                        resolved = known;
                        break;
                    }
                    const char *format = resolved ? field(resolved, "format") : NULL;
                    if (!resolved || !cJSON_IsTrue(get(resolved, "valid")) ||
                        (!equal_case(format, "LX") && !equal_case(format, "LE") &&
                         !equal_case(format, "NE")))
                    {
                        status = "incompatible";
                        cJSON_AddStringToObject(entry, "reason",
                                                "Resolved DLL is not a readable OS/2 executable");
                        dependency_error(
                            errors, found,
                            "Resolved DLL is not a readable OS/2 executable; existing file preserved");
                    }
                }
                else
                {
                    const cJSON *locations = get(index, name);
                    status = locations ? "unreachable" : "missing";
                    dependency_array_unique(unresolved, name);
                    if (locations)
                    {
                        cJSON *location = cJSON_CreateObject();
                        cJSON_AddStringToObject(location, "module", name);
                        cJSON_AddItemToObject(location, "paths", cJSON_Duplicate(locations, 1));
                        cJSON_AddItemToArray(unreachable, location);
                        cJSON_AddStringToObject(
                            entry, "reason",
                            "Installed outside this binary's directory and LIBPATH");
                    }
                    else
                        dependency_array_unique(missing, name);
                }
            }
            cJSON_AddStringToObject(entry, "status", status);
            cJSON_AddItemToArray(imports, entry);
        }
        cJSON_Delete(requirements);
    }
    cJSON_Delete(cache);
    return imports;
}

static void dependency_source(cJSON *sources, const char *path, int remembered)
{
    cJSON *item;
    cJSON_ArrayForEach(item, sources) if (equal_case(field(item, "path"), path))
    {
        if (remembered)
            put(item, "remembered", cJSON_CreateBool(1));
        return;
    }
    item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "path", path);
    cJSON_AddBoolToObject(item, "remembered", remembered);
    cJSON_AddStringToObject(item, "status", "not-inspected");
    cJSON_AddItemToArray(sources, item);
}

static cJSON *dependency_sources(TbBall *c, const cJSON *request, cJSON *errors)
{
    cJSON *sources = cJSON_CreateArray();
    size_t size = 0;
    char *bytes = tb_ball_read_file(c, tb_ball_join(c, c->ball, "os2-media.json"), &size, 1);
    cJSON *remembered = bytes && size ? cJSON_ParseWithLength(bytes, size) : NULL;
    if (size && (!cJSON_IsObject(remembered) || !cJSON_IsArray(get(remembered, "sources"))))
        dependency_error(errors, "os2-media.json", "Cannot read remembered OS/2 media record");
    const cJSON *item;
    cJSON_ArrayForEach(item, get(remembered, "sources")) if (str(item))
        dependency_source(sources, str(item), 1);
    cJSON_Delete(remembered);
    dependency_take_error(c, errors, "os2-media.json");
    cJSON_ArrayForEach(item, get(request, "sources")) if (str(item))
        dependency_source(sources, str(item), 1);
    cJSON_ArrayForEach(item, get(c->manifest, "drives"))
    {
        if (!item->string || item->string[0] == 'C')
            continue;
        const char *path = field(item, "image");
        if (path && *path)
        {
            char *absolute = host_absolute(path) ? tb_ball_keep(c, strdup(path))
                                                 : tb_ball_join(c, c->ball, path);
            if (absolute)
                dependency_source(sources, absolute, 0);
        }
        else if (str(item) && *str(item))
        {
            int drive = item->string[0] - 'A';
            if (drive >= 0 && drive < TB_BALL_DRIVE_COUNT && c->roots[drive] &&
                !c->unavailable[drive])
                dependency_source(sources, c->roots[drive], 0);
        }
    }
    dependency_take_error(c, errors, "Mapped installation media");
    return sources;
}

static int dependency_possible_media(TbBall *c, const char *source)
{
    cJSON *listing = browse_media(c, source);
    if (!listing)
        return 0;
    int possible = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, get(listing, "entries"))
    {
        const char *name = field(entry, "name");
        if (equal_case(name, "OS2IMAGE") || equal_case(name, "OS2") || equal_case(name, "DISK_0") ||
            equal_case(name, "DISK_1") || equal_case(name, "DISK_2") ||
            equal_case(name, "CMD.EXE") || equal_case(name, "SYSLEVEL.OS2"))
            possible = 1;
    }
    cJSON_Delete(listing);
    return possible;
}

static int dependency_source_accessible(const char *source)
{
    if (!tb_media_path(source))
        return tb_is_file(source) || tb_is_dir(source);
    size_t count = 0;
    TbMediaEntry *entries = tb_media_entries(source, &count);
    tb_media_entries_free(entries, count == (size_t)-1 ? 0 : count);
    if (count != (size_t)-1)
        return 1;
    char *parent = tb_media_parent(source);
    entries = parent ? tb_media_entries(parent, &count) : NULL;
    int found = 0;
    for (size_t i = 0; parent && count != (size_t)-1 && i < count && !found; i++)
    {
        char *child = tb_media_child(parent, entries[i].name);
        found = child && !strcmp(child, source);
        free(child);
    }
    tb_media_entries_free(entries, count == (size_t)-1 ? 0 : count);
    free(parent);
    return found;
}

static char *dependency_installation_root(TbBall *c, const char *source)
{
    cJSON *listing = browse_media(c, source);
    if (!listing)
    {
        free(c->error);
        c->error = NULL;
        return tb_ball_keep(c, strdup(source));
    }
    char *result = NULL;
    const cJSON *entry;
    cJSON_ArrayForEach(entry,
                       get(listing, "entries")) if (cJSON_IsTrue(get(entry, "directory")) &&
                                                    equal_case(field(entry, "name"), "OS2IMAGE"))
    {
        result = tb_ball_keep(c, strdup(field(entry, "path")));
        break;
    }
    cJSON_Delete(listing);
    return result ? result : tb_ball_keep(c, strdup(source));
}

static void dependency_index_media(TbBall *c, cJSON *index, const cJSON *plan)
{
    const cJSON *file;
    cJSON_ArrayForEach(file, get(plan, "files"))
    {
        const char *path = field(file, "path");
        if ((!extract_extension(path, ".dll") && !equal_case(path, "C:\\OS2\\VIEW.EXE") &&
             !equal_case(path, "C:\\OS2\\VIEWDOC.EXE") && !os2_som_companion(path)) ||
            cJSON_IsTrue(get(file, "provided")))
            continue;
        char *name = dependency_basename(c, path);
        if (!name)
            continue;
        cJSON *files = get(index, name);
        if (!files)
            files = cJSON_AddArrayToObject(index, name);
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "path", path);
        cJSON_AddStringToObject(entry, "source", field(file, "source"));
        cJSON_AddNumberToObject(entry, "priority", get(file, "priority")->valuedouble);
        cJSON_AddItemToArray(files, entry);
    }
}

static void dependency_merge_media(cJSON *destination, const cJSON *source)
{
    const cJSON *module, *file;
    cJSON_ArrayForEach(module, source)
    {
        cJSON *items = get(destination, module->string);
        if (!items)
            items = cJSON_AddArrayToObject(destination, module->string);
        cJSON_ArrayForEach(file, module)
        {
            int duplicate = 0;
            const cJSON *existing;
            cJSON_ArrayForEach(existing, items) if (!strcmp(field(existing, "source"),
                                                            field(file, "source"))) duplicate = 1;
            if (!duplicate)
                cJSON_AddItemToArray(items, cJSON_Duplicate(file, 1));
        }
    }
}

/* Image indexes are only an optimization. Bind them to the physical media's
   size, timestamp and both end blocks, then check each chosen OS/2 binary
   again before extraction. Folder trees remain request-scoped. */
static char *dependency_media_fingerprint(TbBall *c, const char *source)
{
    if (tb_media_path(source) && !tb_media_container(source))
        return NULL;
    char *physical = tb_ball_keep(c, tb_media_host_source(source));
    if (!physical || !tb_is_file(physical))
    {
        free(c->error);
        c->error = NULL;
        return NULL;
    }
    FILE *file = tb_fopen(physical, "rb");
    int64_t size = 0;
    int mode = 0;
    if (!file || !tb_file_info(file, &size, &mode) || size < 0)
    {
        if (file)
            fclose(file);
        return NULL;
    }
#ifdef _WIN32
    struct _stat64 info;
    int ok = !_fstat64(tb_file_descriptor(file), &info);
    long nanoseconds = 0;
#else
    struct stat info;
    int ok = !fstat(tb_file_descriptor(file), &info);
#if defined(__APPLE__) && defined(__DARWIN_C_LEVEL) && __DARWIN_C_LEVEL >= __DARWIN_C_FULL
    long nanoseconds = ok ? info.st_mtimespec.tv_nsec : 0;
#elif defined(__APPLE__)
    long nanoseconds = ok ? info.st_mtimensec : 0;
#else
    long nanoseconds = ok ? info.st_mtim.tv_nsec : 0;
#endif
#endif
    SHA256_CTX state;
    sha256_init(&state);
    unsigned char block[65536], digest[32];
    size_t amount = size < (int64_t)sizeof block ? (size_t)size : sizeof block;
    if (ok)
        ok = dependency_read_at(file, (uint64_t)size, 0, block, amount);
    if (ok)
        sha256_update(&state, block, amount);
    if (ok && size > (int64_t)amount)
    {
        ok = dependency_read_at(file, (uint64_t)size, (uint64_t)size - amount, block, amount);
        if (ok)
            sha256_update(&state, block, amount);
    }
    fclose(file);
    if (!ok)
        return NULL;
    sha256_final(&state, digest);
    return tb_ball_format(c, "%lld:%lld:%ld:%s", (long long)size, (long long)info.st_mtime,
                          nanoseconds, tb_ball_hex(c, digest));
}

static char *dependency_cache_path(TbBall *c, const char *source, const char *variant)
{
    SHA256_CTX state;
    unsigned char digest[32];
    sha256_init(&state);
    sha256_update(&state, (const BYTE *)source, strlen(source));
    sha256_final(&state, digest);
    return tb_ball_format(c, "%s/cache/dependency-media-v2-%s-%s.json", c->ball,
                          tb_ball_hex(c, digest), variant);
}

static cJSON *dependency_cached_media(TbBall *c, const char *source, const char *variant,
                                      const char *fingerprint, int *command, int *level)
{
    if (!fingerprint)
        return NULL;
    size_t size = 0;
    char *bytes = tb_ball_read_file(c, dependency_cache_path(c, source, variant), &size, 1);
    if (c->error)
    {
        free(c->error);
        c->error = NULL;
        return NULL;
    }
    cJSON *cache = bytes && size ? cJSON_ParseWithLength(bytes, size) : NULL;
    if (!cache || !equal_case(field(cache, "source"), source) || !field(cache, "fingerprint") ||
        strcmp(field(cache, "fingerprint"), fingerprint) || !cJSON_IsObject(get(cache, "index")))
    {
        cJSON_Delete(cache);
        return NULL;
    }
    char *root = tb_ball_keep(c, tb_media_root(source));
    const cJSON *module, *file;
    int valid = root != NULL;
    cJSON_ArrayForEach(module, get(cache, "index"))
    {
        if (!module->string || !cJSON_IsArray(module))
        {
            valid = 0;
            break;
        }
        cJSON_ArrayForEach(file, module)
        {
            const char *path = field(file, "path"), *member = field(file, "source");
            char *name = path ? dependency_basename(c, path) : NULL;
            const cJSON *priority = get(file, "priority");
            if (!path || !member || !name || !equal_case(name, module->string) || !priority ||
                !cJSON_IsNumber(priority) || priority->valuedouble < 0 ||
                priority->valuedouble > 3 || !root || strncmp(member, root, strlen(root)))
            {
                valid = 0;
                break;
            }
        }
    }
    cJSON *index = valid ? cJSON_Duplicate(get(cache, "index"), 1) : NULL;
    if (valid)
    {
        *command = cJSON_IsTrue(get(cache, "commandSignature"));
        *level = cJSON_IsTrue(get(cache, "versionSignature"));
    }
    cJSON_Delete(cache);
    return index;
}

static void dependency_cache_media(TbBall *c, const char *source, const char *variant,
                                   const char *fingerprint, int command, int level,
                                   const cJSON *index)
{
    if (!fingerprint)
        return;
    cJSON *cache = cJSON_CreateObject();
    cJSON_AddStringToObject(cache, "source", source);
    cJSON_AddStringToObject(cache, "fingerprint", fingerprint);
    cJSON_AddBoolToObject(cache, "commandSignature", command);
    cJSON_AddBoolToObject(cache, "versionSignature", level);
    cJSON_AddItemToObject(cache, "index", cJSON_Duplicate(index, 1));
    char *directory = tb_ball_join(c, c->ball, "cache");
    if (directory && tb_mkdirs(directory))
        tb_ball_write_json(c, dependency_cache_path(c, source, variant), cache);
    cJSON_Delete(cache);
    if (c->error)
    {
        free(c->error);
        c->error = NULL;
    }
}

static cJSON *dependency_verified_sources(TbBall *c, cJSON *sources, cJSON *errors,
                                          cJSON *media_index, int cache_media)
{
    cJSON *eligible = cJSON_CreateArray(), *indexes = cJSON_CreateObject();
    int remembered_cmd = 0, remembered_level = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, sources)
    {
        const char *path = field(item, "path");
        int remembered = cJSON_IsTrue(get(item, "remembered"));
        if (!dependency_source_accessible(path))
        {
            put(item, "status", cJSON_CreateString("unavailable"));
            put(item, "reason",
                cJSON_CreateString("Remembered or attached media is no longer available"));
            continue;
        }
        if (cache_media && !remembered && !dependency_possible_media(c, path))
        {
            put(item, "status", cJSON_CreateString(c->error ? "unavailable" : "not-os2-media"));
            put(item, "reason",
                cJSON_CreateString(c->error ? c->error
                                            : "No OS/2 installation layout at this source"));
            free(c->error);
            c->error = NULL;
            continue;
        }
        char *search_root = dependency_installation_root(c, path);
        put(item, "searchRoot", cJSON_CreateString(search_root));
        if (!strcmp(search_root, path))
            cJSON_AddBoolToObject(item, "secondaryIndexed", 1);
        char *fingerprint = dependency_media_fingerprint(c, path);
        if (fingerprint)
            cJSON_AddStringToObject(item, "fingerprint", fingerprint);
        int command = 0, level = 0;
        cJSON *source_index =
            cache_media ? dependency_cached_media(c, path, "core", fingerprint, &command, &level)
                        : NULL;
        put(item, "coreIndexCache",
            cJSON_CreateString(!cache_media   ? "disabled"
                               : source_index ? "hit"
                                              : "miss"));
        if (!source_index)
        {
            cJSON *request = cJSON_CreateObject(),
                  *selected = cJSON_AddArrayToObject(request, "sources");
            cJSON_AddItemToArray(selected, cJSON_CreateString(search_root));
            cJSON *plan = plan_os2_extraction(c, request);
            cJSON_Delete(request);
            if (!plan)
            {
                put(item, "status", cJSON_CreateString("unavailable"));
                put(item, "reason",
                    cJSON_CreateString(c->error ? c->error : "Cannot inspect installation media"));
                dependency_take_error(c, errors, path);
                continue;
            }
            const cJSON *file;
            cJSON_ArrayForEach(file, get(plan, "files"))
            {
                const char *destination = field(file, "path");
                int index = equal_case(destination, support_paths[0])   ? 0
                            : equal_case(destination, support_paths[1]) ? 1
                                                                        : -1;
                if (index < 0 || (index == 0 && command) || (index == 1 && level))
                    continue;
                SupportFile signature = {0};
                if (support_read(c, field(file, "source"), index, &signature))
                {
                    command |= index == 0;
                    level |= index == 1;
                }
                free(signature.data);
                free(signature.source);
                free(c->error);
                c->error = NULL;
            }
            source_index = cJSON_CreateObject();
            dependency_index_media(c, source_index, plan);
            cJSON_Delete(plan);
            if (cache_media)
                dependency_cache_media(c, path, "core", fingerprint, command, level, source_index);
        }
        cJSON_AddItemToObject(indexes, path, source_index);
        cJSON_AddBoolToObject(item, "commandSignature", command);
        cJSON_AddBoolToObject(item, "versionSignature", level);
        put(item, "status",
            cJSON_CreateString(command && level ? "verified-os2-media" : "unverified"));
        if (remembered)
        {
            remembered_cmd |= command;
            remembered_level |= level;
        }
        if (command && level)
            dependency_array_unique(eligible, path);
    }
    if (remembered_cmd && remembered_level)
        cJSON_ArrayForEach(item, sources) if (cJSON_IsTrue(get(item, "remembered")) &&
                                              equal_case(field(item, "status"), "unverified"))
        {
            put(item, "status", cJSON_CreateString("verified-os2-set"));
            dependency_array_unique(eligible, field(item, "path"));
        }
    cJSON_ArrayForEach(item, sources) if (equal_case(field(item, "status"), "verified-os2-media") ||
                                          equal_case(field(item, "status"), "verified-os2-set"))
        dependency_merge_media(media_index, get(indexes, field(item, "path")));
    cJSON_Delete(indexes);
    return eligible;
}

cJSON *validate_os2_media(TbBall *c, const cJSON *request)
{
    const cJSON *requested = get(request, "sources"), *source;
    if (!tb_ball_array_strings(c, requested) || !cJSON_GetArraySize(requested))
    {
        tb_ball_fail(c, "Choose OS/2 installation media");
        return NULL;
    }
    cJSON *opened = cJSON_CreateObject(), *normalized = cJSON_AddArrayToObject(opened, "sources"),
          *skipped = cJSON_AddArrayToObject(opened, "skippedSources");
    /* Normalize each explicit input so finding an ISO in one archive cannot
       silently omit a separately selected folder, floppy or archive. */
    cJSON_ArrayForEach(source, requested)
    {
        cJSON *one_request = cJSON_CreateObject(),
              *one_sources = cJSON_AddArrayToObject(one_request, "sources");
        cJSON_AddItemToArray(one_sources, cJSON_Duplicate(source, 1));
        cJSON *one = open_os2_source(c, one_request);
        cJSON_Delete(one_request);
        if (!one)
        {
            cJSON_Delete(opened);
            return NULL;
        }
        const cJSON *item;
        cJSON_ArrayForEach(item, get(one, "sources"))
            dependency_array_unique(normalized, str(item));
        cJSON_ArrayForEach(item, get(one, "skippedSources"))
            cJSON_AddItemToArray(skipped, cJSON_Duplicate(item, 1));
        cJSON_Delete(one);
    }
    cJSON *result = cJSON_CreateObject(), *media = cJSON_AddArrayToObject(result, "media"),
          *errors = cJSON_AddArrayToObject(result, "errors"), *index = cJSON_CreateObject(),
          *physical = cJSON_AddArrayToObject(result, "physicalSources");
    cJSON *installation_set = cJSON_CreateArray();
    cJSON_ArrayForEach(source, get(opened, "sources"))
    {
        cJSON *siblings = cJSON_CreateArray();
        cJSON_AddItemToArray(siblings, cJSON_Duplicate(source, 1));
        if (extract_expand_installation_roots(c, siblings))
        {
            const cJSON *sibling;
            cJSON_ArrayForEach(sibling, siblings)
                dependency_array_unique(installation_set, str(sibling));
        }
        cJSON_Delete(siblings);
    }
    cJSON_ArrayForEach(source, get(opened, "sources"))
    {
        /* The explicitly selected images form one installation set, using
           the same combined-signature rule as remembered floppy sets. Only
           installation layouts and their discovered siblings join that set;
           unrelated application media cannot inherit another CD's proof. */
        int member = dependency_possible_media(c, str(source));
        const cJSON *known;
        cJSON_ArrayForEach(known, installation_set) if (!strcmp(str(known), str(source))) member =
            1;
        dependency_source(media, str(source), member);
        char *host = tb_ball_keep(c, tb_media_host_source(str(source)));
        if (host)
            dependency_array_unique(physical, host);
    }
    cJSON *eligible = dependency_verified_sources(c, media, errors, index, 0);
    cJSON_AddItemToObject(result, "sources", eligible);
    int complete = cJSON_GetArraySize(eligible) > 0 && !cJSON_GetArraySize(errors);
    cJSON *item;
    cJSON_ArrayForEach(item, media)
    {
        cJSON_DeleteItemFromObjectCaseSensitive(item, "remembered");
        cJSON_AddBoolToObject(item, "selected", 1);
        const char *status = field(item, "status");
        if (!equal_case(status, "verified-os2-media") && !equal_case(status, "verified-os2-set"))
            complete = 0;
    }
    cJSON_AddBoolToObject(result, "complete", complete);
    cJSON_AddItemToObject(result, "skippedSources",
                          cJSON_Duplicate(get(opened, "skippedSources"), 1));
    cJSON_AddItemToObject(result, "readStats", extraction_read_stats());
    cJSON_Delete(index);
    cJSON_Delete(installation_set);
    cJSON_Delete(opened);
    if (c->error)
    {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

static int dependency_secondary_sources(TbBall *c, cJSON *sources, cJSON *media_index,
                                        cJSON *errors)
{
    int expanded = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, sources)
    {
        if (cJSON_IsTrue(get(item, "secondaryIndexed")) ||
            (!equal_case(field(item, "status"), "verified-os2-media") &&
             !equal_case(field(item, "status"), "verified-os2-set")))
            continue;
        const char *path = field(item, "path"), *fingerprint = field(item, "fingerprint");
        int command = 0, level = 0;
        cJSON *index = dependency_cached_media(c, path, "full", fingerprint, &command, &level);
        put(item, "fullIndexCache", cJSON_CreateString(index ? "hit" : "miss"));
        put(item, "secondaryIndexed", cJSON_CreateBool(1));
        if (!index)
        {
            cJSON *request = cJSON_CreateObject(),
                  *selected = cJSON_AddArrayToObject(request, "sources");
            cJSON_AddItemToArray(selected, cJSON_CreateString(path));
            cJSON *plan = plan_os2_extraction(c, request);
            cJSON_Delete(request);
            if (!plan)
            {
                dependency_take_error(c, errors, path);
                continue;
            }
            index = cJSON_CreateObject();
            dependency_index_media(c, index, plan);
            cJSON_Delete(plan);
            command = cJSON_IsTrue(get(item, "commandSignature"));
            level = cJSON_IsTrue(get(item, "versionSignature"));
            dependency_cache_media(c, path, "full", fingerprint, command, level, index);
        }
        dependency_merge_media(media_index, index);
        cJSON_Delete(index);
        expanded = 1;
    }
    return expanded;
}

static void dependency_recovery(TbBall *c, cJSON *binaries, cJSON *index, cJSON *seen,
                                cJSON *media_index, cJSON *modules, cJSON *imported, cJSON *errors)
{
    cJSON *wanted = cJSON_Duplicate(modules, 1);
    const cJSON *module;
    cJSON_ArrayForEach(module, modules) if (equal_case(str(module), "HELPMGR"))
    {
        dependency_array_unique(wanted, "HPMGRMRI");
        dependency_array_unique(wanted, "VIEW.EXE");
        dependency_array_unique(wanted, "VIEWDOC.EXE");
        break;
    }
    cJSON_ArrayForEach(module, wanted)
    {
        const char *name = str(module);
        const cJSON *candidates = get(media_index, name), *candidate;
        if (!candidates)
            continue;
        cJSON *priorities = cJSON_CreateObject();
        cJSON_ArrayForEach(candidate, candidates)
        {
            char *key = tb_ball_keep(c, strdup(field(candidate, "path")));
            if (!key)
                continue;
            for (char *p = key; *p; p++)
                *p = (char)toupper((unsigned char)*p);
            const cJSON *old = get(priorities, key);
            double priority = get(candidate, "priority")->valuedouble;
            if (!old || old->valuedouble < priority)
                put(priorities, key, cJSON_CreateNumber(priority));
        }
        cJSON *selected = cJSON_CreateArray();
        cJSON_ArrayForEach(candidate, candidates)
        {
            const char *path = field(candidate, "path"), *source = field(candidate, "source");
            char *key = tb_ball_keep(c, strdup(path));
            if (!key)
                continue;
            for (char *p = key; *p; p++)
                *p = (char)toupper((unsigned char)*p);
            if (get(candidate, "priority")->valuedouble < get(priorities, key)->valuedouble)
                continue;
            char *host = tb_ball_host_path(c, path, 0);
            if (host && tb_is_file(host))
                continue;
            dependency_take_error(c, errors, path);
            if (os2_som_companion(path) && !extract_extension(path, ".exe"))
            {
                dependency_array_unique(selected, source);
                continue;
            }
            cJSON *checked = dependency_read_binary(c, source, source, errors);
            const char *format = checked ? field(checked, "format") : NULL;
            if (checked && cJSON_IsTrue(get(checked, "valid")) &&
                (equal_case(format, "LX") || equal_case(format, "LE") || equal_case(format, "NE")))
                dependency_array_unique(selected, source);
            else if (checked && cJSON_IsTrue(get(checked, "valid")))
                dependency_error(
                    errors, source,
                    "Media candidate targets another platform or is not an OS/2 executable; skipped");
            cJSON_Delete(checked);
        }
        cJSON_Delete(priorities);
        if (!cJSON_GetArraySize(selected))
        {
            cJSON_Delete(selected);
            continue;
        }
        cJSON *request = cJSON_CreateObject();
        cJSON_AddItemToObject(request, "sources", selected);
        cJSON *required = cJSON_AddArrayToObject(request, "modules");
        cJSON_AddItemToArray(required, cJSON_CreateString(name));
        cJSON *categories = cJSON_AddArrayToObject(request, "categories");
        cJSON_AddItemToArray(categories, cJSON_CreateString("full"));
        cJSON_AddBoolToObject(request, "rememberSources", 0);
        cJSON *result = extract_os2_categories(c, request);
        cJSON_Delete(request);
        if (!result)
        {
            dependency_take_error(c, errors, name);
            continue;
        }
        const cJSON *item;
        cJSON_ArrayForEach(item, get(result, "imported"))
        {
            const char *guest = field(item, "path");
            char *host = tb_ball_host_path(c, guest, 0);
            cJSON *recovered = cJSON_Duplicate(item, 1);
            unsigned char digest[32];
            if (host && tb_ball_digest_file(c, host, digest))
            {
                put(recovered, "sha256", cJSON_CreateString(tb_ball_hex(c, digest)));
                if (extract_extension(guest, ".dll") || extract_extension(guest, ".exe"))
                    dependency_index_binary(c, binaries, index, seen, errors, host, guest);
            }
            else
                dependency_take_error(c, errors, guest);
            cJSON_AddItemToArray(imported, recovered);
        }
        cJSON_Delete(result);
    }
    cJSON_Delete(wanted);
}

cJSON *dependency_compact(const cJSON *report)
{
    static const char *fields[] = {"summary",
                                   "logPath",
                                   "jsonPath",
                                   "missing",
                                   "imported",
                                   "disabled",
                                   "complete",
                                   "staticOnly",
                                   "unreachable",
                                   "media",
                                   "componentGroups",
                                   "componentFiles",
                                   "fingerprint",
                                   "runtimeBuild",
                                   "stale",
                                   "automaticRecovery",
                                   "scannedBinaries",
                                   "scope",
                                   "roots",
                                   "selectedProgram",
                                   "unknown",
                                   "cached",
                                   "indexGenerated",
                                   "cachedBinaries",
                                   NULL};
    cJSON *value = cJSON_CreateObject();
    for (int i = 0; fields[i]; i++)
    {
        const cJSON *field = get(report, fields[i]);
        if (field)
            cJSON_AddItemToObject(value, fields[i], cJSON_Duplicate(field, 1));
    }
    return value;
}

cJSON *dependency_audit_latest(TbBall *c)
{
    char *path = tb_ball_join(c, c->ball, "logs/dependencies-latest.json");
    FILE *file = path ? tb_fopen(path, "rb") : NULL;
    if (!file)
        return NULL;
    int64_t size = 0;
    int mode = 0;
    if (!tb_file_info(file, &size, &mode) || size <= 0 || size > 32 * 1024 * 1024)
    {
        fclose(file);
        return NULL;
    }
    char *bytes = malloc((size_t)size + 1);
    int ok = bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!ok)
    {
        free(bytes);
        return NULL;
    }
    bytes[size] = 0;
    cJSON *report = cJSON_ParseWithLength(bytes, (size_t)size);
    free(bytes);
    cJSON *summary = report ? dependency_compact(report) : NULL;
    cJSON_Delete(report);
    if (summary)
        put(summary, "cached", cJSON_CreateTrue());
    return summary;
}

static int dependency_write_text(const cJSON *report, const char *path)
{
    FILE *output = tb_fopen(path, "wb");
    if (!output)
        return 0;
    int ok =
        fprintf(
            output,
            "OS/2 dependency audit\n%s\n\nStatic imports and verified package companions only. Other dynamic library loads and API/ordinal compatibility are not checked.\n\nScanned binaries: %d\nAutomatic recovery: %s\n\n",
            field(report, "summary"), (int)get(report, "scannedBinaries")->valuedouble,
            cJSON_IsTrue(get(report, "automaticRecovery")) ? "enabled" : "disabled") >= 0;
    const char *sections[] = {"media",       "imported",       "searchPaths", "missing",
                              "unreachable", "componentFiles", "binaries",    "imports",
                              "errors",      "limits",         NULL};
    const cJSON *item;
    for (int section = 0; sections[section] && ok; section++)
    {
        ok = fprintf(output, "%s\n", sections[section]) >= 0;
        cJSON_ArrayForEach(item, get(report, sections[section]))
        {
            int written;
            if (str(item))
                written = fprintf(output, "  %s\n", str(item));
            else if (!strcmp(sections[section], "media"))
                written = fprintf(output, "  %s: %s%s%s\n", field(item, "status"),
                                  field(item, "path"), field(item, "reason") ? " — " : "",
                                  field(item, "reason") ? field(item, "reason") : "");
            else if (!strcmp(sections[section], "binaries"))
            {
                const char *format = field(item, "format");
                const char *state = !cJSON_IsTrue(get(item, "valid")) ? "invalid metadata"
                                    : equal_case(format, "LX") || equal_case(format, "LE") ||
                                            equal_case(format, "NE")
                                        ? "imports audited"
                                        : "imports not audited";
                written = fprintf(output, "  %s: %s, %s\n", field(item, "path"), format, state);
            }
            else if (!strcmp(sections[section], "imports"))
                written = fprintf(output, "  %s -> %s: %s%s%s\n", field(item, "binary"),
                                  field(item, "module"), field(item, "status"),
                                  field(item, "path") ? " " : "",
                                  field(item, "path") ? field(item, "path") : "");
            else if (!strcmp(sections[section], "imported"))
                written = fprintf(output, "  %s <- %s [SHA-256 %s]\n", field(item, "path"),
                                  field(item, "source"),
                                  field(item, "sha256") ? field(item, "sha256") : "unavailable");
            else if (!strcmp(sections[section], "componentFiles"))
                written = fprintf(output, "  %s: %s (%s)\n", field(item, "path"),
                                  field(item, "status"), field(item, "requirement"));
            else if (!strcmp(sections[section], "unreachable"))
            {
                char *locations = cJSON_PrintUnformatted(get(item, "paths"));
                written = fprintf(output, "  %s: %s\n", field(item, "module"),
                                  locations ? locations : "[]");
                free(locations);
            }
            else
                written =
                    fprintf(output, "  %s: %s\n", field(item, "path"), field(item, "message"));
            if (written < 0)
                ok = 0;
        }
        if (fputc('\n', output) == EOF)
            ok = 0;
    }
    ok = ok && !ferror(output);
    if (fclose(output))
        ok = 0;
    return ok;
}

static void dependency_save_report(TbBall *c, cJSON *report)
{
    cJSON *errors = get(report, "errors");
    char *logs = tb_ball_join(c, c->ball, "logs"), *directory = NULL;
    if (logs && tb_mkdirs(logs))
        for (unsigned number = 0; number < 1000; number++)
        {
            char *candidate = tb_ball_format(c, "%s/dependency-audit-%lld-%03u", logs,
                                             (long long)time(NULL), number);
            if (candidate && tb_mkdir_new(candidate))
            {
                directory = candidate;
                break;
            }
            if (errno != EEXIST)
                break;
        }
    if (!directory)
    {
        dependency_error(errors, "logs", "Cannot create a unique dependency audit directory");
        put(report, "complete", cJSON_CreateBool(0));
        return;
    }
    char *json_path = tb_ball_join(c, directory, "audit.json"),
         *text_path = tb_ball_join(c, directory, "audit.txt");
    put(report, "jsonPath", cJSON_CreateString(json_path));
    put(report, "logPath", cJSON_CreateString(text_path));
    if (!dependency_write_text(report, text_path))
        dependency_error(errors, text_path, "Cannot write the human-readable dependency report");
    dependency_take_error(c, errors, text_path);
    if (cJSON_GetArraySize(errors))
        put(report, "complete", cJSON_CreateBool(0));
    if (!tb_ball_write_json(c, json_path, report))
        dependency_take_error(c, errors, json_path);
    cJSON *latest = dependency_compact(report);
    if (!tb_ball_write_json(c, tb_ball_join(c, logs, "dependencies-latest.json"), latest))
        dependency_take_error(c, errors, "logs/dependencies-latest.json");
    cJSON_Delete(latest);
}

static int dependency_som_required(TbBall *c, const cJSON *report)
{
    const cJSON *entry;
    cJSON_ArrayForEach(entry, get(report, "imports")) if (os2_module_matches(field(entry, "module"),
                                                                             "SOMD")) return 1;
    return os2_som_present(c, "C:\\OS2\\DLL\\SOMD.DLL");
}

static int dependency_som_files(TbBall *c, cJSON *report, cJSON *unresolved)
{
    cJSON *files = cJSON_CreateArray();
    int missing = 0;
    if (dependency_som_required(c, report))
        for (int i = 0; os2_som_companions[i]; i++)
        {
            const char *path = os2_som_companions[i];
            int present = os2_som_present(c, path);
            cJSON *file = cJSON_CreateObject();
            cJSON_AddStringToObject(file, "path", path);
            cJSON_AddStringToObject(file, "status", present ? "found" : "missing");
            cJSON_AddStringToObject(file, "requirement", "verified-package-companion");
            cJSON_AddItemToArray(files, file);
            if (!present)
            {
                missing++;
                if (unresolved)
                    dependency_array_unique(unresolved, dependency_basename(c, path));
            }
        }
    put(report, "componentFiles", files);
    return missing;
}

cJSON *dependency_component_groups(const cJSON *report)
{
    int needed[EXTRACT_COUNT] = {0};
    cJSON *groups = cJSON_CreateArray();
    const cJSON *entry;
    cJSON_ArrayForEach(entry, get(report, "imports"))
    {
        const char *status = field(entry, "status"), *module = field(entry, "module");
        if (strcmp(status, "missing") && strcmp(status, "unreachable"))
            continue;
        int category = extract_module_group(module);
        if (category >= 0)
            needed[category] = 1;
    }
    cJSON_ArrayForEach(
        entry, get(report, "componentFiles")) if (equal_case(field(entry, "status"), "missing"))
        needed[EXTRACT_SOM] = 1;
    if (needed[EXTRACT_SOM])
        needed[EXTRACT_RUNTIME] = 1;
    for (int i = 0; i < EXTRACT_COUNT; i++)
    {
        if (!needed[i])
            continue;
        cJSON *group = extract_group(i), *modules = cJSON_AddArrayToObject(group, "modules"),
              *required_by = cJSON_AddArrayToObject(group, "requiredBy"),
              *files = cJSON_AddArrayToObject(group, "files");
        cJSON_ArrayForEach(entry, get(report, "imports"))
        {
            const char *status = field(entry, "status"), *module = field(entry, "module");
            if ((!strcmp(status, "missing") || !strcmp(status, "unreachable")) &&
                extract_module_group(module) == i)
            {
                dependency_array_unique(modules, module);
                dependency_array_unique(required_by, field(entry, "binary"));
            }
        }
        if (i == EXTRACT_SOM)
            cJSON_ArrayForEach(
                entry,
                get(report, "componentFiles")) if (equal_case(field(entry, "status"), "missing"))
            {
                dependency_array_unique(files, field(entry, "path"));
                dependency_array_unique(required_by, "C:\\OS2\\DLL\\SOMD.DLL");
            }
        cJSON_AddBoolToObject(group, "requiredByGroup",
                              i == EXTRACT_RUNTIME && !cJSON_GetArraySize(modules));
        cJSON_AddItemToArray(groups, group);
    }
    return groups;
}

cJSON *audit_dependencies(TbBall *parent, const cJSON *request, int automatic_recovery)
{
    /* Auditing failures belong to the audit, never to the installer's exit
       result or the parent request's successful receipt transaction. */
    TbBall storage = *parent;
    storage.allocations = NULL;
    storage.count = 0;
    storage.error = NULL;
    TbBall *c = &storage;
    cJSON *report = cJSON_CreateObject(), *errors = cJSON_AddArrayToObject(report, "errors"),
          *limits = cJSON_AddArrayToObject(report, "limits"),
          *imported = cJSON_AddArrayToObject(report, "imported"), *index = cJSON_CreateObject(),
          *seen = cJSON_CreateObject(), *media_index = cJSON_CreateObject();
    cJSON_AddStringToObject(report, "scope", "ball");
    cJSON_AddBoolToObject(report, "staticOnly", 1);
    cJSON_AddStringToObject(report, "logPath", "");
    cJSON_AddStringToObject(report, "jsonPath", "");
    cJSON_AddBoolToObject(report, "automaticRecovery", automatic_recovery);
    cJSON_AddStringToObject(report, "started", tb_ball_now(c));
    if (get(request, "exitCode"))
        cJSON_AddItemToObject(report, "installerExitCode",
                              cJSON_Duplicate(get(request, "exitCode"), 1));
    dependency_array_unique(
        limits,
        "Static imports and verified package companions only; other dynamic library loads and API/ordinal compatibility are not checked.");
    dependency_array_unique(
        limits,
        "Original HELPMGR requires its dynamically loaded HPMGRMRI resource DLL; this verified companion is included in the audit.");
    cJSON *sources = dependency_sources(c, request, errors);
    cJSON_AddItemToObject(report, "media", sources);
    const char *build = field(request, "build");
    char *initial_fingerprint = dependency_fingerprint(c, sources, build, NULL);
    cJSON *binaries = dependency_scan(c, index, seen, errors, limits);
    const cJSON *application;
    cJSON_ArrayForEach(application, get(c->manifest, "applications"))
    {
        const char *program = field(application, "program");
        if (!program || !*program)
            continue;
        c->application = application;
        char *host = tb_ball_host_path(c, program, 0);
        if (host && tb_is_file(host))
        {
            const char *guest = tb_ball_guest_for_input(c, host, program);
            if (guest)
            {
                if (extract_extension(program, ".cmd") || extract_extension(program, ".bat"))
                    dependency_index_script(c, binaries, seen, errors, host, guest);
                else
                    dependency_index_binary(c, binaries, index, seen, errors, host, guest);
            }
        }
        dependency_take_error(c, errors, program);
    }
    c->application = NULL;
    cJSON *paths = dependency_search_paths(c, errors), *eligible = NULL;
    cJSON_AddItemToObject(report, "searchPaths", cJSON_Duplicate(paths, 1));
    for (unsigned round = 0; round <= DEPENDENCY_ROUND_LIMIT; round++)
    {
        cJSON *missing = cJSON_CreateArray(), *unreachable = cJSON_CreateArray(),
              *unresolved = cJSON_CreateArray();
        cJSON *imports = dependency_resolve(c, binaries, index, seen, paths, errors, missing,
                                            unreachable, unresolved);
        put(report, "imports", imports);
        put(report, "missing", missing);
        put(report, "unreachable", unreachable);
        dependency_som_files(c, report, unresolved);
        if (!automatic_recovery || !cJSON_GetArraySize(unresolved))
        {
            cJSON_Delete(unresolved);
            break;
        }
        if (!eligible)
            eligible = dependency_verified_sources(c, sources, errors, media_index, 1);
        if (!cJSON_GetArraySize(eligible))
        {
            dependency_array_unique(
                limits, "No verified OS/2 installation media is available for missing DLLs.");
            cJSON_Delete(unresolved);
            break;
        }
        if (round == DEPENDENCY_ROUND_LIMIT)
        {
            dependency_error(errors, "", "Automatic dependency closure reached its 16-round limit");
            cJSON_Delete(unresolved);
            break;
        }
        int previous = cJSON_GetArraySize(imported);
        dependency_recovery(c, binaries, index, seen, media_index, unresolved, imported, errors);
        if (previous == cJSON_GetArraySize(imported) &&
            dependency_secondary_sources(c, sources, media_index, errors))
            dependency_recovery(c, binaries, index, seen, media_index, unresolved, imported,
                                errors);
        cJSON_Delete(unresolved);
        if (previous == cJSON_GetArraySize(imported))
            break;
    }
    if (cJSON_GetArraySize(imported) && eligible && cJSON_GetArraySize(eligible))
    {
        cJSON *remembered = cJSON_CreateObject();
        os2_preserve_media_selection(c, remembered);
        cJSON_AddItemToObject(remembered, "sources", cJSON_Duplicate(eligible, 1));
        cJSON_AddStringToObject(remembered, "recorded", tb_ball_now(c));
        if (!tb_ball_write_json(c, tb_ball_join(c, c->ball, "os2-media.json"), remembered))
            dependency_take_error(c, errors, "os2-media.json");
        cJSON_Delete(remembered);
    }
    int service_changed = 0;
    if (automatic_recovery && dependency_som_required(c, report))
    {
        os2_som_provision(c, &service_changed);
        dependency_take_error(c, errors, "DSOM startup service");
    }
    cJSON *disabled = cJSON_AddArrayToObject(report, "disabled");
    const cJSON *requirement;
    cJSON_ArrayForEach(
        requirement, get(report, "imports")) if (!strcmp(field(requirement, "status"), "disabled"))
        dependency_array_unique(disabled, field(requirement, "module"));
    int disabled_count = cJSON_GetArraySize(disabled);
    cJSON_AddItemToObject(report, "componentGroups", dependency_component_groups(report));
    int unsupported = 0;
    cJSON *inventory = cJSON_AddArrayToObject(report, "binaries");
    const cJSON *binary;
    cJSON_ArrayForEach(binary, binaries)
    {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "path", field(binary, "path"));
        cJSON_AddStringToObject(entry, "format", field(binary, "format"));
        cJSON_AddBoolToObject(entry, "valid", cJSON_IsTrue(get(binary, "valid")));
        cJSON_AddNumberToObject(entry, "moduleCount", cJSON_GetArraySize(get(binary, "modules")));
        cJSON_AddItemToObject(entry, "modules", cJSON_Duplicate(get(binary, "modules"), 1));
        if (get(binary, "companions"))
            cJSON_AddItemToObject(entry, "companions",
                                  cJSON_Duplicate(get(binary, "companions"), 1));
        if (get(binary, "rexx"))
            cJSON_AddItemToObject(entry, "rexx", cJSON_Duplicate(get(binary, "rexx"), 1));
        cJSON_AddItemToArray(inventory, entry);
        if (cJSON_IsTrue(get(binary, "valid")) && !equal_case(field(binary, "format"), "LX") &&
            !equal_case(field(binary, "format"), "NE") &&
            !equal_case(field(binary, "format"), "LE") &&
            !equal_case(field(binary, "format"), "command-file"))
            unsupported++;
    }
    if (unsupported)
        dependency_array_unique(
            limits,
            "Some installed binaries target Windows, DOS or another format; their imports are not audited.");
    cJSON_AddNumberToObject(report, "scannedBinaries", cJSON_GetArraySize(binaries));
    cJSON_AddNumberToObject(report, "unsupportedBinaries", unsupported);
    int absent = cJSON_GetArraySize(get(report, "missing")),
        unreachable = cJSON_GetArraySize(get(report, "unreachable"));
    int missing_files = dependency_som_files(c, report, NULL);
    int complete =
        !absent && !unreachable && !disabled_count && !missing_files && !cJSON_GetArraySize(errors);
    cJSON_AddBoolToObject(report, "complete", complete);
    cJSON_AddStringToObject(report, "finished", tb_ball_now(c));
    cJSON_AddStringToObject(
        report, "summary",
        tb_ball_format(
            c,
            "Scanned %d binaries; recovered %d files; %d missing libraries, %d unreachable imports, %d disabled libraries, %d missing package files and %d audit errors.",
            cJSON_GetArraySize(binaries), cJSON_GetArraySize(imported), absent, unreachable,
            disabled_count, missing_files, cJSON_GetArraySize(errors)));
    dependency_take_error(c, errors, "Dependency audit");
    if (build)
        cJSON_AddStringToObject(report, "runtimeBuild", build);
    char *fingerprint = dependency_fingerprint(c, sources, build, get(report, "componentFiles"));
    char *final_inventory = !cJSON_GetArraySize(imported) && !service_changed
                                ? dependency_fingerprint(c, sources, build, NULL)
                                : NULL;
    int changed =
        !cJSON_GetArraySize(imported) && !service_changed &&
        (!initial_fingerprint || !final_inventory || strcmp(initial_fingerprint, final_inventory));
    if (fingerprint)
        cJSON_AddStringToObject(report, "fingerprint", fingerprint);
    else
    {
        put(report, "complete", cJSON_CreateBool(0));
        put(report, "summary",
            cJSON_CreateString(
                "Dependency audit could not establish current file metadata; check libraries again."));
    }
    if (changed)
    {
        put(report, "complete", cJSON_CreateBool(0));
        put(report, "summary",
            cJSON_CreateString(
                "Installed files or dependency settings changed during the audit; check libraries again."));
        dependency_array_unique(
            limits, "The metadata inventory changed during this audit; its result is stale.");
    }
    cJSON_AddBoolToObject(report, "stale", fingerprint == NULL || changed);
    dependency_database_save(c, report);
    dependency_take_error(c, errors, "Dependency index");
    dependency_save_report(c, report);
    dependency_take_error(c, errors, "Dependency report");
    cJSON_Delete(index);
    cJSON_Delete(media_index);
    cJSON_Delete(seen);
    cJSON_Delete(binaries);
    cJSON_Delete(paths);
    cJSON_Delete(eligible);
    for (size_t i = 0; i < c->count; i++)
        free(c->allocations[i]);
    free(c->allocations);
    free(c->error);
    return report;
}

/* Compatibility query only. Explicit audit-dependencies is the rescan action;
   normal preflight must never discover or recover files as a side effect. */
cJSON *ensure_dependencies(TbBall *c, const cJSON *request)
{
    (void)request;
    cJSON *latest = dependency_audit_latest(c);
    if (!latest)
    {
        latest = cJSON_CreateObject();
        cJSON_AddBoolToObject(latest, "unknown", 1);
        cJSON_AddBoolToObject(latest, "complete", 0);
        cJSON_AddStringToObject(
            latest, "summary",
            "No saved dependency audit. Use Rescan Dependencies to build the index.");
    }
    put(latest, "cached", cJSON_CreateTrue());
    return latest;
}
