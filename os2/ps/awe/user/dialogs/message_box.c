/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 alerts fall back to the DOS speaker when no sound scheme is installed. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmwin.h"
#include "os2/include/private/pmp.h"

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
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
    typedef APIRET(OS2API * BeepFn)(ULONG, ULONG);
    BeepFn beep = (BeepFn)open32DosService("DosBeep");
    ULONG error = beep && GProcessState.registerAudioGenerator ? beep(frequency, 120) : 50;
    if (error)
        open32SetError(error);
    return error == 0;
}

LONG MessageBox(ULONG owner, PSZ text, PSZ caption, ULONG flags)
{
    /* Windows and PM number the button, icon and modality groups differently.
       An unknown group selects the default; result numbers coincide through IDNO. */
    static const ULONG buttons[] = {0, 1, 3, 5, 4, 2};
    static const ULONG icons[] = {0, 0x40, 0x10, 0x20, 0x30};
    ULONG group = flags & 15, icon = (flags >> 4) & 15, button = (flags >> 8) & 15;
    ULONG native = 0x4000 | (group < 6 ? buttons[group] : 0) | (icon < 5 ? icons[icon] : 0) |
                   (button == 1 || button == 2 ? button << 8 : 0) |
                   ((flags & 0x3000) == 0x1000 ? 0x1000 : 0);
    /* Open32 removes the close action from Yes/No boxes, leaving only their answers. */
    PSZ body = text ? text : "", title = caption ? caption : "";
    HWND parent = owner ? owner : HWND_DESKTOP;
    ULONG result = native & 4 ? lx_pm_message_box_no_close(parent, body, title, native)
                              : WinMessageBox(HWND_DESKTOP, parent, body, title, 0, native);
    static const ULONG answers[] = {0, 1, 2, 3, 4, 5, 6, 7};
    return result < sizeof answers / sizeof *answers ? answers[result] : 0;
}
