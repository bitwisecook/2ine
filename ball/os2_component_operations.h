/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read-only live state is independent of the per-ball mutation guard. */
#ifndef TB_BALL_OS2_COMPONENT_OPERATIONS_H
#define TB_BALL_OS2_COMPONENT_OPERATIONS_H
#include "ball/store.h"

cJSON *os2_component_state(TbBall *c, const cJSON *request);
cJSON *component_import(TbBall *c, const cJSON *request);
cJSON *remove_os2_components(TbBall *c, const cJSON *request);
cJSON *cancel_os2_component_operation(TbBall *c, const cJSON *request);
#endif
