/* Public domain. No rights reserved. Wholly AI-authored. */
/* The ball store's C interface for the components layered on it: the request
   context, the settings and files of one open ball, its drive table, guest
   path mapping, configuration files and package operations. */
#ifndef TB_BALL_STORE_H
#define TB_BALL_STORE_H
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ball/platform.h"
#include "ball/vendor/cjson/cJSON.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
/* Drive letters A: to Z: index the drive table; C: is the ball's private drive. */
#define TB_BALL_DRIVE_COUNT 26
#define TB_BALL_DRIVE_C 2
#ifdef _WIN32
#define strdup _strdup
#define PATH_SEPARATOR ";"
#else
#define PATH_SEPARATOR ":"
#endif

typedef struct ComponentOperation ComponentOperation;
/* One request on one open ball: its manifest and resolved drive table, the
   allocations the request owns and its first error. */
typedef struct TbBall TbBall;

/* Work that belongs to the components layered on the ball store: the facade
   installs them for each request, so the store itself never plans launches
   or installs OS/2 components. A missing hook leaves the store's own result. */
typedef struct TbBallHooks
{
    /* Rewrites the files derived from the manifest after drives or settings change. */
    int (*regenerate)(TbBall *c);
    /* Validates the settings groups after a config or unset command. */
    int (*validate_settings)(TbBall *c, const cJSON *request);
    /* The complete client snapshot: store fields plus layered component state. */
    cJSON *(*snapshot)(TbBall *c);
    /* A launcher record the user edited or removed leaves its shell registration. */
    void (*detach_application)(TbBall *c, cJSON *application);
    void (*hide_application)(TbBall *c, const cJSON *application);
    /* Adds the identity of a runtime build to export provenance. */
    void (*describe_runtime)(TbBall *c, const char *build, cJSON *runtime);
} TbBallHooks;

struct TbBall
{
    char **allocations;
    size_t count;
    char *error;
    char *ball;
    char *roots[TB_BALL_DRIVE_COUNT];
    int unavailable[TB_BALL_DRIVE_COUNT];
    cJSON *manifest;
    /* The launcher record whose overrides apply to path and environment lookups. */
    const cJSON *application;
    /* Read-only requests resolve images directly and never publish devices. */
    int readonly_drives;
    /* Request state of the components layered on the store. */
    const char *opengl_policy;
    char *registration_warning;
    ComponentOperation *component_operation;
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
    int tb_ball_regenerate(TbBall * c);
    int tb_ball_validate_settings(TbBall * c, const cJSON *request);
    cJSON *tb_ball_client_snapshot(TbBall * c);
    void tb_ball_detach_application(TbBall * c, cJSON * application);
    void tb_ball_hide_application(TbBall * c, const cJSON *application);
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

int tb_ball_fail(TbBall *c, const char *format, ...);
char *tb_ball_keep(TbBall *c, char *p);
char *tb_ball_resolve_path(TbBall *c, const char *path, int existing);
char *tb_ball_format(TbBall *c, const char *fmt, ...);
char *tb_ball_join(TbBall *c, const char *a, const char *b);
char *tb_ball_read_file(TbBall *c, const char *path, size_t *length, int optional);
int tb_ball_write_json(TbBall *c, const char *path, cJSON *value);
char *tb_ball_latin1_to_utf8(TbBall *c, const char *bytes, size_t n);
char *tb_ball_utf8_to_latin1(TbBall *c, const char *text, size_t *length);
int tb_ball_digest_file(TbBall *c, const char *path, unsigned char hash[32]);
char *tb_ball_hex(TbBall *c, const unsigned char hash[32]);
char *tb_ball_trim(char *s);
char *tb_ball_now(TbBall *c);
int tb_ball_array_strings(TbBall *c, const cJSON *array);

/* Manifest: open, create and save. */
int tb_ball_save_manifest(TbBall *c);
int tb_ball_load(TbBall *c, const cJSON *request);
int tb_ball_create(TbBall *c, const char *path);
/* Balls in the default library, $XDG_DATA_HOME/2ine/ball. */
cJSON *tb_ball_discover(TbBall *c);

/* Settings outside the commands. */
int tb_ball_set_working_directory(TbBall *c, const char *path);
/* A NULL build forgets the runtime reference. */
int tb_ball_set_runtime_build(TbBall *c, const char *build);

/* Drive table: folder, image and device roots. */
int tb_ball_roots(TbBall *c);

/* Guest and host paths over the drive table. */
char *tb_ball_case_path(TbBall *c, const char *path);
int tb_ball_inside(const char *path, const char *root);
int tb_ball_current_drive(TbBall *c);
const char *tb_ball_drive_directory(TbBall *c, int drive);
char *tb_ball_host_path(TbBall *c, const char *name, int explicit_host);
char *tb_ball_guest_path(TbBall *c, const char *host);
char *tb_ball_guest_for_input(TbBall *c, const char *host, const char *input);
int tb_ball_validate_directories(TbBall *c, const cJSON *directories);

/* The guest environment from CONFIG.SYS and the environment settings. */
extern const char *os2_default_environment_names[];
extern const char *os2_default_environment_values[];
cJSON *tb_ball_guest_environment(TbBall *c, const cJSON *overrides);

/* Files: browsing drives and copying host files in. */
cJSON *tb_ball_browse(TbBall *c, const char *path);
int tb_ball_plan_copy(TbBall *c, cJSON *plan, const char *source, const char *destination,
                      unsigned depth);
cJSON *tb_ball_import_files(TbBall *c, const cJSON *request);

/* Launcher records. */
int tb_ball_save_application(TbBall *c, const cJSON *request);

/* Ball commands: drive mappings, settings and launcher records. */
int tb_ball_command(TbBall *c, const cJSON *request);

/* User-editable configuration files: INI profiles and CONFIG.SYS. */
unsigned char *configuration_unhex(TbBall *c, const char *text, size_t *n);
cJSON *configuration_parse(TbBall *c, const unsigned char *p, size_t n);
cJSON *configuration_read(TbBall *c, const cJSON *request);
cJSON *configuration_write(TbBall *c, const cJSON *request);
cJSON *configuration_file_read(TbBall *c, const cJSON *request);
cJSON *configuration_file_write(TbBall *c, const cJSON *request);
cJSON *configuration_files(TbBall *c, const cJSON *request);
/* CONFIG.SYS as UTF-8, and its revision-checked replacement. */
cJSON *tb_ball_config_sys(TbBall *c);
int tb_ball_save_config_sys(TbBall *c, const cJSON *request);

/* Packages: export, import and rename. */
cJSON *tb_ball_export(TbBall *c, const cJSON *request);
cJSON *tb_ball_import(TbBall *c, const cJSON *request);

cJSON *tb_ball_rename(TbBall *c, const cJSON *request);

/* The store fields of the client snapshot. */
cJSON *tb_ball_snapshot(TbBall *c);
#endif
