/* Public domain. No rights reserved. Wholly AI-authored. */
/* Original DSOM package companions and ball-owned service configuration. */
#include "ball/store.h"
#include "environment/components/som.h"

const char *os2_som_companions[] = {"C:\\OS2\\SOMDD.EXE",
                                    "C:\\OS2\\SOMDSVR.EXE",
                                    "C:\\OS2\\ETC\\SOM.IR",
                                    "C:\\OS2\\ETC\\DSOM\\SOMDIMPL.DAT",
                                    "C:\\OS2\\ETC\\DSOM\\SOMDIMPL.TOC",
                                    "C:\\OS2\\ETC\\DSOM\\SOMDCLS.DAT",
                                    "C:\\OS2\\ETC\\DSOM\\SOMDCLS.TOC",
                                    "C:\\OS2\\SYSTEM\\SOMD.MSG",
                                    "C:\\OS2\\SYSTEM\\SOMK.MSG",
                                    "C:\\OS2\\SYSTEM\\SOME_OS2.MSG",
                                    NULL};

int os2_som_companion(const char *path)
{
    for (int i = 0; os2_som_companions[i]; i++)
        if (equal_case(path, os2_som_companions[i]))
            return 1;
    return 0;
}

int os2_som_present(TbBall *c, const char *guest)
{
    char *host = tb_ball_host_path(c, guest, 0);
    return host && tb_is_file(host);
}

int os2_som_complete(TbBall *c)
{
    if (!os2_som_present(c, "C:\\OS2\\DLL\\SOMD.DLL"))
        return 0;
    for (int i = 0; os2_som_companions[i]; i++)
        if (!os2_som_present(c, os2_som_companions[i]))
            return 0;
    return 1;
}

int os2_som_provision(TbBall *c, int *changed)
{
    if (changed)
        *changed = 0;
    if (!os2_som_complete(c))
        return !c->error;
    cJSON *programs = get(c->manifest, "startupPrograms");
    if (programs && !cJSON_IsArray(programs))
        return tb_ball_fail(c, "Cannot configure DSOM: startupPrograms must be an array");
    cJSON *service;
    cJSON_ArrayForEach(service, programs)
    {
        /* A user-owned daemon record takes precedence, including its options.
           The existing managed record is also left intact on repeated imports. */
        const char *program = field(service, "program");
        if (program && equal_case(program, "C:\\OS2\\SOMDD.EXE"))
            return 1;
    }
    if (cJSON_GetArraySize(programs) >= 16)
        return tb_ball_fail(c, "Cannot configure DSOM: the ball already has 16 startup programs");
    cJSON *effective = tb_ball_guest_environment(c, NULL);
    if (!effective)
        return 0;
    if (!programs)
        programs = cJSON_AddArrayToObject(c->manifest, "startupPrograms");
    service = cJSON_CreateObject();
    cJSON_AddStringToObject(service, "owner", "os2-som");
    cJSON_AddStringToObject(service, "program", "C:\\OS2\\SOMDD.EXE");
    cJSON_AddStringToObject(service, "cwd", "C:\\OS2");
    cJSON_AddArrayToObject(service, "arguments");
    cJSON *environment = cJSON_AddObjectToObject(service, "environment");
    /* Resolve against the receiving launch's inherited environment, so later
       application installations can add repositories without stale copies. */
    cJSON_AddStringToObject(environment, "SOMIR", "C:\\OS2\\ETC\\SOM.IR;%SOMIR%");
    const char *base = field(effective, "SOMBASE");
    cJSON_AddStringToObject(environment, "SOMBASE", base && *base ? base : "C:\\OS2");
    cJSON_Delete(effective);
    cJSON_AddItemToArray(programs, service);
    int saved = !c->error && tb_ball_save_manifest(c);
    if (changed)
        *changed = saved;
    return saved;
}
