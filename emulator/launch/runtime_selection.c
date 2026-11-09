/* Public domain. No rights reserved. Wholly AI-authored. */
/* A saved backend is a portable preference; explicit launch overrides are strict. */
#include "ball/store.h"
#include "emulator/launch/runtime_selection.h"

const char *runtime_effective_backend(TbBall *c, const char *build, const char *requested,
                                      int saved)
{
    unsigned available = 0;
    char default_backend[16] = "default";
    const char *path = tb_ball_join(c, build, "runtime-capabilities.json");
    int declared = tb_is_file(path);
    if (declared)
    {
        FILE *file = tb_fopen(path, "rb");
        char text[4096];
        size_t size = file ? fread(text, 1, sizeof text - 1, file) : 0;
        int ok = file && !ferror(file) && feof(file);
        if (file)
            fclose(file);
        text[size] = 0;
        cJSON *metadata = ok && size < sizeof text - 1 ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
        cJSON *backends = get(metadata, "backends"), *entry;
        const char *name = field(metadata, "defaultBackend");
        int valid = cJSON_IsObject(metadata) && cJSON_IsNumber(get(metadata, "schema")) &&
                    get(metadata, "schema")->valuedouble == 1 && cJSON_IsArray(backends) &&
                    cJSON_GetArraySize(backends) > 0 && cJSON_GetArraySize(backends) <= 2;
        cJSON_ArrayForEach(entry, backends)
        {
            const char *backend = str(entry);
            if (backend && !strcmp(backend, "native"))
                available |= 1;
            else if (backend && !strcmp(backend, "unicorn"))
                available |= 2;
            else
                valid = 0;
        }
        if (!name || (strcmp(name, "native") && strcmp(name, "unicorn")) ||
            !(available & (!strcmp(name, "native") ? 1 : 2)))
            valid = 0;
        if (valid)
            snprintf(default_backend, sizeof default_backend, "%s", name);
        cJSON_Delete(metadata);
        if (!valid)
        {
            tb_ball_fail(
                c, "Invalid runtime-capabilities.json. Rebuild or choose another host runtime.");
            return NULL;
        }
    }
    if (!requested || !*requested || !strcmp(requested, "default"))
        requested = default_backend;
    if (strcmp(requested, "default") && strcmp(requested, "native") && strcmp(requested, "unicorn"))
    {
        tb_ball_fail(c, "runtime.backend must be native or unicorn");
        return NULL;
    }
    if (!strcmp(requested, "native") &&
        (!tb_host_native_execution() || (declared && !(available & 1))))
    {
        if (saved && declared && (available & 2))
            return "unicorn";
        tb_ball_fail(
            c,
            "Native execution is unavailable in this host runtime. Choose a runtime with Unicorn or select another backend.");
        return NULL;
    }
    if (!strcmp(requested, "unicorn") && declared && !(available & 2))
    {
        tb_ball_fail(
            c,
            "This runtime does not include Unicorn. Choose a runtime built with TWOINE_PORTABLE_LOADER=ON.");
        return NULL;
    }
    return tb_ball_keep(c, strdup(requested));
}
