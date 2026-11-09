/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launcher argument text: OS/2 command-line quoting and splitting. */
#include "ball/store.h"
#include "emulator/launch/argv.h"

cJSON *launch_format_arguments(TbBall *c, const cJSON *request)
{
    const cJSON *args = get(request, "arguments");
    if (!tb_ball_array_strings(c, args))
        return NULL;
    char *out = tb_ball_keep(c, strdup(""));
    cJSON *item;
    cJSON_ArrayForEach(item, args)
    {
        const char *value = str(item);
        char *quoted = tb_ball_keep(c, strdup("\""));
        for (const char *p = value; *p && quoted; p++)
            quoted = tb_ball_format(c, "%s%c%s", quoted, *p, *p == '\"' ? "\"" : "");
        out = tb_ball_format(c, "%s%s%s\"", out, *out ? " " : "", quoted);
        if (!out)
            return NULL;
    }
    return cJSON_CreateString(out);
}

cJSON *launch_parse_arguments(TbBall *c, const cJSON *request)
{
    const char *text = field(request, "text");
    if (!text)
    {
        tb_ball_fail(c, "Argument text is required");
        return NULL;
    }
    cJSON *args = cJSON_CreateArray();
    char *word = tb_ball_keep(c, strdup(""));
    int started = 0;
    char quote = 0;
    for (const char *p = text; *p; p++)
    {
        if (quote)
        {
            if (*p == quote)
            {
                if (p[1] == quote)
                {
                    word = tb_ball_format(c, "%s%c", word, *p);
                    p++;
                }
                else
                    quote = 0;
            }
            else
                word = tb_ball_format(c, "%s%c", word, *p);
        }
        else if (*p == '\"' || *p == '\'')
        {
            quote = *p;
            started = 1;
        }
        else if (isspace((unsigned char)*p))
        {
            if (started)
            {
                cJSON_AddItemToArray(args, cJSON_CreateString(word));
                word = tb_ball_keep(c, strdup(""));
                started = 0;
            }
        }
        else
        {
            word = tb_ball_format(c, "%s%c", word, *p);
            started = 1;
        }
        if (!word)
            break;
    }
    if (quote)
        tb_ball_fail(c, "Close the quote in the arguments field.");
    if (c->error)
    {
        cJSON_Delete(args);
        return NULL;
    }
    if (started)
        cJSON_AddItemToArray(args, cJSON_CreateString(word));
    return args;
}
