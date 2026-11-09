/* Public domain. No rights reserved. Wholly AI-authored. */
/* CONFIG.SYS: its Latin-1 text, its SET and command statements, its search
   lists (PATH, LIBPATH, DPATH, ...) and the settings a new ball starts with. */
#include "os2/config/config_sys.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const os2_default_environment_names[] = {
    "LANG", "OS2PATH", "LIBPATH", "DPATH", "HELP", "TEMP", "TMP", "USER_INI", "SYSTEM_INI", NULL};
const char *const os2_default_environment_values[] = {"en_US",
                                                      "C:\\OS2;C:\\OS2\\SYSTEM",
                                                      "C:\\OS2\\DLL",
                                                      "C:\\OS2;C:\\OS2\\SYSTEM",
                                                      "C:\\OS2\\HELP",
                                                      "C:\\TEMP",
                                                      "C:\\TEMP",
                                                      "C:\\OS2\\OS2.INI",
                                                      "C:\\OS2\\OS2SYS.INI",
                                                      NULL};

static const char *statement_prefix(const char *name)
{
    return strcmp(name, "LIBPATH") ? "SET " : "";
}

static const char *statement_name(const char *name)
{
    return strcmp(name, "OS2PATH") ? name : "PATH";
}

char *os2_config_sys_default_text(void)
{
    size_t size = 1;
    for (int i = 0; os2_default_environment_names[i]; i++)
        size += strlen(statement_prefix(os2_default_environment_names[i])) +
                strlen(statement_name(os2_default_environment_names[i])) +
                strlen(os2_default_environment_values[i]) + sizeof "=\r\n" - 1;
    char *text = malloc(size);
    if (!text)
        return NULL;
    size_t at = 0;
    for (int i = 0; os2_default_environment_names[i]; i++)
        at += (size_t)snprintf(
            text + at, size - at, "%s%s=%s\r\n", statement_prefix(os2_default_environment_names[i]),
            statement_name(os2_default_environment_names[i]), os2_default_environment_values[i]);
    text[at] = 0;
    return text;
}

size_t os2_latin1_to_utf8(const char *bytes, size_t n, char *out)
{
    size_t at = 0;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char b = (unsigned char)bytes[i];
        if (b < 0x80)
            out[at++] = (char)b;
        else
        {
            out[at++] = (char)(0xc0 | (b >> 6));
            out[at++] = (char)(0x80 | (b & 0x3f));
        }
    }
    out[at] = 0;
    return at;
}

int os2_utf8_to_latin1(const char *text, char *out, size_t *length)
{
    size_t at = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    {
        if (*p < 0x80)
            out[at++] = (char)*p;
        /* U+0080..U+00FF are the two-byte sequences C2 80..C3 BF. */
        else if ((*p == 0xc2 || *p == 0xc3) && p[1] >= 0x80 && p[1] <= 0xbf)
        {
            out[at++] = (char)(((*p & 3) << 6) | (p[1] & 0x3f));
            p++;
        }
        else
            return 0;
    }
    out[at] = 0;
    *length = at;
    return 1;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    return s;
}

int os2_config_sys_statement(char *line, char **name, char **value)
{
    char *entry = trim(line);
    int kind = OS2_CONFIG_SYS_COMMAND;
    if (strlen(entry) >= 4 && toupper((unsigned char)entry[0]) == 'S' &&
        toupper((unsigned char)entry[1]) == 'E' && toupper((unsigned char)entry[2]) == 'T' &&
        isspace((unsigned char)entry[3]))
    {
        entry += 4;
        kind = OS2_CONFIG_SYS_SET;
    }
    char *equals = strchr(entry, '=');
    if (!equals)
        return OS2_CONFIG_SYS_NONE;
    *equals = 0;
    *name = trim(entry);
    for (char *p = *name; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    *value = trim(equals + 1);
    return kind;
}

static int search_list_entry_equal(const char *entry, size_t length, const char *directory)
{
    while (length && isspace((unsigned char)*entry))
        entry++, length--;
    while (length && isspace((unsigned char)entry[length - 1]))
        length--;
    if (length > 1 && *entry == '"' && entry[length - 1] == '"')
        entry++, length -= 2;
    /* Keep the separator of a drive root such as "C:\". */
    while (length > 3 && (entry[length - 1] == '\\' || entry[length - 1] == '/'))
        length--;
    if (strlen(directory) != length)
        return 0;
    for (size_t i = 0; i < length; i++)
    {
        char a = entry[i] == '/' ? '\\' : (char)toupper((unsigned char)entry[i]);
        char b = directory[i] == '/' ? '\\' : (char)toupper((unsigned char)directory[i]);
        if (a != b)
            return 0;
    }
    return 1;
}

int os2_search_list_contains(const char *list, const char *directory)
{
    for (const char *part = list ? list : ""; *part;)
    {
        const char *end = strchr(part, ';');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (search_list_entry_equal(part, n, directory))
            return 1;
        part += n;
        if (*part)
            part++;
    }
    return 0;
}
