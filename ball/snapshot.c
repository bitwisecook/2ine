/* Public domain. No rights reserved. Wholly AI-authored. */
/* The ball snapshot returned to clients. */
#include "ball/ball_internal.h"
#include "ball/drives.h"
#include "ball/guest_environment.h"
#include "ball/os2_dependencies.h"
#include "ball/os2_support.h"
#include "ball/paths.h"
#include "ball/snapshot.h"
#include "ball/wps_applications.h"

cJSON *tb_ball_snapshot(Context *c)
{
    if (!tb_ball_roots(c) || !wps_sync(c))
        return NULL;
    char *path = tb_ball_join(c, c->roots[2], "CONFIG.SYS");
    size_t size = 0;
    char *bytes = tb_ball_read_file(c, path, &size, 1);
    if (!bytes)
        return NULL;
    cJSON *value = cJSON_CreateObject(), *mapping = cJSON_CreateObject();
    if (c->registration_warning)
        cJSON_AddStringToObject(value, "applicationRegistrationWarning", c->registration_warning);
    cJSON_AddItemToObject(value, "manifest", cJSON_Duplicate(c->manifest, 1));
    cJSON *registration = wps_latest(c);
    if (registration)
        cJSON_AddItemToObject(value, "applicationRegistration", registration);
    cJSON_AddItemToObject(value, "os2Support", os2_support(c));
    cJSON *dependency_audit = dependency_audit_latest(c);
    if (dependency_audit)
        cJSON_AddItemToObject(value, "dependencyAudit", dependency_audit);
    cJSON *records = cJSON_AddObjectToObject(value, "applicationRecords"), *application;
    cJSON_ArrayForEach(application, get(c->manifest, "applications"))
    {
        char *encoded = cJSON_PrintUnformatted(application);
        if (encoded)
        {
            cJSON_AddStringToObject(records, application->string, encoded);
            cJSON_free(encoded);
        }
    }
    cJSON *drive_records = cJSON_AddObjectToObject(value, "driveRecords"), *drive_entry;
    cJSON_ArrayForEach(drive_entry, get(c->manifest, "drives"))
    {
        char *encoded = cJSON_PrintUnformatted(drive_entry);
        if (encoded)
        {
            cJSON_AddStringToObject(drive_records, drive_entry->string, encoded);
            cJSON_free(encoded);
        }
    }
    cJSON *image_sources = cJSON_AddObjectToObject(value, "imageSources");
    for (int i = 0; i < 26; i++)
        if (c->roots[i] && tb_image_path(c->roots[i]))
        {
            char *source = tb_image_source(c->roots[i]);
            if (source)
            {
                char letter[2] = {(char)('A' + i), 0};
                cJSON_AddStringToObject(image_sources, letter, source);
                free(source);
            }
        }
    cJSON_AddItemToObject(value, "roots", mapping);
    cJSON *unavailable = cJSON_AddObjectToObject(value, "unavailableDrives");
    cJSON_ArrayForEach(drive_entry,
                       get(c->manifest, "drives")) if (c->unavailable[drive_entry->string[0] - 'A'])
        cJSON_AddStringToObject(unavailable, drive_entry->string,
                                str(drive_entry) ? str(drive_entry) : field(drive_entry, "image"));
    for (int i = 0; i < 26; i++)
        if (c->roots[i])
        {
            char letter[2] = {(char)('A' + i), 0};
            cJSON_AddStringToObject(mapping, letter, c->roots[i]);
        }
    cJSON *directories = cJSON_AddObjectToObject(value, "workingDirectories");
    for (int i = 0; i < 26; i++)
        if (c->roots[i] || c->unavailable[i])
        {
            char key[2] = {(char)('A' + i), 0};
            cJSON_AddStringToObject(directories, key, tb_ball_drive_directory(c, i));
        }
    cJSON_AddStringToObject(value, "currentDrive",
                            tb_ball_format(c, "%c", 'A' + tb_ball_current_drive(c)));
    cJSON *env = tb_ball_guest_environment(c, NULL);
    if (!env)
    {
        cJSON_Delete(value);
        return NULL;
    }
    if (field(env, "OS2PATH"))
    {
        put(env, "PATH", cJSON_CreateString(field(env, "OS2PATH")));
        cJSON_DeleteItemFromObjectCaseSensitive(env, "OS2PATH");
    }
    cJSON_AddItemToObject(value, "environmentDefaults", env);
    cJSON_AddStringToObject(value, "configSys", tb_ball_latin1_to_utf8(c, bytes, size));
    return value;
}
