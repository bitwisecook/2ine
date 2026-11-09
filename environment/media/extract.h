/* Public domain. No rights reserved. Wholly AI-authored. */
/* Category-based OS/2 media planning and staged extraction into a ball. */
#ifndef TB_ENVIRONMENT_MEDIA_EXTRACT_H
#define TB_ENVIRONMENT_MEDIA_EXTRACT_H
#include "ball/store.h"

/* Rank of a media file when several sources supply one destination; the
   highest rank wins a tie. */
enum ExtractPriority
{
    /* The install boot disk carries reduced copies of base-set tools. */
    EXTRACT_PRIORITY_BOOT_DISK = 0,
    EXTRACT_PRIORITY_LOOSE = 1,
    EXTRACT_PRIORITY_NESTED = 2,
    /* Found under a recognised installed-system root, or a support file. */
    EXTRACT_PRIORITY_INSTALLED_LAYOUT = 3,
};

int extract_extension(const char *name, const char *extension);
int extract_video(const char *name);
int extract_expand_installation_roots(TbBall *c, cJSON *sources);
cJSON *open_os2_source(TbBall *c, const cJSON *request);
cJSON *plan_os2_extraction(TbBall *c, const cJSON *request);
cJSON *extract_os2_categories(TbBall *c, const cJSON *request);
#endif
