/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMSHAPI_H_
#define _INCL_PMSHAPI_H_

#include "os2libs/pmwin.h"
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

    OS2EXPORT ULONG OS2API WinCreateSwitchEntry(HAB hab, PSWCNTRL entry) OS2APIINFO(121);
    OS2EXPORT ULONG OS2API WinQueryTaskTitle(ULONG session, PSZ title, ULONG capacity)
        OS2APIINFO(128);
    OS2EXPORT ULONG OS2API WinAddSwitchEntry(PSWCNTRL entry) OS2APIINFO(120);
    OS2EXPORT ULONG OS2API WinRemoveSwitchEntry(ULONG handle) OS2APIINFO(129);
    OS2EXPORT ULONG OS2API WinChangeSwitchEntry(ULONG handle, PSWCNTRL entry) OS2APIINFO(123);
    OS2EXPORT ULONG OS2API WinQuerySwitchEntry(ULONG handle, PSWCNTRL entry) OS2APIINFO(124);
    OS2EXPORT ULONG OS2API WinQuerySwitchHandle(ULONG window, ULONG process) OS2APIINFO(125);

    OS2EXPORT ULONG OS2API WinQueryTaskSizePos(HANDLE hab, ULONG session, PSWP position)
        OS2APIINFO(127);

    /* Host form of PROGDETAILS; bindings decode the 72-byte guest record into it. */
    typedef struct
    {
        ULONG Length;
        ULONG progc, fbVisible;
        PSZ pszTitle, pszExecutable, pszParameters, pszStartupDir, pszIcon, pszEnvironment;
        SWP swpInitial;
    } PROGDETAILS, *PPROGDETAILS;

    OS2EXPORT ULONG OS2API WinStartApp(HWND notify, PPROGDETAILS details, PSZ parameters,
                                       PVOID reserved, ULONG options) OS2APIINFO(119);
    /* WinStartApp for callers that need the PM error without a PM anchor block.
       Returns the HAPP, or zero with the PMERR_ value in *error. */
    ULONG lx_pmshapi_start_app(HWND notify, PPROGDETAILS details, PSZ parameters, ULONG options,
                               ULONG *error);

    OS2EXPORT HINI OS2API PrfOpenProfile(HANDLE hab, PSZ name) OS2APIINFO(102);
    OS2EXPORT BOOL OS2API PrfCloseProfile(HINI profile) OS2APIINFO(103);
    OS2EXPORT BOOL OS2API PrfQueryProfile(HANDLE hab, PPRFPROFILE profile) OS2APIINFO(107);
    OS2EXPORT LONG OS2API PrfQueryProfileInt(HINI profile, PSZ app, PSZ key, LONG fallback)
        OS2APIINFO(114);
    OS2EXPORT ULONG OS2API PrfQueryProfileString(HINI profile, PSZ app, PSZ key, PSZ fallback,
                                                 PVOID data, ULONG capacity) OS2APIINFO(115);
    OS2EXPORT BOOL OS2API PrfWriteProfileString(HINI profile, PSZ app, PSZ key, PSZ data)
        OS2APIINFO(116);
    OS2EXPORT BOOL OS2API PrfQueryProfileData(HINI profile, PSZ app, PSZ key, PVOID data,
                                              PULONG size) OS2APIINFO(117);
    OS2EXPORT BOOL OS2API PrfWriteProfileData(HINI profile, PSZ app, PSZ key, PVOID data,
                                              ULONG size) OS2APIINFO(118);
    OS2EXPORT BOOL OS2API PrfQueryProfileSize(HINI profile, PSZ app, PSZ key, PULONG size)
        OS2APIINFO(101);

#ifdef __cplusplus
}
#endif

#endif
