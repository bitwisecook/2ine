/* Public domain. No rights reserved. Wholly AI-authored. */
/* Environment-selected configuration precedes the XDG per-user audio file. */
#include "os2libs/midi_config.h"
#include "hal/host_fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static char *config_path(void)
{
    const char *selected = getenv("TWOINE_AUDIO_CONFIG");
    if (selected && *selected)
    {
        size_t size = strlen(selected) + 1;
        char *path = malloc(size);
        if (path)
            memcpy(path, selected, size);
        return path;
    }
    const char *directory = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    const char *base = directory && *directory ? directory : home;
    if (!base || !*base)
        return NULL;
    const char *suffix = directory && *directory ? "/2ine/audio.conf" : "/.config/2ine/audio.conf";
    size_t size = strlen(base) + strlen(suffix) + 1;
    char *path = malloc(size);
    if (path)
        snprintf(path, size, "%s%s", base, suffix);
    return path;
}
char *lx_midi_config_value(const char *key)
{
    if (!key || !*key || strchr(key, '=') || strchr(key, '\n'))
        return NULL;
    char *path = config_path();
    if (!path)
        return NULL;
    size_t size;
    LxHostFsError error;
    char *text = lx_host_file_snapshot(path, 16384, &size, &error);
    free(path);
    if (!text)
        return NULL;
    char *result = NULL;
    size_t length = strlen(key);
    for (char *line = text, *end = text + size; line < end;)
    {
        char *next = memchr(line, '\n', (size_t)(end - line));
        if (!next)
            next = end;
        char *last = next;
        if (last > line && last[-1] == '\r')
            last--;
        if ((size_t)(last - line) > length && !memcmp(line, key, length) && line[length] == '=')
        {
            size_t bytes = (size_t)(last - line) - length - 1;
            if (bytes && !memchr(line + length + 1, 0, bytes))
            {
                char *value = malloc(bytes + 1);
                if (value)
                {
                    memcpy(value, line + length + 1, bytes);
                    value[bytes] = 0;
                    free(result);
                    result = value;
                }
            }
        }
        line = next < end ? next + 1 : end;
    }
    free(text);
    return result;
}
