/* Public domain. No rights reserved. Wholly AI-authored. */
/* Static OS/2 import auditing and media-backed dependency recovery. */
#ifndef TB_BALL_OS2_DEPENDENCIES_H
#define TB_BALL_OS2_DEPENDENCIES_H
#include "ball/store.h"
#define DEPENDENCY_ENTRY_LIMIT 100000u
#define DEPENDENCY_BINARY_LIMIT 30000u
char *dependency_module(TbBall *c, const char *name);
void dependency_error(cJSON *errors, const char *path, const char *message);
void dependency_take_error(TbBall *c, cJSON *errors, const char *path);
int dependency_candidate(const char *name);
void dependency_array_unique(cJSON *array, const char *value);
char *dependency_basename(TbBall *c, const char *path);
void dependency_split_paths(TbBall *c, cJSON *paths, const char *value);
cJSON *dependency_search_paths(TbBall *c, cJSON *errors);
char *dependency_find(TbBall *c, const char *binary, const char *module, cJSON *paths, cJSON *cache,
                      cJSON *errors);
cJSON *validate_os2_media(TbBall *c, const cJSON *request);
cJSON *dependency_compact(const cJSON *report);
cJSON *dependency_audit_latest(TbBall *c);
cJSON *dependency_component_groups(const cJSON *report);
cJSON *audit_dependencies(TbBall *parent, const cJSON *request, int automatic_recovery);
cJSON *ensure_dependencies(TbBall *c, const cJSON *request);
#endif
