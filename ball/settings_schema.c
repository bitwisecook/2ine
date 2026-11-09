/* Public domain. No rights reserved. Wholly AI-authored. */
/* Validation of the runtime's system.* settings. */
#include "ball/store.h"
#include "ball/settings_schema.h"
#include <math.h>

const char *launch_setting_text(TbBall *c, const cJSON *v)
{
    if (cJSON_IsString(v))
        return str(v);
    if (cJSON_IsBool(v))
        return cJSON_IsTrue(v) ? "true" : "false";
    if (cJSON_IsNumber(v))
        return tb_ball_format(c, "%.17g", v->valuedouble);
    tb_ball_fail(c, "Invalid setting value");
    return NULL;
}

int launch_validate_system(TbBall *c)
{
    cJSON *settings = get(c->manifest, "system"), *entry;
    if (settings && !cJSON_IsObject(settings))
        return tb_ball_fail(c, "Invalid system settings");
    cJSON_ArrayForEach(entry, settings)
    {
        const char *v = launch_setting_text(c, entry);
        if (!v)
            return 0;
        if (strpbrk(v, "\r\n"))
            return tb_ball_fail(c, "A setting must fit on one line");
        if (!strcmp(entry->string, "beep_volume"))
        {
            char *end = NULL;
            double number = strtod(v, &end);
            if (end == v || *end || !isfinite(number) || number < 0 || number > 1)
                return tb_ball_fail(c, "beep_volume must be between 0 and 1");
        }
        else if (!strcmp(entry->string, "display_dpi"))
        {
            char *end = NULL;
            long number = strtol(v, &end, 10);
            if (end == v || *end || number < 72 || number > 384)
                return tb_ball_fail(c, "display_dpi must be a whole number between 72 and 384");
        }
        else if (!strcmp(entry->string, "display_depth"))
        {
            if (strcmp(v, "4") && strcmp(v, "8") && strcmp(v, "16") && strcmp(v, "24"))
                return tb_ball_fail(c, "display_depth must be 4, 8, 16 or 24");
        }
        else if (!strcmp(entry->string, "opengl"))
        {
            if (strcmp(v, "mesa") && strcmp(v, "original") && strcmp(v, "off"))
                return tb_ball_fail(c, "opengl must be mesa, original or off");
        }
        else if (!strcmp(entry->string, "window_chrome"))
        {
            if (strcmp(v, "native") && strcmp(v, "os2") && strcmp(v, "both"))
                return tb_ball_fail(c, "window_chrome must be native, os2 or both");
        }
        else if (!strcmp(entry->string, "trace_native") || !strcmp(entry->string, "trace_events") ||
                 !strcmp(entry->string, "video") || !strcmp(entry->string, "pcm") ||
                 !strcmp(entry->string, "midi") || !strcmp(entry->string, "dive") ||
                 !strcmp(entry->string, "tls_upgrade"))
        {
            const char *valid[] = {"1",  "yes", "y",     "true", "t",   "on", "0",
                                   "no", "n",   "false", "f",    "off", NULL};
            int found = 0;
            for (int i = 0; valid[i]; i++)
                if (equal_case(v, valid[i]))
                    found = 1;
            if (!found)
                return tb_ball_fail(c, "Invalid boolean setting: %s", entry->string);
        }
        else
            return tb_ball_fail(c, "Unknown system setting: %s", entry->string);
    }
    return 1;
}
