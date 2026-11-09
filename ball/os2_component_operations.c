/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read-only live state is independent of the per-ball mutation guard. */
#include "ball/ball_internal.h"
#include "ball/manifest.h"
#include "ball/os2_component_operations.h"
#include "ball/os2_component_store.h"
#include "ball/os2_extract.h"
#include "ball/os2_groups.h"
#include "ball/os2_som.h"
#include "ball/os2_support.h"
#include "ball/os2_tree.h"

static cJSON *component_current_progress(Context *c, const cJSON *request)
{
    const char *id = field(request, "operationID");
    if (get(request, "operationID") && !id)
    {
        tb_ball_fail(c, "Invalid component operation ID");
        return NULL;
    }
    if (!component_storage(c, "cache/component-operations"))
        return NULL;
    if (!id)
    {
        cJSON *latest =
            component_read(c, tb_ball_join(c, c->ball, "cache/component-operations/latest.json"));
        const char *saved = field(latest, "operationID");
        id = saved ? tb_ball_keep(c, strdup(saved)) : NULL;
        cJSON_Delete(latest);
    }
    if (!id)
        return NULL;
    if (!component_token(id))
    {
        tb_ball_fail(c, "Invalid component operation ID");
        return NULL;
    }
    cJSON *progress =
        component_read(c, tb_ball_format(c, "%s/cache/component-operations/%s.json", c->ball, id));
    if (progress)
        put(progress, "logPath",
            cJSON_CreateString(tb_ball_format(c, "%s/logs/os2-components/%s.json", c->ball, id)));
    return progress;
}

static int component_hash_matches(Context *c, const char *host, const cJSON *record,
                                  TbFileIdentity *identity)
{
    const char *expected = field(record, "sha256");
    if (!expected || strlen(expected) != 64)
        return 0;
    TbFileIdentity before, after;
    unsigned char digest[32];
    if (!tb_path_identity(host, &before) || !tb_ball_digest_file(c, host, digest) ||
        !tb_path_identity(host, &after) || memcmp(&before, &after, sizeof before))
        return 0;
    if (identity)
        *identity = after;
    return equal_case(tb_ball_hex(c, digest), expected);
}

/* A read-only polling request may revisit a touched file whose bytes are
   unchanged. Cache only hash-verified verdicts bound to its complete stat identity;
   removal always performs a fresh hash and never uses this bounded thread cache. */
static int component_poll_hash(Context *c, const char *host, const cJSON *record)
{
    typedef struct ComponentHashVerdict
    {
        char path[1024], sha256[65];
        TbFileIdentity identity;
        int matches;
    } ComponentHashVerdict;

    static _Thread_local ComponentHashVerdict verdicts[64];
    static _Thread_local unsigned next;
    TbFileIdentity before, after;
    const char *expected = field(record, "sha256");
    if (!expected || strlen(expected) != 64 || !tb_path_identity(host, &before))
        return 0;
    int cacheable = strlen(host) < sizeof verdicts[0].path;
    if (cacheable)
        for (unsigned i = 0; i < 64; i++)
            if (!strcmp(verdicts[i].path, host) && !strcmp(verdicts[i].sha256, expected) &&
                !memcmp(&verdicts[i].identity, &before, sizeof before))
                return verdicts[i].matches;
    int matches = component_hash_matches(c, host, record, NULL);
    if (cacheable && !c->error && tb_path_identity(host, &after) &&
        !memcmp(&before, &after, sizeof before))
    {
        ComponentHashVerdict *entry = &verdicts[next++ % 64];
        strcpy(entry->path, host);
        strcpy(entry->sha256, expected);
        entry->identity = after;
        entry->matches = matches;
    }
    return matches;
}

