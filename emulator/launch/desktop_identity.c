/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launcher icons and the desktop identity of a launch. */
#include "ball/store.h"
#include "emulator/launch/desktop_identity.h"
#include "formats/encoding/base64.h"
#include "formats/executable/os2_executable.h"
#include "formats/resources/icon.h"

cJSON *launch_application_icon(TbBall *c, const cJSON *request)
{
    const char *name = field(request, "name"), *program = field(request, "program");
    cJSON *app = name ? get(get(c->manifest, "applications"), name) : NULL;
    if (!program)
        program = field(app, "program");
    const char *custom = field(app, "icon");
    cJSON *result = cJSON_CreateObject();
    if (get(app, "wpsRegistration"))
        cJSON_AddItemToObject(result, "wpsRegistration",
                              cJSON_Duplicate(get(app, "wpsRegistration"), 1));
    if (program)
    {
        const char *base = program;
        for (const char *p = program; *p; p++)
            if (*p == '/' || *p == '\\')
                base = p + 1;
        char *fallback = tb_ball_keep(c, strdup(base));
        char *dot = fallback ? strrchr(fallback, '.') : NULL;
        if (dot && dot != fallback)
            *dot = 0;
        cJSON_AddStringToObject(result, "displayName", fallback ? fallback : base);
    }
    if (name && get(app, "wpsRegistration"))
    {
        cJSON_DeleteItemFromObjectCaseSensitive(result, "displayName");
        cJSON_AddStringToObject(result, "displayName", name);
    }
    if (custom)
    {
        char *path = tb_ball_case_path(c, tb_ball_join(c, c->ball, custom));
        if (!path || !tb_ball_inside(path, c->ball))
        {
            cJSON_Delete(result);
            tb_ball_fail(c, "Icon path leaves the ball");
            return NULL;
        }
        size_t size = 0;
        char *bytes = tb_ball_read_file(c, path, &size, 0);
        if (bytes)
        {
            TbIconImage *icon = tb_icon_bitmap((const unsigned char *)bytes, size);
            if (icon)
            {
                char *rgba = tb_icon_rgba_base64(icon);
                cJSON_AddStringToObject(result, "rgba", rgba);
                cJSON_AddNumberToObject(result, "width", icon->width);
                cJSON_AddNumberToObject(result, "height", icon->height);
                free(rgba);
                tb_icon_free(icon);
            }
            else
            {
                char *encoded = tb_base64((const unsigned char *)bytes, size);
                cJSON_AddStringToObject(result, "image", encoded);
                free(encoded);
            }
            cJSON_AddStringToObject(result, "source", "custom");
            return result;
        }
    }
    if (get(app, "wpsIcon"))
    {
        const cJSON *setting = get(app, "wpsIcon"), *resource = get(setting, "resource");
        const char *guest = field(setting, "path");
        char *path = guest ? tb_ball_host_path(c, guest, 0) : NULL;
        uint64_t length = 0;
        int64_t modified = 0;
        size_t size = 0;
        char *bytes =
            path && tb_path_info(path, &length, &modified) && length <= 64u * 1024u * 1024u
                ? tb_ball_read_file(c, path, &size, 1)
                : NULL;
        TbIconImage *icon =
            bytes ? (resource ? tb_icon_executable_resource((uint8_t *)bytes, size,
                                                            (unsigned)resource->valuedouble)
                              : tb_icon_bitmap((uint8_t *)bytes, size))
                  : NULL;
        if (icon)
        {
            char *rgba = tb_icon_rgba_base64(icon);
            cJSON_AddStringToObject(result, "rgba", rgba);
            cJSON_AddNumberToObject(result, "width", icon->width);
            cJSON_AddNumberToObject(result, "height", icon->height);
            cJSON_AddStringToObject(result, "source",
                                    resource ? "wps-iconresource" : "wps-iconfile");
            cJSON_AddStringToObject(result, "iconPath", guest);
            if (field(setting, "declaredPath"))
            {
                cJSON_AddStringToObject(result, "declaredIconPath", field(setting, "declaredPath"));
                cJSON_AddStringToObject(result, "fallbackReason", field(setting, "fallbackReason"));
            }
            free(rgba);
            tb_icon_free(icon);
            if (name)
            {
                cJSON_DeleteItemFromObjectCaseSensitive(result, "displayName");
                cJSON_AddStringToObject(result, "displayName", name);
            }
            return result;
        }
        free(c->error);
        c->error = NULL;
    }
    if (program)
    {
        char *host = tb_ball_host_path(c, program, 0);
        if (host && tb_is_file(host))
        {
            size_t size = 0;
            char *bytes = tb_ball_read_file(c, host, &size, 0);
            char *description =
                bytes ? tb_executable_description((const uint8_t *)bytes, size) : NULL;
            if (description && !get(app, "wpsRegistration"))
            {
                cJSON_DeleteItemFromObjectCaseSensitive(result, "displayName");
                cJSON_AddStringToObject(result, "displayName", description);
                free(description);
            }
            else
                free(description);
            TbIconImage *icon =
                bytes ? tb_icon_executable((const unsigned char *)bytes, size) : NULL;
            if (icon)
            {
                char *rgba = tb_icon_rgba_base64(icon);
                cJSON_AddStringToObject(result, "rgba", rgba);
                cJSON_AddNumberToObject(result, "width", icon->width);
                cJSON_AddNumberToObject(result, "height", icon->height);
                cJSON_AddStringToObject(result, "source", "embedded");
                free(rgba);
                tb_icon_free(icon);
                return result;
            }
        }
    }
    cJSON_AddStringToObject(result, "source", "none");
    return result;
}

/* Presentation metadata is optional and cannot prevent a valid guest launch. */
cJSON *launch_desktop_identity(TbBall *parent, const char *program, const char *name)
{
    TbBall storage = *parent;
    storage.allocations = NULL;
    storage.count = 0;
    storage.error = NULL;
    TbBall *c = &storage;
    cJSON *request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "program", program);
    if (name)
        cJSON_AddStringToObject(request, "name", name);
    cJSON *icon = launch_application_icon(c, request), *result = cJSON_CreateObject();
    const char *base = program;
    for (const char *at = program; *at; at++)
        if (*at == '/' || *at == '\\')
            base = at + 1;
    char *fallback = tb_ball_keep(c, strdup(base));
    char *suffix = fallback ? strrchr(fallback, '.') : NULL;
    if (suffix && suffix != fallback)
        *suffix = 0;
    const char *display = name ? name : field(icon, "displayName");
    if (!display || !*display)
        display = fallback && *fallback ? fallback : "OS2 Application";
    cJSON_AddStringToObject(result, "name", display);
    cJSON_AddStringToObject(result, "nameSource",
                            name                                    ? "launcher"
                            : fallback && strcmp(display, fallback) ? "module-description"
                                                                    : "filename");
    if (icon && (field(icon, "rgba") || field(icon, "image")))
        cJSON_AddItemToObject(result, "icon", cJSON_Duplicate(icon, 1));
    cJSON_Delete(icon);
    cJSON_Delete(request);
    for (size_t i = 0; i < c->count; i++)
        free(c->allocations[i]);
    free(c->allocations);
    free(c->error);
    return result;
}
