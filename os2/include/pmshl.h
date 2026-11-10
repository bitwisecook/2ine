/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMSHL_H_
#define _INCL_PMSHL_H_

#include "os2/include/pmwin.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Shell definitions */

    typedef HANDLE HINI;

#define HINI_PROFILE ((HINI)0)

#define HINI_USERPROFILE ((HINI)0xffffffffu)

#define HINI_SYSTEMPROFILE ((HINI)0xfffffffeu)

    enum
    {
        MAXNAMEL = 60
    };

    /* PROGCATEGORY values */
    enum
    {
        PROG_DEFAULT = 0,
        PROG_FULLSCREEN = 1,
        PROG_WINDOWABLEVIO = 2,
        PROG_PM = 3,
        PROG_GROUP = 5,
        PROG_REAL = 4,
        PROG_VDM = 4,
        PROG_WINDOWEDVDM = 7,
        PROG_DLL = 6,
        PROG_PDD = 8,
        PROG_VDD = 9,
        PROG_WINDOW_REAL = 10,
        PROG_WINDOW_PROT = 11,
        PROG_30_STD = 11,
        PROG_WINDOW_AUTO = 12,
        PROG_SEAMLESSVDM = 13,
        PROG_30_STDSEAMLESSVDM = 13,
        PROG_SEAMLESSCOMMON = 14,
        PROG_30_STDSEAMLESSCOMMON = 14,
        PROG_31_STDSEAMLESSVDM = 15,
        PROG_31_STDSEAMLESSCOMMON = 16,
        PROG_31_ENHSEAMLESSVDM = 17,
        PROG_31_ENHSEAMLESSCOMMON = 18,
        PROG_31_ENH = 19,
        PROG_31_STD = 20,
        PROG_DOS_GAME = 21,
        PROG_WIN_GAME = 22,
        PROG_DOS_MODE = 23,
        PROG_RESERVED = 255
    };

    typedef struct
    {
        ULONG cchUserName;
        PSZ pszUserName;
        ULONG cchSysName;
        PSZ pszSysName;
    } PRFPROFILE, *PPRFPROFILE;

    /* INCL_WINPROGRAMLIST */

    enum
    {
        SHE_VISIBLE = 0x00,
        SHE_INVISIBLE = 0x01,
        SHE_RESERVED = 0xFF,
        SHE_UNPROTECTED = 0x00,
        SHE_PROTECTED = 0x02
    };

    /* WinStartApp options */
    enum
    {
        SAF_VALIDFLAGS = 0x001F,
        SAF_INSTALLEDCMDLINE = 0x0001,
        SAF_STARTCHILDAPP = 0x0002,
        SAF_MAXIMIZED = 0x0004,
        SAF_MINIMIZED = 0x0008,
        SAF_BACKGROUND = 0x0010
    };

    /* Host form of PROGDETAILS; bindings decode the 72-byte guest record into it. */
    typedef struct
    {
        ULONG Length;
        ULONG progc, fbVisible;
        PSZ pszTitle, pszExecutable, pszParameters, pszStartupDir, pszIcon, pszEnvironment;
        SWP swpInitial;
    } PROGDETAILS, *PPROGDETAILS;

    OS2EXPORT ULONG OS2API WinStartApp(HWND notify, PPROGDETAILS details, PSZ parameters,
                                       PVOID reserved, ULONG options);

    /* INCL_WINSWITCHLIST */

    typedef struct
    {
        ULONG hwnd, hwndIcon, hprog, idProcess, idSession, uchVisibility, fbJump;
        CHAR szSwtitle[MAXNAMEL + 4];
        ULONG bProgType;
    } SWCNTRL, *PSWCNTRL;

    enum
    {
        SWL_VISIBLE = 0x04,
        SWL_INVISIBLE = 0x01,
        SWL_GRAYED = 0x02,
        SWL_JUMPABLE = 0x02,
        SWL_NOTJUMPABLE = 0x01
    };

    OS2EXPORT ULONG OS2API WinCreateSwitchEntry(HAB hab, PSWCNTRL entry);

    OS2EXPORT ULONG OS2API WinQueryTaskTitle(ULONG session, PSZ title, ULONG capacity);

    OS2EXPORT ULONG OS2API WinAddSwitchEntry(PSWCNTRL entry);

    OS2EXPORT ULONG OS2API WinRemoveSwitchEntry(ULONG handle);

    OS2EXPORT ULONG OS2API WinChangeSwitchEntry(ULONG handle, PSWCNTRL entry);

    OS2EXPORT ULONG OS2API WinQuerySwitchEntry(ULONG handle, PSWCNTRL entry);

    OS2EXPORT ULONG OS2API WinQuerySwitchHandle(ULONG window, ULONG process);

    OS2EXPORT ULONG OS2API WinQueryTaskSizePos(HANDLE hab, ULONG session, PSWP position);

    /* INCL_WINSHELLDATA */

    OS2EXPORT HINI OS2API PrfOpenProfile(HANDLE hab, PSZ name);

    OS2EXPORT BOOL OS2API PrfCloseProfile(HINI profile);

    OS2EXPORT BOOL OS2API PrfQueryProfile(HANDLE hab, PPRFPROFILE profile);

    OS2EXPORT LONG OS2API PrfQueryProfileInt(HINI profile, PSZ app, PSZ key, LONG fallback);

    OS2EXPORT ULONG OS2API PrfQueryProfileString(HINI profile, PSZ app, PSZ key, PSZ fallback,
                                                 PVOID data, ULONG capacity);

    OS2EXPORT BOOL OS2API PrfWriteProfileString(HINI profile, PSZ app, PSZ key, PSZ data);

    OS2EXPORT BOOL OS2API PrfQueryProfileData(HINI profile, PSZ app, PSZ key, PVOID data,
                                              PULONG size);

    OS2EXPORT BOOL OS2API PrfWriteProfileData(HINI profile, PSZ app, PSZ key, PVOID data,
                                              ULONG size);

    OS2EXPORT BOOL OS2API PrfQueryProfileSize(HINI profile, PSZ app, PSZ key, PULONG size);

#ifdef __cplusplus
}
#endif

#endif
