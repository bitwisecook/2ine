/* Public domain. No rights reserved. Wholly AI-authored. */
/* Component jobs persist progress atomically and journal each committed file. */
struct ComponentOperation
{
    TbMutationLock *lock;
    const char *id, *progress_path, *cancel_path, *log_path, *journal_path;
    cJSON *progress, *ledger;
    int cancelled;
};

static int component_token(const char *token)
{
    if (!token || !*token || strlen(token) > 64)
        return 0;
    for (const unsigned char *at = (const unsigned char *)token; *at; at++)
        if (!((*at >= 'A' && *at <= 'Z') || (*at >= 'a' && *at <= 'z') ||
              (*at >= '0' && *at <= '9') || *at == '-' || *at == '_'))
            return 0;
    return 1;
}

static char *component_key(Context *c, const char *guest)
{
    char *key = keep(c, strdup(guest));
    for (char *at = key; at && *at; at++)
        *at = *at == '/' ? '\\' : (char)toupper((unsigned char)*at);
    return key;
}

static cJSON *component_read(Context *c, const char *path)
{
    TbFileIdentity identity;
    uint64_t record_size = 0;
    int64_t record_modified = 0;
    if (tb_is_link(path) ||
        (tb_path_info(path, &record_size, &record_modified) && !tb_path_identity(path, &identity)))
    {
        fail(c, "Component record is redirected or not a regular file: %s", path);
        return NULL;
    }
    size_t size = 0;
    char *bytes = read_file(c, path, &size, 1);
    cJSON *value = bytes && size ? cJSON_ParseWithLength(bytes, size) : NULL;
    if (bytes && size && !value)
        fail(c, "Cannot read component record: %s", path);
    return value;
}

static int component_storage(Context *c, const char *relative)
{
    char *path = keep(c, strdup(relative)), *current = c->ball;
    if (tb_is_link(c->ball))
        return fail(c, "Component records cannot use a redirected ball");
    for (char *part = path; part && *part;)
    {
        char *slash = strchr(part, '/');
        if (slash)
            *slash = 0;
        current = join(c, current, part);
        if (!current || tb_is_link(current))
            return fail(c, "Component records cannot use symbolic links");
        part = slash ? slash + 1 : NULL;
    }
    return 1;
}

static cJSON *component_ledger_once(Context *c)
{
    cJSON *ledger = component_read(c, join(c, c->ball, "os2-components.json"));
    if (!ledger)
        ledger = cJSON_CreateObject();
    if (!cJSON_IsObject(ledger))
        fail(c, "Component ownership record must be an object");
    char *path = join(c, c->ball, "logs/os2-components/ownership.jsonl");
    size_t length = 0;
    TbFileIdentity journal_identity;
    uint64_t journal_size = 0;
    int64_t journal_modified = 0;
    if (tb_is_link(path) || (tb_path_info(path, &journal_size, &journal_modified) &&
                             !tb_path_identity(path, &journal_identity)))
        fail(c, "Component ownership journal is not a regular file");
    char *journal = c->error ? NULL : read_file(c, path, &length, 1);
    for (char *line = journal; line && *line;)
    {
        char *end = strchr(line, '\n');
        if (!end)
            break; /* A concurrent append can expose an unfinished last row. */
        cJSON *entry = cJSON_ParseWithLength(line, (size_t)(end - line));
        const char *guest = field(entry, "path");
        if (!entry || !guest)
        {
            cJSON_Delete(entry);
            fail(c, "Cannot read component ownership journal");
            break;
        }
        char *key = component_key(c, guest);
        if (cJSON_IsTrue(get(entry, "removed")))
            cJSON_DeleteItemFromObjectCaseSensitive(ledger, key);
        else
            put(ledger, key, cJSON_Duplicate(entry, 1));
        cJSON_Delete(entry);
        line = end + 1;
    }
    if (c->error)
    {
        cJSON_Delete(ledger);
        return NULL;
    }
    return ledger;
}

/* Finalizing a job replaces the snapshot and then truncates its journal. Retry
   if that snapshot changed around our read, so polling cannot miss that handoff. */
