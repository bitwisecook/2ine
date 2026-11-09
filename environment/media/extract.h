/* Public domain. No rights reserved. Wholly AI-authored. */
/* Category-based OS/2 media planning and staged extraction into a ball. */
#ifndef TB_ENVIRONMENT_MEDIA_EXTRACT_H
#define TB_ENVIRONMENT_MEDIA_EXTRACT_H
#include "ball/store.h"

int extract_extension(const char *name, const char *extension);
int extract_video(const char *name);
int extract_expand_installation_roots(TbBall *c, cJSON *sources);
cJSON *open_os2_source(TbBall *c, const cJSON *request);
cJSON *plan_os2_extraction(TbBall *c, const cJSON *request);
cJSON *extract_os2_categories(TbBall *c, const cJSON *request);
#endif
