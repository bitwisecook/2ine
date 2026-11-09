/* Public domain. No rights reserved. Wholly AI-authored. */
/* The work the facade layers on the ball store for each request: derived
   files, settings validation, shell registrations and the client snapshot. */
#include "ball/ball_internal.h"
#include "ball/facade/facade.h"
#include "emulator/launch/host_environment.h"
#include "environment/dependencies/audit.h"
#include "environment/media/support.h"
#include "emulator/launch/runtime.h"
#include "emulator/launch/runtime_config.h"
#include "environment/wps_sync.h"

static int facade_regenerate(TbBall *c)
{
    return launch_write_runtime_config(c);
}

/* Settings are checked by composing the launch environment they produce. */
static int facade_validate_settings(TbBall *c, const cJSON *request)
{
    const char *build = field(request, "build");
    char *absolute = tb_ball_resolve_path(c, build && *build ? build : ".", 0);
    if (!absolute)
        return 0;
    cJSON *env = launch_host_environment(c, absolute, field(request, "backend"), NULL);
    if (!env)
        return 0;
    cJSON_Delete(env);
    return 1;
}

/* The store's fields, with the Workplace Shell projection, the OS/2 support
   state and the last dependency audit inserted after the manifest. */
cJSON *facade_snapshot(TbBall *c)
{
    if (!tb_ball_roots(c) || !wps_sync(c))
        return NULL;
    cJSON *ball = tb_ball_snapshot(c);
    if (!ball)
        return NULL;
    cJSON *value = cJSON_CreateObject();
    if (c->registration_warning)
        cJSON_AddStringToObject(value, "applicationRegistrationWarning", c->registration_warning);
    cJSON_AddItemToObject(value, "manifest",
                          cJSON_DetachItemFromObjectCaseSensitive(ball, "manifest"));
    cJSON *registration = wps_latest(c);
    if (registration)
        cJSON_AddItemToObject(value, "applicationRegistration", registration);
    cJSON_AddItemToObject(value, "os2Support", os2_support(c));
    cJSON *dependency_audit = dependency_audit_latest(c);
    if (dependency_audit)
        cJSON_AddItemToObject(value, "dependencyAudit", dependency_audit);
    while (ball->child)
    {
        cJSON *item = ball->child;
        const char *name = item->string;
        cJSON_AddItemToObject(value, name, cJSON_DetachItemViaPointer(ball, item));
    }
    cJSON_Delete(ball);
    return value;
}

static void facade_describe_runtime(TbBall *c, const char *build, cJSON *runtime)
{
    int available = build && (tb_is_file(tb_ball_join(c, build, "lx_loader")) ||
                              tb_is_file(tb_ball_join(c, build, "lx_loader.exe")));
    cJSON_AddBoolToObject(runtime, "available", available);
    if (available)
    {
        char *identity = runtime_id(c, build);
        if (identity)
            cJSON_AddStringToObject(runtime, "identitySHA256", identity);
        const char *loader = runtime_loader_name(c, build);
        unsigned char hash[TB_BALL_DIGEST_SIZE];
        if (loader && tb_ball_digest_file(c, tb_ball_join(c, build, loader), hash))
            cJSON_AddStringToObject(runtime, "loaderSHA256", tb_ball_hex(c, hash));
        char *path = tb_ball_join(c, build, "2ine-build.json");
        if (path && tb_is_file(path))
        {
            char *text = tb_ball_read_file(c, path, NULL, 0);
            cJSON *details = text ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
            if (details)
                cJSON_AddItemToObject(runtime, "buildInformation", details);
            else
                tb_ball_fail(c, "Invalid runtime build information: %s", path);
        }
    }
}

const TbBallHooks facade_hooks = {
    .regenerate = facade_regenerate,
    .validate_settings = facade_validate_settings,
    .snapshot = facade_snapshot,
    .detach_application = wps_detach,
    .hide_application = wps_hide,
    .describe_runtime = facade_describe_runtime,
};
