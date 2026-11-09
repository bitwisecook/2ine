/* Public domain. No rights reserved. Wholly AI-authored. */
/* mciSendString and mciGetErrorString. Only the digital-video device accepts
   string commands. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/mm/mmpm/mci/svmc/digital_video.h"

ULONG mciSendString(PSZ command, PSZ output, USHORT length, ULONG window, USHORT user)
{
    TRACE_NATIVE("mciSendString(%s)", command ? command : "<null>");
    if (output && length)
        output[0] = 0;
    int video_handled = 0;
    ULONG result = lx_mci_video_string(command, output, length, window, user, &video_handled);
    return video_handled ? result : MCIERR_DEVICE_NOT_READY;
}

ULONG mciGetErrorString(ULONG error, PSZ output, USHORT length)
{
    if (!output || !length)
        return 0;
    snprintf(output, length, "MCI error %lu%s", (unsigned long)error,
             error == MCIERR_DEVICE_NOT_READY ? ": device unavailable" : "");
    return 1;
}
