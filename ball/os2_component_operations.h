/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read-only live state is independent of the per-ball mutation guard. */
#ifndef TB_BALL_OS2_COMPONENT_OPERATIONS_H
#define TB_BALL_OS2_COMPONENT_OPERATIONS_H
#include "ball/ball_internal.h"

cJSON *os2_component_state(Context *c, const cJSON *request);
cJSON *component_import(Context *c, const cJSON *request);
cJSON *remove_os2_components(Context *c, const cJSON *request);
cJSON *cancel_os2_component_operation(Context *c, const cJSON *request);
#endif