static cJSON *component_ledger(Context *c)
{
    char *path = join(c, c->ball, "os2-components.json");
    cJSON *ledger = NULL;
    for (unsigned attempt = 0; attempt < 3; attempt++)
    {
        TbFileIdentity before, after;
        int existed = tb_path_identity(path, &before);
        cJSON_Delete(ledger);
        ledger = component_ledger_once(c);
        int exists = tb_path_identity(path, &after);
        if (c->error || (existed == exists && (!exists || !memcmp(&before, &after, sizeof before))))
            break;
    }
    return ledger;
}

static cJSON *component_identity(Context *c, const TbFileIdentity *identity)
{
    cJSON *value = cJSON_CreateObject();
    const uint64_t fields[] = {
        identity->device,          identity->file,    identity->size,
        identity->modified,        identity->changed, identity->modified_fraction,
        identity->changed_fraction};
    const char *names[] = {"device",           "file",           "size", "modified", "changed",
                           "modifiedFraction", "changedFraction"};
    for (unsigned i = 0; i < 7; i++)
        cJSON_AddStringToObject(value, names[i], format(c, "%llu", (unsigned long long)fields[i]));
    return value;
}

static int component_same_identity(Context *c, const cJSON *record, const TbFileIdentity *identity)
{
    cJSON *current = component_identity(c, identity);
    int same = cJSON_Compare(current, get(record, "identity"), 1);
    cJSON_Delete(current);
    return same;
}

static void component_progress(Context *c, const char *phase, const char *source, const char *path)
{
    ComponentOperation *job = c->component_operation;
    if (!job || c->error)
        return;
    put(job->progress, "phase", cJSON_CreateString(phase));
    cJSON *paths = cJSON_CreateArray(), *sources = cJSON_CreateArray();
    if (path)
        cJSON_AddItemToArray(paths, cJSON_CreateString(path));
    if (source)
        cJSON_AddItemToArray(sources, cJSON_CreateString(source));
    put(job->progress, "activePaths", paths);
    put(job->progress, "activeSources", sources);
    put(job->progress, "updated", cJSON_CreateString(now(c)));
    write_json(c, job->progress_path, job->progress);
}

static int component_cancelled(Context *c)
{
    ComponentOperation *job = c->component_operation;
    if (job && tb_is_file(job->cancel_path))
    {
        job->cancelled = 1;
        fail(c, "Component operation cancelled; files already installed are preserved");
        return 1;
    }
    return c->error != NULL;
}

static int component_journal(Context *c, cJSON *entry)
{
    ComponentOperation *job = c->component_operation;
    if (!job)
        return 1;
    char *text = cJSON_PrintUnformatted(entry);
    FILE *file = text ? tb_fopen(job->journal_path, "ab") : NULL;
    int ok = file && fwrite(text, 1, strlen(text), file) == strlen(text) &&
             fputc('\n', file) != EOF && !fflush(file);
    if (file && fclose(file))
        ok = 0;
    cJSON_free(text);
    if (!ok)
        return fail(c, "Cannot save component ownership journal");
    char *key = component_key(c, field(entry, "path"));
    if (cJSON_IsTrue(get(entry, "removed")))
        cJSON_DeleteItemFromObjectCaseSensitive(job->ledger, key);
    else
        put(job->ledger, key, cJSON_Duplicate(entry, 1));
    cJSON *completed = get(job->progress, "completedPaths");
    if (cJSON_GetArraySize(completed) == 256)
    {
        cJSON_DeleteItemFromArray(completed, 0);
        put(job->progress, "completedPathsTruncated", cJSON_CreateTrue());
    }
    cJSON_AddItemToArray(completed, cJSON_CreateString(field(entry, "path")));
    double count = get(job->progress, "completedCount")->valuedouble;
    put(job->progress, "completedCount", cJSON_CreateNumber(count + 1));
    return 1;
}

