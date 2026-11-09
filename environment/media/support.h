/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared installation-media browser and original OS/2 support-file import. */
#ifndef TB_ENVIRONMENT_MEDIA_SUPPORT_H
#define TB_ENVIRONMENT_MEDIA_SUPPORT_H
#include "ball/store.h"

enum
{
    BASE_SUPPORT_COUNT = 3,
    SUPPORT_COUNT = 11
};

extern const char *support_names[];
extern const char *support_paths[];
cJSON *os2_support(TbBall *c);
void os2_preserve_media_selection(TbBall *c, cJSON *media);
cJSON *remember_os2_media(TbBall *c, const cJSON *request);
int media_candidate(const char *name);
cJSON *extraction_read_stats(void);
cJSON *browse_media(TbBall *c, const char *input);

typedef struct SupportFile
{
    unsigned char *data;
    size_t size;
    char *source;
} SupportFile;

int support_read(TbBall *c, const char *source, int index, SupportFile *file);
cJSON *import_os2_support(TbBall *c, const cJSON *request);
#endif
