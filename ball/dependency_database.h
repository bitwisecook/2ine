/* Public domain. No rights reserved. Wholly AI-authored. */
/* Portable metadata only: no host paths, executable bytes, or media contents. */
static cJSON *dependency_database_from_report(Context *c, const cJSON *report)
{
    if (!cJSON_IsArray(get(report, "binaries")) || !cJSON_IsArray(get(report, "imports")))
        return NULL;
    cJSON *database = cJSON_CreateObject(),
          *binaries = cJSON_AddArrayToObject(database, "binaries");
    cJSON_AddNumberToObject(database, "schema", 1);
    cJSON_AddBoolToObject(database, "unknown", cJSON_IsTrue(get(report, "stale")));
    cJSON_AddStringToObject(database, "generated", field(report, "finished"));
    cJSON *legacy = cJSON_CreateObject();
    const cJSON *binary;
    cJSON_ArrayForEach(binary, get(report, "binaries"))
    {
        const char *path = field(binary, "path");
        if (!path || strlen(path) < 3 || path[1] != ':' || path[2] != '\\')
            continue;
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "path", component_key(c, path));
        cJSON_AddStringToObject(entry, "format", field(binary, "format"));
        cJSON_AddBoolToObject(entry, "valid", cJSON_IsTrue(get(binary, "valid")));
        cJSON *modules = cJSON_AddArrayToObject(entry, "modules");
        if (cJSON_IsArray(get(binary, "modules")))
        {
            const cJSON *name;
            cJSON_ArrayForEach(name, get(binary, "modules")) if (str(name))
                dependency_array_unique(modules, str(name));
            cJSON_ArrayForEach(name, get(binary, "companions")) if (str(name))
                dependency_array_unique(modules, str(name));
        }
        else
            cJSON_AddItemReferenceToObject(legacy, field(entry, "path"), entry);
        if (get(binary, "rexx"))
            cJSON_AddItemToObject(entry, "rexx", cJSON_Duplicate(get(binary, "rexx"), 1));
        cJSON_AddItemToArray(binaries, entry);
    }
    /* One normalized path lookup per edge. Legacy reports can be large; never
       allocate a path for every binary/import pair during a normal query. */
    const cJSON *import;
    cJSON_ArrayForEach(import, get(report, "imports"))
    {
        const char *path = field(import, "binary"), *module = field(import, "module");
        cJSON *modules = path ? get(get(legacy, component_key(c, path)), "modules") : NULL;
        if (modules && module && *module)
            dependency_array_unique(modules, module);
    }
    cJSON_Delete(legacy);
    if (get(report, "componentFiles"))
        cJSON_AddItemToObject(database, "componentFiles",
                              cJSON_Duplicate(get(report, "componentFiles"), 1));
    return database;
}
static void dependency_database_save(Context *c, const cJSON *report)
{
    cJSON *database = dependency_database_from_report(c, report);
    if (database)
    {
        if (!tb_mkdirs(join(c, c->ball, "cache")) ||
            !write_json(c, join(c, c->ball, "cache/dependency-index.json"), database))
            fail(c, "Cannot save the dependency metadata index");
        cJSON_Delete(database);
    }
}
static cJSON *dependency_database_json(Context *c, const char *path)
{
    uint64_t length = 0;
    int64_t modified = 0;
    if (!tb_path_info(path, &length, &modified) || !length || length > 32u * 1024u * 1024u)
        return NULL;
    size_t size = 0;
    char *bytes = read_file(c, path, &size, 1);
    return bytes && size ? cJSON_ParseWithLength(bytes, size) : NULL;
}
static cJSON *dependency_database_load(Context *c)
{
    cJSON *database = dependency_database_json(c, join(c, c->ball, "cache/dependency-index.json"));
    if (cJSON_IsNumber(get(database, "schema")) && get(database, "schema")->valuedouble == 1 &&
        cJSON_IsArray(get(database, "binaries")) &&
        cJSON_GetArraySize(get(database, "binaries")) <= DEPENDENCY_BINARY_LIMIT)
        return database;
    cJSON_Delete(database);
    /* Read-only legacy migration: never walk a directory or rebuild the audit. */
    cJSON *latest = dependency_database_json(c, join(c, c->ball, "logs/dependencies-latest.json"));
    const char *path = field(latest, "jsonPath"), *parent = NULL;
    if (path)
    {
        const char *at = strstr(path, "/dependency-audit-");
        while (at)
        {
            parent = at + 1;
            at = strstr(at + 1, "/dependency-audit-");
        }
    }
    if (parent && !strstr(parent, "..") && !strchr(parent, '\\') && strlen(parent) < 128 &&
        !strcmp(strrchr(parent, '/') ? strrchr(parent, '/') : "", "/audit.json"))
    {
        cJSON *report = dependency_database_json(c, join(c, join(c, c->ball, "logs"), parent));
        database = dependency_database_from_report(c, report);
        cJSON_Delete(report);
    }
    cJSON_Delete(latest);
    return database;
}
