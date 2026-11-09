/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private interface shared by the ball engine's compilation units: the request
   context and the JSON helpers every unit uses. */
#ifndef TB_BALL_INTERNAL_H
#define TB_BALL_INTERNAL_H

#define _POSIX_C_SOURCE 200809L
#include "ball/include/twoine_ball.h"
#include "ball/platform.h"
#include "storage/volume.h"
#include "formats/encoding/base64.h"
#include "formats/endian.h"
#include "formats/executable/lx.h"
#include "formats/executable/ne.h"
#include "formats/executable/os2_executable.h"
#include "formats/profile/os2_ini.h"
#include "formats/resources/icon.h"
#include "ball/vendor/cjson/cJSON.h"
#include "ball/vendor/sha256/sha256.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define strdup _strdup
#define PATH_SEPARATOR ";"
#else
#define PATH_SEPARATOR ":"
#endif

typedef struct ComponentOperation ComponentOperation;
typedef struct Context Context;

/* Work that belongs to the components layered on the ball store: the facade
   installs them for each request, so the store itself never plans launches
   or installs OS/2 components. A missing hook leaves the store's own result. */
typedef struct TbBallHooks
{
    /* Rewrites the files derived from the manifest after drives or settings change. */
    int (*regenerate)(Context *c);
    /* Validates the settings groups after a config or unset command. */
    int (*validate_settings)(Context *c, const cJSON *request);
    /* The complete client snapshot: store fields plus layered component state. */
    cJSON *(*snapshot)(Context *c);
    /* A launcher record the user edited or removed leaves its shell registration. */
    void (*detach_application)(Context *c, cJSON *application);
    void (*hide_application)(Context *c, const cJSON *application);
    /* Adds the identity of a runtime build to export provenance. */
    void (*describe_runtime)(Context *c, const char *build, cJSON *runtime);
} TbBallHooks;

struct Context
{
    char **allocations;
    size_t count;
    char *error;
    char *ball;
    char *roots[26];
    int unavailable[26];
    cJSON *manifest;
    const cJSON *application;
    const char *opengl_policy;
    char *registration_warning;
    ComponentOperation *component_operation;
    int readonly_drives;
    const TbBallHooks *hooks;
};

static inline const char *str(const cJSON *j)
{
    return cJSON_IsString(j) ? j->valuestring : NULL;
}

static inline cJSON *get(const cJSON *j, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(j, key);
}

static inline const char *field(const cJSON *j, const char *key)
{
    return str(get(j, key));
}

/* Recognize host paths from either platform without treating DOS paths as relative. */
static inline int host_absolute(const char *path)
{
    return path && (path[0] == '/' || path[0] == '\\' ||
                    (isalpha((unsigned char)path[0]) && path[1] == ':'));
}

static inline int native_host_path(const char *path)
{
#ifdef _WIN32
    return !(path[0] == '/' && path[1] != '/');
#else
    return path[0] != '\\' && !(path[0] == '/' && path[1] == '/') &&
           !(isalpha((unsigned char)path[0]) && path[1] == ':');
    int tb_ball_regenerate(Context * c);
    int tb_ball_validate_settings(Context * c, const cJSON *request);
    cJSON *tb_ball_client_snapshot(Context * c);
    void tb_ball_detach_application(Context * c, cJSON * application);
    void tb_ball_hide_application(Context * c, const cJSON *application);
#endif
}

static inline void put(cJSON *j, const char *key, cJSON *value)
{
    if (get(j, key))
        cJSON_ReplaceItemInObjectCaseSensitive(j, key, value);
    else
        cJSON_AddItemToObject(j, key, value);
}

static inline cJSON *object(cJSON *j, const char *key)
{
    cJSON *v = get(j, key);
    if (!v)
    {
        v = cJSON_CreateObject();
        put(j, key, v);
    }
    return cJSON_IsObject(v) ? v : NULL;
}

static inline int compare_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static inline int equal_case(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    }
    return *a == *b;
}

int tb_ball_fail(Context *c, const char *format, ...);
char *tb_ball_keep(Context *c, char *p);
char *tb_ball_resolve_path(Context *c, const char *path, int existing);
char *tb_ball_format(Context *c, const char *fmt, ...);
char *tb_ball_join(Context *c, const char *a, const char *b);
char *tb_ball_read_file(Context *c, const char *path, size_t *length, int optional);
int tb_ball_write_json(Context *c, const char *path, cJSON *value);
char *tb_ball_latin1_to_utf8(Context *c, const char *bytes, size_t n);
char *tb_ball_utf8_to_latin1(Context *c, const char *text, size_t *length);
int tb_ball_digest_file(Context *c, const char *path, unsigned char hash[32]);
char *tb_ball_hex(Context *c, const unsigned char hash[32]);
char *tb_ball_trim(char *s);
char *tb_ball_now(Context *c);
int tb_ball_array_strings(Context *c, const cJSON *array);
int tb_ball_regenerate(Context *c);
int tb_ball_validate_settings(Context *c, const cJSON *request);
cJSON *tb_ball_client_snapshot(Context *c);
void tb_ball_detach_application(Context *c, cJSON *application);
void tb_ball_hide_application(Context *c, const cJSON *application);
int tb_ball_regenerate(Context *c);
int tb_ball_validate_settings(Context *c, const cJSON *request);
cJSON *tb_ball_client_snapshot(Context *c);
void tb_ball_detach_application(Context *c, cJSON *application);
void tb_ball_hide_application(Context *c, const cJSON *application);
#endif
