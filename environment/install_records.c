/* Public domain. No rights reserved. Wholly AI-authored. */
/* Installer receipts: the record of the last installer a ball ran. */
#include "ball/store.h"
#include "environment/install_records.h"
#include "environment/dependencies/audit.h"
#include "environment/wps_sync.h"
#include "ball/vendor/sha256/sha256.h"

int environment_record_install(TbBall *c, const cJSON *request)
{
    cJSON *receipt = get(request, "receipt"), *exit = get(request, "exitCode");
    const char *program = field(request, "programHost");
    if (!cJSON_IsObject(receipt) || !cJSON_IsNumber(exit) || !program)
    {
        tb_ball_fail(c, "A launch receipt, program and actual exit code are required");
        return 0;
    }
    cJSON *saved = cJSON_Duplicate(receipt, 1);
    unsigned char digest[32];
    if (!field(saved, "program_sha256") && !tb_ball_digest_file(c, program, digest))
    {
        cJSON_Delete(saved);
        return 0;
    }
    if (!field(saved, "program_sha256"))
        put(saved, "program_sha256", cJSON_CreateString(tb_ball_hex(c, digest)));
    put(saved, "exit_code", cJSON_Duplicate(exit, 1));
    put(saved, "finished", cJSON_CreateString(tb_ball_now(c)));
    if (!wps_sync(c))
    {
        cJSON_Delete(saved);
        return 0;
    }
    cJSON *registered = wps_latest(c);
    if (registered)
        put(saved, "applicationRegistration", registered);
    cJSON *audit = audit_dependencies(c, request, exit->valuedouble == 0);
    if (audit)
    {
        put(saved, "dependencyAudit", dependency_compact(audit));
        cJSON_Delete(audit);
    }
    int ok = tb_ball_write_json(c, tb_ball_join(c, c->ball, "logs/last-install.json"), saved);
    cJSON_Delete(saved);
    return ok;
}
