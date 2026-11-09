/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launcher argument text: OS/2 command-line quoting and splitting. */
#ifndef TB_EMULATOR_LAUNCH_ARGV_H
#define TB_EMULATOR_LAUNCH_ARGV_H
#include "ball/store.h"

/* Quotes each argument in double quotes, doubling embedded quotes. */
cJSON *launch_format_arguments(TbBall *c, const cJSON *request);
/* Splits text at unquoted white space; a doubled quote inside quotes is literal. */
cJSON *launch_parse_arguments(TbBall *c, const cJSON *request);
#endif