static cJSON *component_file_state_record(Context *c, const char *guest, const cJSON *record,
                                          int verify)
{
    cJSON *file = cJSON_CreateObject();
    cJSON_AddStringToObject(file, "path", guest);
    char *host = component_private_path(c, guest);
    const char *status = "missing", *reason = NULL;
    int present = host && tb_is_file(host), managed = 0;
    if (c->error)
    {
        reason = tb_ball_keep(c, strdup(c->error));
        free(c->error);
        c->error = NULL;
        status = "protected";
    }
    else if (present)
    {
        const char *hash = field(record, "sha256");
        if (!hash || strlen(hash) != 64 || !field(record, "path") ||
            strcmp(component_key(c, field(record, "path")), component_key(c, guest)))
        {
            status = "unowned";
            reason =
                "Installed before ownership tracking, or supplied separately; removal is protected.";
        }
        else
        {
            TbFileIdentity identity;
            int same =
                tb_path_identity(host, &identity) && component_same_identity(c, record, &identity);
            if ((same && !verify) || (verify ? component_hash_matches(c, host, record, NULL)
                                             : component_poll_hash(c, host, record)))
            {
                managed = 1;
                status = "managed";
            }
            else
            {
                status = "modified";
                reason = "The file changed after installation; it will be preserved.";
                if (c->error)
                {
                    reason = tb_ball_keep(c, strdup(c->error));
                    free(c->error);
                    c->error = NULL;
                    status = "protected";
                }
            }
        }
    }
    cJSON_AddBoolToObject(file, "present", present);
    cJSON_AddBoolToObject(file, "managed", managed);
    cJSON_AddStringToObject(file, "status", status);
    if (reason)
        cJSON_AddStringToObject(file, "reason", reason);
    const char *keys[] = {"category", "source", "sha256", NULL};
    for (unsigned i = 0; keys[i]; i++)
        if (field(record, keys[i]))
            cJSON_AddStringToObject(file, keys[i], field(record, keys[i]));
    return file;
}

static cJSON *component_file_state(Context *c, const char *guest, const cJSON *ledger, int verify)
{
    return component_file_state_record(c, guest, get(ledger, component_key(c, guest)), verify);
}

/* Source IDs are opaque media identities: lookup never reopens installation media. */
typedef struct ComponentSourceRecord
{
    const char *source;
    const cJSON *record;
} ComponentSourceRecord;

static int component_source_compare(const void *left, const void *right)
{
    const ComponentSourceRecord *a = left, *b = right;
    int result = strcmp(a->source, b->source);
    return result ? result : strcmp(field(a->record, "path"), field(b->record, "path"));
}

static void component_state_add(cJSON *result, cJSON *file)
{
    cJSON_AddItemToArray(get(result, "files"), file);
    const char *status = field(file, "status"), *path = field(file, "path");
    if (cJSON_IsTrue(get(file, "present")))
        cJSON_AddItemToArray(get(result, "installedPaths"), cJSON_CreateString(path));
    const char *key = !strcmp(status, "managed")     ? "managedPaths"
                      : !strcmp(status, "modified")  ? "modifiedPaths"
                      : !strcmp(status, "unowned")   ? "unownedPaths"
                      : !strcmp(status, "protected") ? "protectedPaths"
                                                     : NULL;
    if (key)
        cJSON_AddItemToArray(get(result, key), cJSON_CreateString(path));
}

