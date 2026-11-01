/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 WINI sections share the OS/2 user profile. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmshapi.h"

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
