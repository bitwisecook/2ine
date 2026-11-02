/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 WINI sections share the OS/2 user profile. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmshapi.h"
#include "os2libs/pmwinx/case_tables.h"

static char *profile_section(const char *section)
{
    if (!section)
        return NULL;
    size_t length = strlen(section);
    char *app = malloc(length + 6);
    if (app)
    {
        memcpy(app, "WINI_", 5);
        memcpy(app + 5, section, length + 1);
    }
    return app;
}

ULONG GetProfileInt(PSZ section, PSZ key, LONG fallback)
{
    TRACE_NATIVE("GetProfileInt('%s', '%s', %d)", section, key, fallback);
    char *app = profile_section(section);
    if (!app || !key)
    {
        free(app);
        open32SetError(87);
        return (ULONG)fallback;
    }
    ULONG result = (ULONG)PrfQueryProfileInt(HINI_USERPROFILE, app, key, fallback);
    free(app);
    return result;
}

ULONG GetProfileString(PSZ section, PSZ key, PSZ fallback, PSZ output, ULONG capacity)
{
    TRACE_NATIVE("GetProfileString('%s', '%s', '%s', %p, %u)", section, key, fallback, output,
                 capacity);
    char *app = profile_section(section);
    if (!app || !output || !capacity)
    {
        free(app);
        open32SetError(87);
        return 0;
    }
    ULONG result = PrfQueryProfileString(HINI_USERPROFILE, app, key, fallback, output, capacity);
    free(app);
    return result;
}
static char *private_key(const char *key)
{
    char *copy = key ? strdup(key) : NULL;
    if (copy)
        for (unsigned char *p = (unsigned char *)copy; *p; p++)
            *p = upper_case[*p];
    return copy;
}
ULONG GetPrivateProfileString(PSZ section, PSZ key, PSZ fallback, PSZ output, ULONG capacity,
                              PSZ filename)
{
    TRACE_NATIVE("GetPrivateProfileString('%s', '%s', '%s', %p, %u, '%s')", section ? section : "",
                 key ? key : "", fallback ? fallback : "", output, (uint)capacity,
                 filename ? filename : "");
    if (!output || !fallback || !capacity || !filename)
    {
        open32SetError(87);
        return 0;
    }
    output[0] = 0;
    char *normalized = private_key(key);
    if (key && !normalized)
    {
        open32SetError(8);
        return 0;
    }
    HINI profile = PrfOpenProfile(0, filename);
    ULONG result = 0;
    if (profile)
    {
        result = PrfQueryProfileString(profile, section, normalized, fallback, output, capacity);
        PrfCloseProfile(profile);
    }
    else if (section && key)
    {
        size_t count = strlen(fallback);
        if (count >= capacity)
            count = capacity - 1;
        memcpy(output, fallback, count);
        output[count] = 0;
        result = (ULONG)count;
    }
    free(normalized);
    return result;
}
BOOL WritePrivateProfileString(PSZ section, PSZ key, PSZ value, PSZ filename)
{
    if (!filename || (!section && (key || value)))
    {
        open32SetError(87);
        return FALSE;
    }
    char *normalized = private_key(key);
    if (key && !normalized)
    {
        open32SetError(8);
        return FALSE;
    }
    HINI profile = PrfOpenProfile(0, filename);
    BOOL result =
        profile && (!section || PrfWriteProfileString(profile, section, normalized, value));
    if (profile)
        PrfCloseProfile(profile);
    free(normalized);
    return result;
}
