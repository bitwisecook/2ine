/* Public domain. No rights reserved. Wholly AI-authored. */
/* Installer receipts: the record of the last installer a ball ran. */
#ifndef TB_ENVIRONMENT_INSTALL_RECORDS_H
#define TB_ENVIRONMENT_INSTALL_RECORDS_H
#include "ball/store.h"

/* Completes a launch receipt with the exit code, the Workplace Shell
   registrations and a dependency audit, and saves it as logs/last-install.json. */
int environment_record_install(TbBall *c, const cJSON *request);
#endif
