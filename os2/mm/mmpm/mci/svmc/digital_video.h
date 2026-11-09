/* Public domain. No rights reserved. Wholly AI-authored. */
/* The digital-video device as seen by mciSendCommand, mciSendString and MDM
   termination. A command for another device returns with *handled 0. */
#ifndef TWOINE_MMPM_DIGITAL_VIDEO_H
#define TWOINE_MMPM_DIGITAL_VIDEO_H
#include "os2/include/os2def.h"
ULONG lx_mci_video_command(USHORT device, USHORT message, ULONG flags, PVOID parameters,
                           USHORT user, int *handled);
ULONG lx_mci_video_string(const char *command, char *output, USHORT length, ULONG window,
                          USHORT user, int *handled);
void lx_mci_video_shutdown(void);
#endif
