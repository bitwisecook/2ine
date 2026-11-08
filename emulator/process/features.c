/* Public domain. No rights reserved. Wholly AI-authored. */
#include "emulator/features.h"
#include <ctype.h>
#include <string.h>
#ifndef TWOINE_VIDEO
#define TWOINE_VIDEO 0
#endif
static int features[LX_FEATURE_COUNT] = {TWOINE_VIDEO, 1, 1, 1};
static int opengl = LX_OPENGL_MESA;
static int window_chrome = LX_CHROME_BOTH;
static int tls_upgrade = 1;

static int same(const char *a, const char *b)
{
    while (*a && *b)
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    return *a == *b;
}

int lxRuntimeFeatureEnabled(LxRuntimeFeature feature)
{
    return feature >= 0 && feature < LX_FEATURE_COUNT && features[feature];
}

int lxRuntimeOpenGLProvider(void)
{
    return opengl;
}

int lxRuntimeWindowChrome(void)
{
    return window_chrome;
}

int lxRuntimeTlsUpgrade(void)
{
    return tls_upgrade;
}

int lxRuntimeConfigureFeature(const char *name, const char *value)
{
    if (!strcmp(name, "tls_upgrade"))
    {
        const char *yes[] = {"1", "yes", "y", "true", "t", "on"};
        const char *no[] = {"0", "no", "n", "false", "f", "off"};
        for (unsigned i = 0; i < sizeof yes / sizeof *yes; i++)
        {
            if (same(value, yes[i]))
            {
                tls_upgrade = 1;
                return 1;
            }
            if (same(value, no[i]))
            {
                tls_upgrade = 0;
                return 1;
            }
        }
        return -1;
    }
    if (!strcmp(name, "window_chrome"))
    {
        if (!strcmp(value, "native"))
            window_chrome = LX_CHROME_NATIVE;
        else if (!strcmp(value, "os2"))
            window_chrome = LX_CHROME_OS2;
        else if (!strcmp(value, "both"))
            window_chrome = LX_CHROME_BOTH;
        else
            return -1;
        return 1;
    }
    if (!strcmp(name, "opengl"))
    {
        if (!strcmp(value, "off"))
            opengl = LX_OPENGL_OFF;
        else if (!strcmp(value, "mesa"))
            opengl = LX_OPENGL_MESA;
        else if (!strcmp(value, "original"))
            opengl = LX_OPENGL_ORIGINAL;
        else
            return -1;
        return 1;
    }
    static const char *names[] = {"video", "pcm", "midi", "dive"};
    for (unsigned i = 0; i < LX_FEATURE_COUNT; i++)
        if (!strcmp(name, names[i]))
        {
            const char *yes[] = {"1", "yes", "y", "true", "t", "on"};
            const char *no[] = {"0", "no", "n", "false", "f", "off"};
            for (unsigned j = 0; j < sizeof yes / sizeof *yes; j++)
            {
                if (same(value, yes[j]))
                {
                    features[i] = 1;
                    return 1;
                }
                if (same(value, no[j]))
                {
                    features[i] = 0;
                    return 1;
                }
            }
            return -1;
        }
    return 0;
}
