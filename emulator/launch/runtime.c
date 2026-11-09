/* Public domain. No rights reserved. Wholly AI-authored. */
/* Runtime build identity and the per-ball runtime snapshot. */
#include "ball/store.h"
#include "emulator/launch/runtime.h"
#include "ball/vendor/sha256/sha256.h"

static int runtime_name(const char *s)
{
    size_t n = strlen(s);
    return !strcmp(s, "runtime-capabilities.json") || !strcmp(s, "lx_loader") ||
           !strcmp(s, "lx_loader.exe") || (n > 4 && !strcmp(s + n - 4, ".dll")) ||
           (!strncmp(s, "lib", 3) &&
            ((n > 3 && !strcmp(s + n - 3, ".so")) || (n > 6 && !strcmp(s + n - 6, ".dylib"))));
}

const char *runtime_loader_name(TbBall *c, const char *build)
{
    if (tb_is_file(tb_ball_join(c, build, "lx_loader")))
        return "lx_loader";
    if (tb_is_file(tb_ball_join(c, build, "lx_loader.exe")))
        return "lx_loader.exe";
    tb_ball_fail(c, "Choose a build directory containing lx_loader");
    return NULL;
}

char **runtime_names(const char *build, size_t *count)
{
    size_t n = 0;
    char **names = tb_names(build, &n);
    size_t kept = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (runtime_name(names[i]))
            names[kept++] = names[i];
        else
            free(names[i]);
    }
    if (kept > 1)
        qsort(names, kept, sizeof(*names), compare_names);
    *count = kept;
    return names;
}

char *runtime_id(TbBall *c, const char *build)
{
    size_t n = 0;
    char **names = runtime_names(build, &n);
    SHA256_CTX state;
    sha256_init(&state);
    int ok = 1;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char hash[32];
        if (!tb_ball_digest_file(c, tb_ball_join(c, build, names[i]), hash))
        {
            ok = 0;
            break;
        }
        sha256_update(&state, (const BYTE *)names[i], strlen(names[i]));
        sha256_update(&state, hash, 32);
    }
    tb_names_free(names, n);
    unsigned char out[32];
    sha256_final(&state, out);
    return ok ? tb_ball_hex(c, out) : NULL;
}

int runtime_sync(TbBall *c, const char *build, const char *directory)
{
    if (!tb_mkdirs(directory))
        return tb_ball_fail(c, "Cannot create runtime snapshot");
    size_t n = 0;
    char **names = runtime_names(build, &n);
    int ok = 1;
    for (size_t i = 0; i < n; i++)
    {
        char *src = tb_ball_join(c, build, names[i]), *dst = tb_ball_join(c, directory, names[i]);
        unsigned char a[32], b[32];
        if (!tb_ball_digest_file(c, src, a))
        {
            ok = 0;
            break;
        }
        if (tb_is_file(dst))
        {
            if (!tb_ball_digest_file(c, dst, b))
            {
                ok = 0;
                break;
            }
            if (!memcmp(a, b, 32))
                continue;
        }
        if (!tb_copy(src, dst))
        {
            ok = tb_ball_fail(c, "Cannot snapshot runtime file: %s", names[i]);
            break;
        }
    }
    tb_names_free(names, n);
    return ok;
}

cJSON *runtime_files(TbBall *c, const cJSON *request)
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

cJSON *runtime_sync_request(TbBall *c, const cJSON *request)
{
    const char *build = field(request, "build"), *destination = field(request, "directory");
    if (!build || !destination)
    {
        tb_ball_fail(c, "Build and destination are required");
        return NULL;
    }
    return runtime_sync(c, build, destination) ? cJSON_CreateBool(1) : NULL;
}
