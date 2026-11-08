/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMSHAPI_H_
#define _INCL_PMSHAPI_H_

#include "os2/include/pmwin.h"
#ifdef __cplusplus
extern "C"
{
#endif

    typedef HANDLE HINI;
#define HINI_PROFILE ((HINI)0)
#define HINI_USERPROFILE ((HINI)0xffffffffu)
#define HINI_SYSTEMPROFILE ((HINI)0xfffffffeu)

    typedef struct
    {
        ULONG cchUserName;
        PSZ pszUserName;
        ULONG cchSysName;
        PSZ pszSysName;
    } PRFPROFILE, *PPRFPROFILE;

    typedef struct
    {
        ULONG hwnd, hwndIcon, hprog, idProcess, idSession, uchVisibility, fbJump;
        CHAR szSwtitle[64];
        ULONG bProgType;
    } SWCNTRL, *PSWCNTRL;

    OS2EXPORT ULONG OS2API WinCreateSwitchEntry(HAB hab, PSWCNTRL entry);
    OS2EXPORT ULONG OS2API WinQueryTaskTitle(ULONG session, PSZ title, ULONG capacity);
    OS2EXPORT ULONG OS2API WinAddSwitchEntry(PSWCNTRL entry);
    OS2EXPORT ULONG OS2API WinRemoveSwitchEntry(ULONG handle);
    OS2EXPORT ULONG OS2API WinChangeSwitchEntry(ULONG handle, PSWCNTRL entry);
    OS2EXPORT ULONG OS2API WinQuerySwitchEntry(ULONG handle, PSWCNTRL entry);
    OS2EXPORT ULONG OS2API WinQuerySwitchHandle(ULONG window, ULONG process);

    OS2EXPORT ULONG OS2API WinQueryTaskSizePos(HANDLE hab, ULONG session, PSWP position);

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
    /* WinStartApp for callers that need the PM error without a PM anchor block.
       Returns the HAPP, or zero with the PMERR_ value in *error. */
    ULONG lx_pmshapi_start_app(HWND notify, PPROGDETAILS details, PSZ parameters, ULONG options,
                               ULONG *error);

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