static int component_record(Context *c, const char *guest, const char *source, const char *category,
                            const char *hash)
{
    ComponentOperation *job = c->component_operation;
    if (!job)
        return 1;
    char *host = host_path(c, guest, 0);
    TbFileIdentity identity;
    if (!host || !inside(host, c->roots[2]) || !tb_path_identity(host, &identity))
        return fail(c, "Cannot record installed component: %s", guest);
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "path", guest);
    cJSON_AddStringToObject(entry, "source", source ? source : "");
    cJSON_AddStringToObject(entry, "category", category ? category : "cmd");
    cJSON_AddStringToObject(entry, "sha256", hash);
    cJSON_AddStringToObject(entry, "operationID", job->id);
    cJSON_AddItemToObject(entry, "identity", component_identity(c, &identity));
    int ok = component_journal(c, entry);
    cJSON_Delete(entry);
    return ok;
}

static int component_begin(Context *c, const cJSON *request, const char *kind)
{
    if (!component_storage(c, "cache/component-operations") ||
        !component_storage(c, "logs/os2-components") ||
        !component_storage(c, "os2-components.json") ||
        !component_storage(c, "logs/os2-components/ownership.jsonl"))
        return 0;
    char *cache = join(c, c->ball, "cache/component-operations"),
         *logs = join(c, c->ball, "logs/os2-components");
    if (!tb_mkdirs(cache) || !tb_mkdirs(logs))
        return fail(c, "Cannot create component operation records");
    ComponentOperation *job = (ComponentOperation *)keep(c, calloc(1, sizeof *job));
    if (!job)
        return 0;
    job->lock = tb_mutation_lock(cache);
    if (!job->lock)
        return errno == EBUSY
                   ? fail(c, "Another component operation is already running for this ball")
                   : fail(c, "Cannot lock component records: %s", strerror(errno));
    c->component_operation = job;
    const char *id = field(request, "operationID");
    if (get(request, "operationID") && !id)
        return fail(c, "Invalid component operation ID");
    if (!id)
    {
        char *workspace = keep(c, tb_archive_workspace(cache));
        if (!workspace)
            return fail(c, "Cannot create component operation ID");
        SHA256_CTX hash;
        unsigned char digest[32];
        sha256_init(&hash);
        sha256_update(&hash, (const unsigned char *)workspace, strlen(workspace));
        sha256_final(&hash, digest);
        id = format(c, "auto-%.32s", hex(c, digest));
        tb_remove_tree(workspace);
    }
    if (!component_token(id))
        return fail(c, "Invalid component operation ID");
    job->id = id;
    job->progress_path = format(c, "%s/%s.json", cache, id);
    job->cancel_path = format(c, "%s/%s.cancel", cache, id);
    job->log_path = format(c, "%s/%s.json", logs, id);
    job->journal_path = join(c, logs, "ownership.jsonl");
    uint64_t old_size = 0;
    int64_t old_modified = 0;
    if (tb_is_link(job->progress_path) || tb_is_link(job->log_path) ||
        tb_path_info(job->progress_path, &old_size, &old_modified) ||
        tb_path_info(job->log_path, &old_size, &old_modified))
        return fail(c, "Component operation ID has already been used");
    job->progress = cJSON_CreateObject();
    cJSON_AddStringToObject(job->progress, "operationID", id);
    cJSON_AddStringToObject(job->progress, "kind", kind);
    cJSON_AddStringToObject(job->progress, "status", "running");
    cJSON_AddStringToObject(job->progress, "logPath", job->log_path);
    cJSON_AddNumberToObject(job->progress, "completedCount", 0);
    cJSON_AddNumberToObject(job->progress, "totalCount", 0);
    cJSON_AddArrayToObject(job->progress, "completedPaths");
    component_progress(c, "planning", NULL, NULL);
    cJSON *latest = cJSON_CreateObject();
    cJSON_AddStringToObject(latest, "operationID", id);
    write_json(c, join(c, cache, "latest.json"), latest);
    cJSON_Delete(latest);
    job->ledger = component_ledger(c);
    /* Fold any complete rows left by an interrupted job before appending again.
       This also discards its incomplete final row without losing committed rows. */
    uint64_t journal_size = 0;
    int64_t journal_modified = 0;
    if (!c->error && tb_path_info(job->journal_path, &journal_size, &journal_modified) &&
        journal_size && write_json(c, join(c, c->ball, "os2-components.json"), job->ledger) &&
        !tb_atomic_write(job->journal_path, "", 0))
        fail(c, "Cannot resume component ownership journal");
    return !c->error;
}

