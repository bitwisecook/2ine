/* Public domain. No rights reserved. Wholly AI-authored. */
/* Receiving-host capability resolution is read-only; saved preferences stay portable. */
#ifndef TB_BALL_RUNTIME_FEATURES_H
#define TB_BALL_RUNTIME_FEATURES_H
#include "ball/store.h"

cJSON *runtime_feature_metadata(TbBall *c, const char *build);
void runtime_feature_policy(TbBall *c, const char *build);
cJSON *runtime_features(TbBall *c, const char *build);
int runtime_features_validate(TbBall *c, const char *build);
#endif
