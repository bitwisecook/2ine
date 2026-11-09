/* Public domain. No rights reserved. Wholly AI-authored. */
/* Workplace Shell object records as PMWP keeps them in its object profile,
   and the setup strings (KEY=value;...) that configure each object. */
#include "os2/config/wps_objects.h"
#include <ctype.h>
#include <string.h>

/* Longest key compared; longer keys are cut and match nothing real. */
#define SETUP_KEY_LIMIT 256

int os2_wps_object_fields(const char *data, size_t size, const char *fields[OS2_WPS_OBJECT_FIELDS])
{
    size_t at = 0;
    for (int i = 0; i < OS2_WPS_OBJECT_FIELDS; i++)
    {
        const char *nul = at < size ? memchr(data + at, 0, size - at) : NULL;
        if (!nul)
            return OS2_WPS_RECORD_TRUNCATED;
        fields[i] = data + at;
        at = (size_t)(nul - data) + 1;
    }
    return at == size ? OS2_WPS_RECORD_VALID : OS2_WPS_RECORD_TRAILING;
}

static int key_equal(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

int os2_wps_setup_value(const char *setup, const char *name, char *value, size_t capacity,
                        size_t *length)
{
    int found = 0;
    while (setup && *setup)
    {
        char key[SETUP_KEY_LIMIT];
        size_t at = 0;
        while (*setup && *setup != '=' && *setup != ';')
        {
            char c = *setup++;
            if (c == '^' && *setup)
                c = *setup++;
            if (at + 1 < sizeof key)
                key[at++] = c;
        }
        key[at] = 0;
        while (at && (key[at - 1] == ' ' || key[at - 1] == '\t'))
            key[--at] = 0;
        const char *start = key;
        while (*start == ' ' || *start == '\t')
            start++;
        int match = *setup == '=' && key_equal(start, name);
        if (*setup == '=')
            setup++;
        at = 0;
        while (*setup && *setup != ';')
        {
            char c = *setup++;
            if (c == '^' && *setup)
                c = *setup++;
            if (match && at + 1 < capacity)
                value[at++] = c;
        }
        if (match)
        {
            value[at] = 0;
            if (length)
                *length = at;
            found = 1;
        }
        if (*setup)
            setup++;
    }
    return found;
}
