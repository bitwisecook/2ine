/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest and host path translation over the ball's drive table. */
#ifndef TB_BALL_PATHS_H
#define TB_BALL_PATHS_H
#include "ball/ball_internal.h"

char *tb_ball_case_path(Context *c, const char *path);
int tb_ball_inside(const char *path, const char *root);
int tb_ball_current_drive(Context *c);
const char *tb_ball_drive_directory(Context *c, int drive);
char *tb_ball_host_path(Context *c, const char *name, int explicit_host);
char *tb_ball_guest_path(Context *c, const char *host);
char *tb_ball_guest_for_input(Context *c, const char *host, const char *input);
int tb_ball_validate_directories(Context *c, const cJSON *directories);
#endif
