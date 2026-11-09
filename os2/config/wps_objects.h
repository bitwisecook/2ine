/* Public domain. No rights reserved. Wholly AI-authored. */
/* Workplace Shell object records as PMWP keeps them in its object profile,
   and the setup strings (KEY=value;...) that configure each object. */
#ifndef TB_OS2_WPS_OBJECTS_H
#define TB_OS2_WPS_OBJECTS_H
#include <stddef.h>

/* A record is the class name, title, location and setup string, each
   NUL-terminated, filling the value exactly. */
enum
{
    OS2_WPS_OBJECT_CLASS,
    OS2_WPS_OBJECT_TITLE,
    OS2_WPS_OBJECT_LOCATION,
    OS2_WPS_OBJECT_SETUP,
    OS2_WPS_OBJECT_FIELDS
};

enum
{
    OS2_WPS_RECORD_VALID = 0,
    /* A field has no terminating NUL. */
    OS2_WPS_RECORD_TRUNCATED,
    /* Bytes follow the setup string. */
    OS2_WPS_RECORD_TRAILING
};

/* Points fields into data. */
int os2_wps_object_fields(const char *data, size_t size, const char *fields[OS2_WPS_OBJECT_FIELDS]);

/* Copies the value of key name from a setup string into value (at most
   capacity - 1 bytes, NUL-terminated) and its length into *length when
   length is not NULL. A caret makes the next character literal; keys
   compare without case and surrounding blanks or tabs; the last occurrence
   of a key wins. Returns 0 when the key is absent. */
int os2_wps_setup_value(const char *setup, const char *name, char *value, size_t capacity,
                        size_t *length);
#endif
