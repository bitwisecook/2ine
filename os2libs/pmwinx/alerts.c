/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 alerts fall back to the DOS speaker when no sound scheme is installed. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
BOOL MessageBeep(ULONG type)
{
    TRACE_NATIVE("MessageBeep(%X)", type);
    ULONG frequency;
    switch (type)
    {
        case 0:
        case UINT32_MAX:
            frequency = 440;
            break;
        case 0x10:
            frequency = 220;
            break;
        case 0x20:
            frequency = 660;
            break;
        case 0x30:
            frequency = 880;
            break;
        case 0x40:
            frequency = 1047;
            break;
        default:
            open32SetError(87);
            return FALSE;
    }
    typedef APIRET(OS2API * BeepFn)(ULONG, ULONG);
    BeepFn beep = (BeepFn)open32DosService("DosBeep");
    ULONG error = beep && GLoaderState.registerAudioGenerator ? beep(frequency, 120) : 50;
    if (error)
        open32SetError(error);
    return error == 0;
}
