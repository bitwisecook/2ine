/* Public domain. No rights reserved. Wholly AI-authored. */
/* Metadata freshness for installed imports and their configuration/media inputs. */
#include "ball/store.h"
#include "storage/container.h"
#include "ball/dependency_freshness.h"
#include "ball/os2_dependencies.h"
#include "ball/vendor/sha256/sha256.h"

static void dependency_token_text(SHA256_CTX *state, const char *text)
{
    const char *value = text ? text : "";
    sha256_update(state, (const BYTE *)value, strlen(value) + 1);
}

static int dependency_token_order(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int dependency_token_file(TbBall *c, SHA256_CTX *state, const char *path)
{
    TbFileIdentity identity;
    dependency_token_text(state, path);
    if (!tb_is_file(path))
    {
        dependency_token_text(state, "absent");
        return 1;
    }
    if (tb_path_identity(path, &identity))
    {
        sha256_update(state, (const BYTE *)&identity, sizeof identity);
        return 1;
    }
    uint64_t size = 0;
    int64_t modified = 0;
    if (!tb_path_info(path, &size, &modified))
        return 0;
    char *value = tb_ball_format(c, "%llu:%lld", (unsigned long long)size, (long long)modified);
    dependency_token_text(state, value);
    return value != NULL;
}

static int dependency_token_tree(TbBall *c, SHA256_CTX *state, const char *root, int installed,
                                 int recursive, unsigned *visited, unsigned *binaries)
{
    cJSON *queue = cJSON_CreateArray();
    cJSON_AddItemToArray(queue, cJSON_CreateString(root));
    int ok = 1;
    for (int at = 0; ok && at < cJSON_GetArraySize(queue); at++)
    {
        const char *directory = str(cJSON_GetArrayItem(queue, at));
        size_t count = 0;
        char **names = tb_names(directory, &count);
        if ((!names && count) || count == (size_t)-1 || !tb_is_dir(directory))
        {
            ok = 0;
            break;
        }
        if (count > 1)
            qsort(names, count, sizeof *names, dependency_token_order);
        for (size_t i = 0; ok && i < count; i++)
        {
            if (++*visited > DEPENDENCY_ENTRY_LIMIT)
            {
                ok = 0;
                break;
            }
            char *child = tb_ball_join(c, directory, names[i]);
            if (!child)
            {
                ok = 0;
                break;
            }
            if (tb_is_link(child))
            {
                dependency_token_text(state, child);
                dependency_token_text(state, "link-skipped");
            }
            else if (tb_is_dir(child))
            {
                char *cache = tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "OS2/DLL/2INE");
                if (recursive && (!installed || !equal_case(child, cache)))
                    cJSON_AddItemToArray(queue, cJSON_CreateString(child));
            }
            else if (!installed || dependency_candidate(names[i]))
            {
                if (installed && ++*binaries > DEPENDENCY_BINARY_LIMIT)
                    ok = 0;
                else
                    ok = dependency_token_file(c, state, child);
            }
        }
        tb_names_free(names, count);
    }
    cJSON_Delete(queue);
    return ok;
}

static int dependency_token_contents(TbBall *c, SHA256_CTX *state, const char *path)
{
    dependency_token_text(state, path);
    uint64_t length = 0;
    int64_t modified = 0;
    if (tb_is_file(path) && (!tb_path_info(path, &length, &modified) || length > 1024 * 1024))
        return 0;
    size_t size = 0;
    char *bytes = tb_ball_read_file(c, path, &size, 1);
    if (!bytes)
        return 0;
    sha256_update(state, (const BYTE *)bytes, size);
    dependency_token_text(state, "");
    return 1;
}

/* A failed/limited metadata pass cannot establish freshness. It is isolated
   from the caller so a stale report does not break snapshot or app launch. */