static void component_finish(Context *c, cJSON *result)
{
    ComponentOperation *job = c->component_operation;
    if (!job)
        return;
    char *original_error = c->error;
    c->error = NULL;
    if (job->ledger && job->journal_path &&
        write_json(c, join(c, c->ball, "os2-components.json"), job->ledger) &&
        !tb_atomic_write(job->journal_path, "", 0))
        fail(c, "Cannot finish component ownership journal");
    if (!original_error && c->error)
    {
        original_error = c->error;
        c->error = NULL;
    }
    if (job->progress)
    {
        put(job->progress, "status",
            cJSON_CreateString(job->cancelled   ? "cancelled"
                               : original_error ? "failed"
                                                : "complete"));
        if (original_error)
            put(job->progress, "error", cJSON_CreateString(original_error));
        put(job->progress, "phase", cJSON_CreateString("finished"));
        put(job->progress, "updated", cJSON_CreateString(now(c)));
        put(job->progress, "activePaths", cJSON_CreateArray());
        put(job->progress, "activeSources", cJSON_CreateArray());
        cJSON *report = cJSON_Duplicate(job->progress, 1);
        if (result)
            cJSON_AddItemToObject(report, "result", cJSON_Duplicate(result, 1));
        write_json(c, job->log_path, report);
        cJSON_Delete(report);
        if (!original_error && c->error)
        {
            original_error = c->error;
            c->error = NULL;
            put(job->progress, "status", cJSON_CreateString("failed"));
            put(job->progress, "error", cJSON_CreateString(original_error));
        }
        write_json(c, job->progress_path, job->progress);
        if (result)
            cJSON_AddItemToObject(result, "progress", cJSON_Duplicate(job->progress, 1));
    }
    if (original_error)
    {
        free(c->error);
        c->error = original_error;
    }
    cJSON_Delete(job->ledger);
    cJSON_Delete(job->progress);
    tb_mutation_unlock(job->lock);
    c->component_operation = NULL;
}

/* Resolve a guest destination without following symbolic links, including case aliases.
   Missing regular paths are allowed; mapped/external drives and cache internals are not. */
static char *component_private_path(Context *c, const char *guest)
{
    if (!guest || strlen(guest) < 4 || strlen(guest) > 32768 ||
        toupper((unsigned char)guest[0]) != 'C' || guest[1] != ':' ||
        (guest[2] != '\\' && guest[2] != '/') || !c->roots[2] || !inside(c->roots[2], c->ball) ||
        tb_image_path(c->roots[2]) || tb_is_link(c->roots[2]))
    {
        fail(c, "Component files must be on this ball's private C: drive");
        return NULL;
    }
    char *key = component_key(c, guest);
    if (!strncmp(key, "C:\\OS2\\DLL\\2INE\\", 16) || equal_case(key, "C:\\OS2\\DLL\\2INE"))
    {
        fail(c, "Runtime cache files cannot be managed as OS/2 components");
        return NULL;
    }
    char *parts = keep(c, strdup(guest + 3)), *path = c->roots[2];
    for (char *at = parts; at && *at;)
    {
        char *end = strpbrk(at, "/\\");
        if (end)
            *end = 0;
        if (!*at || !strcmp(at, ".") || !strcmp(at, "..") || strpbrk(at, ":*?\r\n"))
        {
            fail(c, "Invalid private C: component path");
            return NULL;
        }
        char *candidate = join(c, path, at);
        if (!tb_is_link(candidate) && !tb_is_file(candidate) && !tb_is_dir(candidate) &&
            tb_is_dir(path))
        {
            size_t count = 0;
            char **names = tb_names(path, &count);
            for (size_t i = 0; i < count; i++)
                if (equal_case(names[i], at))
                {
                    candidate = join(c, path, names[i]);
                    break;
                }
            tb_names_free(names, count);
        }
        if (tb_is_link(candidate) || (end && tb_is_file(candidate)) ||
            (!end && tb_is_dir(candidate)))
        {
            fail(c, "Component path is redirected or not a regular file");
            return NULL;
        }
        path = candidate;
        at = end ? end + 1 : NULL;
        if (at && !*at)
        {
            fail(c, "A component file path is required");
            return NULL;
        }
    }
    return path;
}
