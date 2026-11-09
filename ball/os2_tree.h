/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hierarchical media choices and exact planned-source selection. */
#ifndef TB_BALL_OS2_TREE_H
#define TB_BALL_OS2_TREE_H
#include "ball/ball_internal.h"

/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hierarchical media choices and exact planned-source selection. */
typedef struct ExtractFileSelection
{
    int explicit;
    const char **sources;
    size_t count;
} ExtractFileSelection;

int extract_selection(Context *c, const cJSON *request, const cJSON *plan,
                      ExtractFileSelection *selection);
int extract_file_selected(const ExtractFileSelection *selection, const int *categories,
                          const cJSON *file);
cJSON *extract_tree(Context *c, cJSON *files);
#endif