char *dependency_fingerprint(TbBall *parent, const cJSON *sources, const char *build,
                             const cJSON *component_files)
{
    TbBall storage = *parent;
    storage.allocations = NULL;
    storage.count = 0;
    storage.error = NULL;
    TbBall *c = &storage;
    SHA256_CTX state;
    unsigned char digest[32];
    sha256_init(&state);
    dependency_token_text(&state, "2ine-installed-dependencies-v3");
    unsigned visited = 0, binaries = 0;
    int ok = dependency_token_tree(c, &state, c->roots[TB_BALL_DRIVE_C], 1, 1, &visited, &binaries);
    static const char *keys[] = {"environment", "system",          "runtime",
                                 "drives",      "startupPrograms", NULL};
    for (int i = 0; ok && keys[i]; i++)
    {
        char *encoded = cJSON_PrintUnformatted(get(c->manifest, keys[i]));
        dependency_token_text(&state, keys[i]);
        dependency_token_text(&state, encoded);
        cJSON_free(encoded);
    }
    ok = ok &&
         dependency_token_contents(c, &state,
                                   tb_ball_join(c, c->roots[TB_BALL_DRIVE_C], "CONFIG.SYS")) &&
         dependency_token_contents(c, &state, tb_ball_join(c, c->ball, "os2-media.json"));
    cJSON *errors = cJSON_CreateArray(), *paths = dependency_search_paths(c, errors);
    const cJSON *item;
    cJSON_ArrayForEach(item, paths)
    {
        const char *guest = str(item);
        if (!guest || strlen(guest) < 2 || toupper((unsigned char)guest[0]) == 'C')
            continue;
        char *host = tb_ball_host_path(c, guest, 0);
        dependency_token_text(&state, host);
        if (host && tb_is_dir(host))
            ok = ok && dependency_token_tree(c, &state, host, 1, 0, &visited, &binaries);
    }
    cJSON_Delete(paths);
    ok = ok && !cJSON_GetArraySize(errors);
    cJSON_Delete(errors);
    cJSON_ArrayForEach(item, sources)
    {
        const char *source = field(item, "path");
        dependency_token_text(&state, source);
        char *physical = source ? tb_ball_keep(c, tb_media_host_source(source)) : NULL;
        if (physical && tb_is_dir(physical))
            ok = ok && dependency_token_tree(c, &state, physical, 0, 1, &visited, &binaries);
        else if (physical)
            ok = ok && dependency_token_file(c, &state, physical);
        else
            dependency_token_text(&state, "unavailable-source");
    }
    cJSON_ArrayForEach(item, component_files)
    {
        const char *guest = field(item, "path");
        char *host = guest ? tb_ball_host_path(c, guest, 0) : NULL;
        if (host)
            ok = ok && dependency_token_file(c, &state, host);
        else
            ok = 0;
    }
    dependency_token_text(&state, build);
    if (build)
        ok = ok &&
             dependency_token_contents(c, &state,
                                       tb_ball_join(c, build, "runtime-capabilities.json")) &&
             dependency_token_contents(c, &state, tb_ball_join(c, build, "2ine-build.json"));
    char *result = NULL;
    if (ok && !c->error)
    {
        sha256_final(&state, digest);
        result = tb_ball_format(parent, "metadata-v3:%s", tb_ball_hex(c, digest));
    }
    for (size_t i = 0; i < c->count; i++)
        free(c->allocations[i]);
    free(c->allocations);
    free(c->error);
    return result;
}

static void dependency_mark_freshness(TbBall *c, cJSON *report)
{
    const char *recorded = field(report, "fingerprint");
    char *current =
        recorded ? dependency_fingerprint(c, get(report, "media"), field(report, "runtimeBuild"),
                                          get(report, "componentFiles"))
                 : NULL;
    int stale =
        cJSON_IsTrue(get(report, "stale")) || !recorded || !current || strcmp(recorded, current);
    put(report, "stale", cJSON_CreateBool(stale));
    if (stale)
    {
        put(report, "complete", cJSON_CreateBool(0));
        put(report, "summary",
            cJSON_CreateString(
                "Dependency audit is stale; installed binaries, search settings or media changed. Check libraries again."));
    }
}
