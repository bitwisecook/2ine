/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 alerts fall back to the DOS speaker when no sound scheme is installed. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmwin.h"
#include "os2/include/private/pmp.h"

enum
{
    /* Alert tones in hertz, one per MessageBeep sound, and their length. */
    BEEP_DEFAULT_HZ = 440,
    BEEP_HAND_HZ = 220,
    BEEP_QUESTION_HZ = 660,
    BEEP_EXCLAMATION_HZ = 880,
    BEEP_ASTERISK_HZ = 1047,
    BEEP_DURATION_MS = 120
};

BOOL MessageBeep(ULONG type)
{
    TRACE_NATIVE("MessageBeep(%X)", type);
    ULONG frequency;
    switch (type)
    {
        case WIN32_MB_OK:
        case UINT32_MAX:
            frequency = BEEP_DEFAULT_HZ;
            break;
        case WIN32_MB_ICONHAND:
            frequency = BEEP_HAND_HZ;
            break;
        case WIN32_MB_ICONQUESTION:
            frequency = BEEP_QUESTION_HZ;
            break;
        case WIN32_MB_ICONEXCLAMATION:
            frequency = BEEP_EXCLAMATION_HZ;
            break;
        case WIN32_MB_ICONASTERISK:
            frequency = BEEP_ASTERISK_HZ;
            break;
        default:
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
    typedef APIRET(OS2API * BeepFn)(ULONG, ULONG);
    BeepFn beep = (BeepFn)open32DosService("DosBeep");
    ULONG error = beep && GProcessState.registerAudioGenerator ? beep(frequency, BEEP_DURATION_MS)
                                                               : ERROR_NOT_SUPPORTED;
    if (error)
        open32SetError(error);
    return error == 0;
}

LONG MessageBox(ULONG owner, PSZ text, PSZ caption, ULONG flags)
{
    /* Windows and PM number the button, icon and modality groups differently.
       An unknown group selects the default; result numbers coincide through IDNO. */
    static const ULONG buttons[] = {MB_OK,          MB_OKCANCEL, MB_ABORTRETRYIGNORE,
                                    MB_YESNOCANCEL, MB_YESNO,    MB_RETRYCANCEL};
    static const ULONG icons[] = {MB_NOICON, MB_ICONHAND, MB_ICONQUESTION, MB_ICONEXCLAMATION,
                                  MB_ICONASTERISK};
    /* Win32 packs the type, icon and default button into successive nibbles. */
    ULONG group = flags & WIN32_MB_TYPEMASK, icon = (flags >> 4) & 15, button = (flags >> 8) & 15;
    ULONG native = MB_MOVEABLE | (group < 6 ? buttons[group] : 0) | (icon < 5 ? icons[icon] : 0) |
                   (button == 1 || button == 2 ? button << 8 : 0) |
                   ((flags & WIN32_MB_MODEMASK) == WIN32_MB_SYSTEMMODAL ? MB_SYSTEMMODAL : 0);
    /* Open32 removes the close action from Yes/No boxes, leaving only their answers. */
    PSZ body = text ? text : "", title = caption ? caption : "";
    HWND parent = owner ? owner : HWND_DESKTOP;
    ULONG result = native & MB_YESNO ? lx_pm_message_box_no_close(parent, body, title, native)
                                     : WinMessageBox(HWND_DESKTOP, parent, body, title, 0, native);
    static const ULONG answers[] = {0, 1, 2, 3, 4, 5, 6, 7};
    return result < sizeof answers / sizeof *answers ? answers[result] : 0;
}
