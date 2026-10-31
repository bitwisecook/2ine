/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "runtime/core/internal.h"
void cfgWarn(const char *fname, const int lineno, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "2ine cfg warning: [%s:%d] ", fname, lineno);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

int cfgIsSpace(const int ch)
{
    switch (ch)
    {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            return 1;
    }
    return 0;
}

int cfgIsSymChar(const int ch)
{
    return ((ch == '_') || ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')));
}

void cfgProcessBoolString(const char *fname, const int lineno, int *output, const char *val)
{
    static const char *y[] = {"1", "yes", "y", "true", "t", "on"};
    static const char *n[] = {"0", "no", "n", "false", "f", "off"};
    int i;
    for (i = 0; i < (sizeof(y) / sizeof(y[0])); i++)
    {
        if (strcasecmp(val, y[i]) == 0)
        {
            *output = 1;
            return;
        }
        if (strcasecmp(val, n[i]) == 0)
        {
            *output = 0;
            return;
        }
    }
    cfgWarn(fname, lineno, "\"%s\" is not valid for this setting. Try 1 or 0.", val);
}

void cfgProcessFloatString(const char *fname, const int lineno, float *output, const char *val)
{
    errno = 0;
    const float converted = strtof(val, NULL);
    if (!errno)
    {
        *output = converted;
    }
    else
    {
        cfgWarn(fname, lineno, "\"%s\" is not valid for this setting. Try a number.", val);
    }
}

void cfgProcessMountPoint(const char *fname, const int lineno, const char *var, const char *val)
{
    char letter = var[0];
    if ((letter >= 'a') && (letter <= 'z'))
    {
        letter -= 'a' - 'A';
    }

    if ((letter < 'A') || (letter > 'Z') || (var[1] != '\0'))
    {
        cfgWarn(fname, lineno, "Invalid disk \"%s\" (must be between 'A' and 'Z')", var);
        return;
    }

    const int idx = (int)(letter - 'A');
    free(GLoaderState.disks[idx]);
    GLoaderState.disks[idx] = NULL;

    if (*val != '\0')
    { // val=="" means don't mount
        const size_t vallen = strlen(val);
        LxHostFileInfo statbuf;
        if (lx_host_path_info(val, &statbuf) != LX_HOST_FS_OK)
        {
            cfgWarn(fname, lineno, "Path \"%s\" for drive %c:\\ isn't accessible: %s", val, letter,
                    strerror(errno));
        }
        else if (statbuf.kind != LX_HOST_FILE_DIRECTORY)
        {
            cfgWarn(fname, lineno, "Path \"%s\" for drive %c:\\ isn't a directory", val, letter);
        }
        else if ((GLoaderState.disks[idx] = (char *)malloc(vallen + 2)) == NULL)
        {
            cfgWarn(fname, lineno, "Out of memory!");
        }
        else
        {
            char *ptr = GLoaderState.disks[idx];
            strcpy(ptr, val);
            if (ptr[vallen - 1] != '/')
            {
                ptr[vallen] = '/';
                ptr[vallen + 1] = '\0';
            }
        }
    }
}

void cfgProcessSystem(const char *fname, const int lineno, const char *var, const char *val)
{
    if (strcmp(var, "trace_native") == 0)
    {
        cfgProcessBoolString(fname, lineno, &GLoaderState.trace_native, val);
    }
    else if (strcmp(var, "trace_events") == 0)
    {
        cfgProcessBoolString(fname, lineno, &GLoaderState.trace_events, val);
    }
    else if (strcmp(var, "beep_volume") == 0)
    {
        cfgProcessFloatString(fname, lineno, &GLoaderState.beep_volume, val);
    }
    else
    {
        cfgWarn(fname, lineno, "Unknown variable system.%s", var);
    }
}

void cfgProcessLine(const char *fname, const int lineno, const char *category, const char *var,
                    const char *val)
{

    if ((*category == '\0') || (*var == '\0'))
    {
        cfgWarn(fname, lineno, "Invalid configuration line");
    }
    else if (strcmp(category, "mountpoint") == 0)
    {
        cfgProcessMountPoint(fname, lineno, var, val);
    }
    else if (strcmp(category, "system") == 0)
    {
        cfgProcessSystem(fname, lineno, var, val);
    }
    else
    {
        cfgWarn(fname, lineno, "Unknown category \"%s\"", category);
    }
}

void cfgLoad(const char *fname)
{
    FILE *io = fopen(fname, "r");
    char *buffer = NULL;
    size_t buflen = 0;
    unsigned int lineno = 0;
    ssize_t len;

    if (io == NULL)
    {
        return;
    }

    while ((len = getline(&buffer, &buflen, io)) >= 0)
    {
        lineno++;

        while ((len > 0) && (cfgIsSpace(buffer[len - 1])))
        {
            buffer[--len] = '\0';
        }

#define SKIPSPACE()                                                                                \
    while (cfgIsSpace(*ptr))                                                                       \
    {                                                                                              \
        ptr++;                                                                                     \
    }

        char *ptr = buffer;
        SKIPSPACE();
        if ((*ptr == '\0') || (*ptr == ';') || (*ptr == '#') ||
            ((ptr[0] == '/') && (ptr[1] == '/')))
        {

            continue;
        }
        const char *category = ptr;
        while (cfgIsSymChar(*ptr))
        {
            ptr++;
        }
        if (*ptr != '.')
        {
            cfgWarn(fname, lineno, "Invalid configuration line");
            continue;
        }
        *(ptr++) = '\0';
        const char *var = ptr;
        while (cfgIsSymChar(*ptr))
        {
            ptr++;
        }

        if (*ptr == '=')
        {
            *(ptr++) = '\0';
        }
        else if (!cfgIsSpace(*ptr))
        {
            cfgWarn(fname, lineno, "Invalid configuration line");
            continue;
        }
        else
        {
            *(ptr++) = '\0';
            SKIPSPACE();
            if (*ptr != '=')
            {
                cfgWarn(fname, lineno, "Invalid configuration line");
                continue;
            }
            ptr++;
        }
        SKIPSPACE();
        const char *val = ptr;
#undef SKIPSPACE

        cfgProcessLine(fname, lineno, category, var, val);
    }

    free(buffer);
    fclose(io);
}

void cfgLoadFiles(void)
{
    cfgLoad("/etc/2ine.cfg");

    char *fname = NULL;
    const char *env = getenv("XDG_CONFIG_HOME");
    if (env)
    {
        const size_t buflen = strlen(env) + 32;
        fname = (char *)malloc(buflen);
        if (fname)
        {
            snprintf(fname, buflen, "%s/2ine/2ine.cfg", env);
        }
    }
    else
    {
        env = getenv("HOME");
        if (env)
        {
            const size_t buflen = strlen(env) + 32;
            fname = (char *)malloc(buflen);
            if (fname)
            {
                snprintf(fname, buflen, "%s/.config/2ine/2ine.cfg", env);
            }
        }
    }

    if (fname)
    {
        cfgLoad(fname);
        free(fname);
    }
}
