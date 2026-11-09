/* Public domain. No rights reserved. Wholly AI-authored. */
/* CONFIG.SYS: its Latin-1 text, its SET and command statements, its search
   lists (PATH, LIBPATH, DPATH, ...) and the settings a new ball starts with. */
#ifndef TB_OS2_CONFIG_SYS_H
#define TB_OS2_CONFIG_SYS_H
#include <stddef.h>

/* The environment of a new CONFIG.SYS, as NULL-terminated name and value
   tables. OS2PATH is written as SET PATH; LIBPATH is a command, not a SET. */
extern const char *const os2_default_environment_names[];
extern const char *const os2_default_environment_values[];
/* The CONFIG.SYS text holding the default environment, CR LF terminated. */
char *os2_config_sys_default_text(void);

/* CONFIG.SYS is Latin-1. out holds at least 2 * n + 1 bytes; returns the
   UTF-8 length, excluding the NUL out ends with. */
size_t os2_latin1_to_utf8(const char *bytes, size_t n, char *out);
/* out holds at least strlen(text) + 1 bytes. Returns 0 for a character
   Latin-1 cannot hold. */
int os2_utf8_to_latin1(const char *text, char *out, size_t *length);

/* What a CONFIG.SYS line holds: "SET NAME=value", another "NAME=value"
   statement such as LIBPATH, or neither. */
enum
{
    OS2_CONFIG_SYS_NONE = 0,
    OS2_CONFIG_SYS_SET = 1,
    OS2_CONFIG_SYS_COMMAND = 2
};

/* Splits line in place. White space around the name and the value is
   dropped and the name is upper-cased. */
int os2_config_sys_statement(char *line, char **name, char **value);

/* Search lists are ';'-separated directories. Directories compare without
   case, quotes, surrounding blanks or a trailing separator, with '/' as '\'. */
int os2_search_list_contains(const char *list, const char *directory);
#endif
