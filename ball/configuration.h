/* Public domain. No rights reserved. Wholly AI-authored. */
/* Binary-safe profile inspection and revision-checked value replacement. */
#ifndef TB_BALL_CONFIGURATION_H
#define TB_BALL_CONFIGURATION_H
#include "ball/ball_internal.h"

unsigned char *configuration_unhex(Context *c, const char *text, size_t *n);
cJSON *configuration_parse(Context *c, const unsigned char *p, size_t n);
cJSON *configuration_read(Context *c, const cJSON *request);
cJSON *configuration_write(Context *c, const cJSON *request);
cJSON *configuration_file_read(Context *c, const cJSON *request);
cJSON *configuration_file_write(Context *c, const cJSON *request);
cJSON *configuration_files(Context *c, const cJSON *request);
#endif
