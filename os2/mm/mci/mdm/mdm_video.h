/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_MDM_VIDEO_H
#define TWOINE_MDM_VIDEO_H
#include "os2/include/os2types.h"
ULONG lx_mci_video_command(USHORT device, USHORT message, ULONG flags, PVOID parameters,
                           USHORT user, int *handled);
ULONG lx_mci_video_string(const char *command, char *output, USHORT length, ULONG window,
                          USHORT user, int *handled);
void lx_mci_video_shutdown(void);
#endif
