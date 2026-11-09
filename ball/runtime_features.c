/* Public domain. No rights reserved. Wholly AI-authored. */
/* Receiving-host capability resolution is read-only; saved preferences stay portable. */
#include "ball/ball_internal.h"
#include "ball/guest_environment.h"
#include "ball/os2_dependencies.h"
#include "ball/runtime_features.h"
#include "ball/settings_schema.h"

static int runtime_feature_bool(const cJSON *value, int fallback)
{
    if (!value)
        return fallback;
    if (cJSON_IsBool(value))
        return cJSON_IsTrue(value);
    const char *text = str(value);
    if (!text)
        return cJSON_IsNumber(value) ? value->valuedouble != 0 : fallback;
    return equal_case(text, "1") || equal_case(text, "yes") || equal_case(text, "y") ||
           equal_case(text, "true") || equal_case(text, "t") || equal_case(text, "on");
}

cJSON *runtime_feature_metadata(Context *c, const char *build)
{
    const char *path = tb_ball_join(c, build, "runtime-capabilities.json");
    if (!tb_is_file(path))
        return cJSON_CreateObject();
    FILE *file = tb_fopen(path, "rb");
    char text[16384];
    size_t size = file ? fread(text, 1, sizeof text - 1, file) : 0;
    int ok = file && !ferror(file) && feof(file);
    if (file)
        fclose(file);
    text[size] = 0;
    cJSON *metadata = ok && size < sizeof text - 1 ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
    if (!cJSON_IsObject(metadata) || !cJSON_IsNumber(get(metadata, "schema")) ||
        get(metadata, "schema")->valuedouble != 1 ||
        (get(metadata, "features") && !cJSON_IsObject(get(metadata, "features"))))
    {
        cJSON_Delete(metadata);
        tb_ball_fail(c,
                     "Invalid runtime-capabilities.json. Rebuild or choose another host runtime.");
        return NULL;
    }
    return metadata;
}

static int runtime_has_module(Context *c, const char *build, const char *module)
{
    return tb_is_file(tb_ball_join(c, build, tb_ball_format(c, "lib%s.so", module)));
}

void runtime_feature_policy(Context *c, const char *build)
{
    (void)build;
    c->opengl_policy = field(get(c->manifest, "system"), "opengl");
    if (!c->opengl_policy)
        c->opengl_policy = "mesa";
}

cJSON *runtime_features(Context *c, const char *build)
{
    if (!launch_validate_system(c))
        return NULL;
    if (!build || !*build)
    {
        tb_ball_fail(c, "A receiving host runtime is required");
        return NULL;
    }
    cJSON *metadata = runtime_feature_metadata(c, build);
    if (!metadata)
        return NULL;
    const cJSON *compiled = get(metadata, "features"), *settings = get(c->manifest, "system");
    cJSON *result = cJSON_CreateObject();
    const char *names[] = {"video", "pcm", "midi", "dive"};
    const char *modules[] = {"mdm", "mdm", "mdm", "dive"};
    for (unsigned i = 0; i < 4; i++)
    {
        const cJSON *capability = get(compiled, names[i]);
        int available =
            capability ? cJSON_IsTrue(capability) : runtime_has_module(c, build, modules[i]);
        if (!capability && i == 0)
            available &= tb_is_file(tb_ball_join(c, build, "ffmpeg-build.json"));
        cJSON *feature = cJSON_AddObjectToObject(result, names[i]);
        const cJSON *requested = get(settings, names[i]);
        cJSON_AddItemToObject(feature, "requested",
                              requested
                                  ? cJSON_CreateBool(runtime_feature_bool(requested, available))
                                  : cJSON_CreateNull());
        cJSON_AddBoolToObject(feature, "default", available);
        cJSON_AddBoolToObject(feature, "effective",
                              available && runtime_feature_bool(requested, available));
        cJSON_AddBoolToObject(feature, "available", available);
        cJSON_AddStringToObject(
            feature, "reason",
            available ? "" : "This receiving runtime was built without this feature.");
    }
    cJSON *errors = cJSON_CreateArray(), *paths = cJSON_CreateArray(),
          *cache = cJSON_CreateObject();
    cJSON *env = tb_ball_guest_environment(c, get(c->application, "environment"));
    if (env)
    {
        dependency_split_paths(c, paths, field(env, "BEGINLIBPATH"));
        dependency_split_paths(c, paths, field(env, "LIBPATH"));
        dependency_split_paths(c, paths, field(env, "ENDLIBPATH"));
        cJSON_Delete(env);
    }
    const char *program = field(c->application, "program");
    char *original = dependency_find(c, program ? program : "C:\\OS2\\DLL\\OPENGL.DLL", "OPENGL",
                                     paths, cache, errors);
    cJSON_Delete(paths);
    cJSON_Delete(cache);
    cJSON_Delete(errors);
    int mesa = get(compiled, "mesa") ? cJSON_IsTrue(get(compiled, "mesa"))
                                     : runtime_has_module(c, build, "opengl");
    const char *default_provider = "mesa";
    const char *requested = field(settings, "opengl");
    const char *selected = requested ? requested : default_provider;
    int available = !strcmp(selected, "mesa")       ? mesa
                    : !strcmp(selected, "original") ? original != NULL
                                                    : 1;
    cJSON *gl = cJSON_AddObjectToObject(result, "opengl"),
          *providers = cJSON_AddObjectToObject(gl, "providers");
    cJSON_AddItemToObject(gl, "requested",
                          requested ? cJSON_CreateString(requested) : cJSON_CreateNull());
    cJSON_AddStringToObject(gl, "default", default_provider);
    cJSON_AddStringToObject(gl, "effective", available ? selected : "off");
    cJSON_AddBoolToObject(gl, "available", available);
    const char *provider_names[] = {"mesa", "original", "off"};
    for (unsigned i = 0; i < 3; i++)
    {
        cJSON *provider = cJSON_AddObjectToObject(providers, provider_names[i]);
        int present = i == 0 ? mesa : i == 1 ? original != NULL : 1;
        cJSON_AddBoolToObject(provider, "available", present);
        cJSON_AddStringToObject(
            provider, "reason",
            i == 0 && !mesa ? "Choose a runtime with Mesa OpenGL support."
            : i == 1
                ? original
                      ? "User-supplied OS/2 renderer; compatibility is not yet qualified."
                      : "Extract original OS/2 OPENGL.DLL and its dependencies from your media."
                : "");
        if (i == 1)
        {
            cJSON_AddBoolToObject(provider, "qualified", 0);
            if (original)
                cJSON_AddStringToObject(provider, "path", original);
        }
    }
    cJSON_AddStringToObject(gl, "reason", field(get(providers, selected), "reason"));
    cJSON_Delete(metadata);
    return result;
}

int runtime_features_validate(Context *c, const char *build)
{
    cJSON *features = runtime_features(c, build), *feature;
    if (!features)
        return 0;
    cJSON_ArrayForEach(feature, features)
    {
        const cJSON *requested = get(feature, "requested");
        if (requested && !cJSON_IsNull(requested) &&
            (cJSON_IsString(requested) || cJSON_IsTrue(requested)) &&
            !cJSON_IsTrue(get(feature, "available")))
        {
            tb_ball_fail(c, "Selected %s feature is unavailable: %s", feature->string,
                         field(feature, "reason"));
            break;
        }
    }
    cJSON_Delete(features);
    return !c->error;
}
