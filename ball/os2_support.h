/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared installation-media browser and original OS/2 support-file import. */
#ifndef TB_BALL_OS2_SUPPORT_H
#define TB_BALL_OS2_SUPPORT_H
#include "ball/ball_internal.h"

enum
{
    BASE_SUPPORT_COUNT = 3,
    SUPPORT_COUNT = 11
};

extern const char *support_names[];
extern const char *support_paths[];
cJSON *os2_support(Context *c);
void os2_preserve_media_selection(Context *c, cJSON *media);
cJSON *remember_os2_media(Context *c, const cJSON *request);
int media_candidate(const char *name);
cJSON *extraction_read_stats(void);
cJSON *browse_media(Context *c, const char *input);

typedef struct SupportFile
{
    unsigned char *data;
    size_t size;
    char *source;
} SupportFile;

int support_read(Context *c, const char *source, int index, SupportFile *file);
cJSON *import_os2_support(Context *c, const cJSON *request);
#endif
