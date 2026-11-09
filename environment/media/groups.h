/* Public domain. No rights reserved. Wholly AI-authored. */
/* Stable media group identifiers shared by extraction and dependency reports. */
#ifndef TB_ENVIRONMENT_MEDIA_GROUPS_H
#define TB_ENVIRONMENT_MEDIA_GROUPS_H
#include "ball/store.h"

/* Public domain. No rights reserved. Wholly AI-authored. */
/* Stable media group identifiers shared by extraction and dependency reports. */
enum
{
    EXTRACT_CMD,
    EXTRACT_FONTS,
    EXTRACT_ASSETS,
    EXTRACT_PRINTERS,
    EXTRACT_HELP,
    EXTRACT_FULL,
    EXTRACT_SOM,
    EXTRACT_RUNTIME,
    EXTRACT_MULTIMEDIA,
    EXTRACT_NETWORK,
    EXTRACT_UTILITIES,
    EXTRACT_LANGUAGES,
    EXTRACT_DIAGNOSTICS,
    EXTRACT_LEGACY,
    EXTRACT_OPENGL,
    EXTRACT_COUNT
};

extern const char *extract_categories[EXTRACT_COUNT];
int extract_category(const char *name);
cJSON *extract_group(int category);
cJSON *extract_groups(void);
int extract_module_group(const char *module);
int extract_som_file(const char *name);
#endif