cJSON *os2_component_state(Context *c, const cJSON *request)
{
    const cJSON *paths = get(request, "paths"), *selection = get(request, "selection"), *item;
    if (paths && (!tb_ball_array_strings(c, paths) || cJSON_GetArraySize(paths) > 100000))
    {
        tb_ball_fail(c, "Component state accepts at most 100000 file paths");
        return NULL;
    }
    if (selection && (!cJSON_IsArray(selection) || cJSON_GetArraySize(selection) > 100000))
    {
        tb_ball_fail(c, "Component state accepts at most 100000 source and destination pairs");
        return NULL;
    }
    cJSON_ArrayForEach(item, selection) if (!cJSON_IsObject(item) || !field(item, "source") ||
                                            !*field(item, "source") || !field(item, "path") ||
                                            !*field(item, "path"))
    {
        tb_ball_fail(c, "Each component selection needs a source and planned destination path");
        return NULL;
    }
    cJSON *result = cJSON_CreateObject();
    const char *arrays[] = {"files",
                            "installedPaths",
                            "managedPaths",
                            "modifiedPaths",
                            "unownedPaths",
                            "protectedPaths",
                            NULL};
    for (unsigned i = 0; arrays[i]; i++)
        cJSON_AddArrayToObject(result, arrays[i]);
    cJSON *ledger = NULL;
    if (cJSON_GetArraySize(paths) || cJSON_GetArraySize(selection))
    {
        if (!component_storage(c, "os2-components.json") ||
            !component_storage(c, "logs/os2-components/ownership.jsonl"))
        {
            cJSON_Delete(result);
            return NULL;
        }
        ledger = component_ledger(c);
        if (c->error)
        {
            cJSON_Delete(ledger);
            cJSON_Delete(result);
            return NULL;
        }
    }
    cJSON_ArrayForEach(item, paths)
        component_state_add(result, component_file_state(c, str(item), ledger, 0));
    ComponentSourceRecord *index = NULL;
    size_t count = 0;
    if (cJSON_GetArraySize(selection))
    {
        index = (ComponentSourceRecord *)tb_ball_keep(
            c, calloc((size_t)cJSON_GetArraySize(ledger) + 1, sizeof *index));
        if (index)
        {
            const cJSON *record;
            cJSON_ArrayForEach(record, ledger) if (field(record, "source") && field(record, "path"))
                index[count++] = (ComponentSourceRecord){field(record, "source"), record};
            qsort(index, count, sizeof *index, component_source_compare);
        }
    }
    size_t resolved_count = (size_t)cJSON_GetArraySize(paths);
    cJSON_ArrayForEach(item, selection)
    {
        if (c->error)
            break;
        const char *source = field(item, "source"), *planned = field(item, "path");
        size_t low = 0, high = count;
        while (low < high)
        {
            size_t middle = low + (high - low) / 2;
            if (strcmp(index[middle].source, source) < 0)
                low = middle + 1;
            else
                high = middle;
        }
        size_t end = low;
        while (end < count && !strcmp(index[end].source, source))
            end++;
        resolved_count += end > low ? end - low : 1;
        if (resolved_count > 100000)
        {
            tb_ball_fail(c, "Component state exceeds 100000 resolved file records");
            break;
        }
        int matched = 0;
        for (size_t at = low; at < end; at++)
        {
            cJSON *file = component_file_state_record(c, field(index[at].record, "path"),
                                                      index[at].record, 0);
            cJSON_AddStringToObject(file, "selectedSource", source);
            cJSON_AddStringToObject(file, "plannedPath", planned);
            component_state_add(result, file);
            matched = 1;
        }
        if (!matched)
        {
            cJSON *file = component_file_state(c, planned, ledger, 0);
            const cJSON *other = get(ledger, component_key(c, planned));
            if (other && strcmp(field(file, "status"), "protected"))
            {
                /* A canonical destination owned by another source is not proof
                   that this selected alternative was installed. */
                put(file, "present", cJSON_CreateFalse());
                put(file, "managed", cJSON_CreateFalse());
                put(file, "status", cJSON_CreateString("missing"));
                put(file, "reason",
                    cJSON_CreateString(
                        "This source is not installed; the canonical destination belongs to another source."));
                cJSON_DeleteItemFromObjectCaseSensitive(file, "source");
                cJSON_DeleteItemFromObjectCaseSensitive(file, "category");
                cJSON_DeleteItemFromObjectCaseSensitive(file, "sha256");
            }
            cJSON_AddStringToObject(file, "selectedSource", source);
            cJSON_AddStringToObject(file, "plannedPath", planned);
            component_state_add(result, file);
        }
    }
    cJSON_Delete(ledger);
    cJSON *progress = component_current_progress(c, request);
    cJSON_AddItemToObject(result, "progress", progress ? progress : cJSON_CreateNull());
    if (c->error)
    {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

cJSON *component_import(Context *c, const cJSON *request)
{
    cJSON *result = NULL;
    if (component_begin(c, request, "install") && !component_cancelled(c))
        result = get(request, "categories") || get(request, "fileSelection")
                     ? extract_os2_categories(c, request)
                     : import_os2_support(c, request);
    component_finish(c, result);
    if (c->error)
    {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

static int component_som_essential(const char *path)
{
    const char *base = strrchr(path, '\\');
    if (!base)
        return 0;
    static const char *libraries[] = {"SOM.DLL",   "SOMD.DLL",   "SOMIR.DLL",
                                      "SOMU.DLL",  "SOMUC.DLL",  "SOMEM.DLL",
                                      "SOMTC.DLL", "SOMSEC.DLL", NULL};
    if (!strncmp(path, "C:\\OS2\\DLL\\", 11))
        for (unsigned i = 0; libraries[i]; i++)
            if (equal_case(base + 1, libraries[i]))
                return 1;
    /* Messages are optional; daemon, server and repositories are service essentials. */
    for (unsigned i = 0; i < 7; i++)
        if (equal_case(path, os2_som_companions[i]))
            return 1;
    return 0;
}

cJSON *remove_os2_components(Context *c, const cJSON *request)
{
    cJSON *result = cJSON_CreateObject(), *removed = cJSON_AddArrayToObject(result, "removedPaths"),
          *preserved = cJSON_AddArrayToObject(result, "preserved");
    cJSON *owned_paths = NULL, *plan = NULL;
    const cJSON *paths = get(request, "paths"), *selected = get(request, "selection"), *item;
    if (!component_begin(c, request, "remove"))
        goto finished;
    if (selected)
    {
        if (paths || get(request, "fileSelection") || !cJSON_IsArray(selected) ||
            cJSON_GetArraySize(selected) > 100000)
        {
            tb_ball_fail(
                c,
                "Select up to 100000 exact sources without a separate paths or fileSelection list");
            goto finished;
        }
        cJSON_ArrayForEach(item, selected) if (!cJSON_IsObject(item) || !field(item, "source") ||
                                               !*field(item, "source") || !field(item, "path") ||
                                               !*field(item, "path"))
        {
            tb_ball_fail(c, "Each component selection needs a source and planned destination path");
            goto finished;
        }
        paths = owned_paths = cJSON_CreateArray();
        size_t capacity = (size_t)cJSON_GetArraySize(c->component_operation->ledger), count = 0,
               resolved = 0;
        ComponentSourceRecord *index = capacity ? malloc(capacity * sizeof *index) : NULL;
        if (index)
            tb_ball_keep(c, (char *)index);
        if (capacity && !index)
        {
            tb_ball_fail(c, "Cannot index installed component sources");
            goto finished;
        }
        const cJSON *record;
        cJSON_ArrayForEach(record, c->component_operation->ledger) if (field(record, "source") &&
                                                                       field(record, "path"))
            index[count++] = (ComponentSourceRecord){field(record, "source"), record};
        if (count > 1)
            qsort(index, count, sizeof *index, component_source_compare);
        cJSON_ArrayForEach(item, selected)
        {
            const char *source = field(item, "source"), *planned = field(item, "path");
            size_t low = 0, high = count;
            while (low < high)
            {
                size_t middle = low + (high - low) / 2;
                if (strcmp(index[middle].source, source) < 0)
                    low = middle + 1;
                else
                    high = middle;
            }
            int matched = 0;
            for (size_t at = low; at < count && !strcmp(index[at].source, source); at++)
            {
                if (resolved++ >= 100000)
                {
                    tb_ball_fail(
                        c, "Exact source selection resolves to more than 100000 destinations");
                    goto finished;
                }
                cJSON_AddItemToArray(owned_paths,
                                     cJSON_CreateString(field(index[at].record, "path")));
                matched = 1;
            }
            if (!matched)
            {
                cJSON *file = component_file_state(c, planned, c->component_operation->ledger, 0);
                put(file, "managed", cJSON_CreateFalse());
                put(file, "status",
                    cJSON_CreateString(cJSON_IsTrue(get(file, "present")) ? "protected"
                                                                          : "missing"));
                put(file, "reason",
                    cJSON_CreateString(
                        "This source has no installed ownership record; existing files were preserved."));
                cJSON_AddStringToObject(file, "selectedSource", source);
                cJSON_AddItemToArray(preserved, file);
            }
        }
    }
    else if (!paths && get(request, "fileSelection"))
    {
        plan = plan_os2_extraction(c, request);
        ExtractFileSelection selection = {0};
        if (!plan || !extract_selection(c, request, plan, &selection))
            goto finished;
        paths = owned_paths = cJSON_CreateArray();
        int categories[EXTRACT_COUNT] = {0};
        cJSON_ArrayForEach(
            item, get(plan, "files")) if (extract_file_selected(&selection, categories, item))
        {
            int matched = 0;
            const cJSON *record;
            cJSON_ArrayForEach(record,
                               c->component_operation->ledger) if (field(record, "source") &&
                                                                   field(record, "path") &&
                                                                   !strcmp(field(record, "source"),
                                                                           field(item, "source")))
            {
                cJSON_AddItemToArray(owned_paths, cJSON_CreateString(field(record, "path")));
                matched = 1;
            }
            if (!matched)
            {
                const char *destination = field(item, "path");
                const cJSON *other =
                    get(c->component_operation->ledger, component_key(c, destination));
                if (other)
                {
                    cJSON *file =
                        component_file_state(c, destination, c->component_operation->ledger, 0);
                    put(file, "managed", cJSON_CreateFalse());
                    put(file, "status", cJSON_CreateString("protected"));
                    put(file, "reason",
                        cJSON_CreateString(
                            "The selected source does not own the file installed at this destination."));
                    cJSON_AddStringToObject(file, "selectedSource", field(item, "source"));
                    cJSON_AddItemToArray(preserved, file);
                }
                else
                    cJSON_AddItemToArray(owned_paths, cJSON_CreateString(destination));
            }
        }
    }
    if (!paths || !tb_ball_array_strings(c, paths) || cJSON_GetArraySize(paths) > 100000)
    {
        tb_ball_fail(c, "Select up to 100000 component file paths to remove");
        goto finished;
    }
    put(c->component_operation->progress, "totalCount",
        cJSON_CreateNumber(cJSON_GetArraySize(paths)));
    int remove_service = 0;
    cJSON *seen = cJSON_CreateObject();
    cJSON_ArrayForEach(item, paths)
    {
        if (component_cancelled(c))
            break;
        const char *guest = str(item);
        char *key = component_key(c, guest);
        if (get(seen, key))
            continue;
        cJSON_AddTrueToObject(seen, key);
        component_progress(c, "removing", NULL, guest);
        if (c->error)
            break;
        cJSON *file = component_file_state(c, guest, c->component_operation->ledger, 1);
        if (!cJSON_IsTrue(get(file, "managed")))
        {
            cJSON_AddItemToArray(preserved, file);
            continue;
        }
        char *host = component_private_path(c, guest);
        TbFileIdentity identity;
        const cJSON *record = get(c->component_operation->ledger, key);
        if (!host || !component_hash_matches(c, host, record, &identity) ||
            !tb_remove_managed_file(host, &identity))
        {
            if (!c->error)
            {
                put(file, "managed", cJSON_CreateFalse());
                put(file, "status", cJSON_CreateString("protected"));
                put(file, "reason",
                    cJSON_CreateString(
                        "The file changed or could not be removed; it was preserved."));
                cJSON_AddItemToArray(preserved, file);
            }
            else
                cJSON_Delete(file);
            continue;
        }
        cJSON_Delete(file);
        cJSON *tombstone = cJSON_CreateObject();
        cJSON_AddStringToObject(tombstone, "path", guest);
        cJSON_AddTrueToObject(tombstone, "removed");
        cJSON_AddStringToObject(tombstone, "operationID", c->component_operation->id);
        component_journal(c, tombstone);
        cJSON_Delete(tombstone);
        cJSON_AddItemToArray(removed, cJSON_CreateString(guest));
        remove_service |= component_som_essential(key);
    }
    cJSON_Delete(seen);
    if (remove_service)
    {
        cJSON *programs = get(c->manifest, "startupPrograms");
        for (int i = cJSON_GetArraySize(programs) - 1; i >= 0; i--)
        {
            cJSON *service = cJSON_GetArrayItem(programs, i);
            if (field(service, "owner") && field(service, "program") &&
                equal_case(field(service, "owner"), "os2-som") &&
                equal_case(field(service, "program"), "C:\\OS2\\SOMDD.EXE"))
                cJSON_DeleteItemFromArray(programs, i);
        }
        tb_ball_save_manifest(c);
    }
finished:
    cJSON_Delete(plan);
    cJSON_Delete(owned_paths);
    component_finish(c, result);
    if (c->error)
    {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

cJSON *cancel_os2_component_operation(Context *c, const cJSON *request)
{
    const char *id = field(request, "operationID");
    if (!component_token(id))
    {
        tb_ball_fail(c, "Invalid component operation ID");
        return NULL;
    }
    if (!component_storage(c, "cache/component-operations"))
        return NULL;
    char *directory = tb_ball_join(c, c->ball, "cache/component-operations");
    if (!tb_mkdirs(directory) ||
        !tb_atomic_write(tb_ball_format(c, "%s/%s.cancel", directory, id), "cancel\n", 7))
    {
        tb_ball_fail(c, "Cannot request component operation cancellation");
        return NULL;
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "operationID", id);
    cJSON_AddTrueToObject(result, "cancelRequested");
    cJSON *progress = component_current_progress(c, request);
    cJSON_AddItemToObject(result, "progress", progress ? progress : cJSON_CreateNull());
    return result;
}
