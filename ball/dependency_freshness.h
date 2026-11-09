/* Public domain. No rights reserved. Wholly AI-authored. */
/* Metadata freshness for installed imports and their configuration/media inputs. */
#ifndef TB_BALL_DEPENDENCY_FRESHNESS_H
#define TB_BALL_DEPENDENCY_FRESHNESS_H
#include "ball/store.h"

char *dependency_fingerprint(TbBall *parent, const cJSON *sources, const char *build,
                             const cJSON *component_files);
#endif
