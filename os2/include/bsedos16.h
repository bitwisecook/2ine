/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* The USE16 DOSCALLS, queue, session, NLS and message entries, as the DDK's
   bsedos16.h declares them for 16-bit callers. */

#ifndef _INCL_BSEDOS16_H_
#define _INCL_BSEDOS16_H_

#include "os2/include/bsedos.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_DOSPROCESS: 16-bit entry points and structures */

    typedef struct
    {
        USHORT codeTerminate;
        USHORT codeResult;
    } RESULTCODES16, *PRESULTCODES16;

    typedef struct
    {
        USHORT pid;
        USHORT tid;
        USHORT pidParent;
    } PIDINFO16, *PPIDINFO16;

    OS2EXPORT APIRET16 OS2API16 Dos16EnterCritSec(VOID);

    OS2EXPORT APIRET16 OS2API16 Dos16ExitCritSec(VOID);

    OS2EXPORT APIRET16 OS2API16 Dos16SetPrty(USHORT scope, USHORT prtyclass, SHORT delta,
                                             USHORT pid);

    OS2EXPORT APIRET16 OS2API16 Dos16KillProcess(USHORT scope, USHORT pid);

    OS2EXPORT APIRET16 OS2API16 Dos16CreateThread(PVOID pfnFun, PVOID pTid, PVOID pbStack);

    OS2EXPORT VOID OS2API16 Dos16Exit(USHORT action, USHORT result);

    OS2EXPORT APIRET16 OS2API16 Dos16GetPID(PPIDINFO16 ppidinfo);

    OS2EXPORT APIRET16 OS2API16 Dos16ExecPgm(PCHAR pObjname, SHORT cbObjname, USHORT execFlag,
                                             PSZ pArg, PSZ pEnv, PRESULTCODES16 pRes, PSZ pName);

    OS2EXPORT APIRET16 OS2API16 Dos16CWait(USHORT action, USHORT option, PRESULTCODES16 pres,
                                           PUSHORT ppid, USHORT pid);

    OS2EXPORT APIRET16 OS2API16 Dos16Beep(USHORT freq, USHORT dur);

    /* INCL_DOSFILEMGR: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16QFileInfo(USHORT hf, USHORT level, PVOID buf, USHORT cb);

    OS2EXPORT APIRET16 OS2API16 Dos16QPathInfo(PSZ path, USHORT level, PVOID buf, USHORT cb,
                                               ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16BufReset(USHORT file);

    OS2EXPORT APIRET16 OS2API16 Dos16QFHandState(USHORT file, PUSHORT state);

    OS2EXPORT APIRET16 OS2API16 Dos16SetFHandState(USHORT file, USHORT state);

    OS2EXPORT APIRET16 OS2API16 Dos16SetFilePtrL(USHORT file, ULONG high, ULONG low, ULONG method,
                                                 PVOID position);

    OS2EXPORT APIRET16 OS2API16 Dos16SelectDisk(USHORT drive);

    OS2EXPORT APIRET16 OS2API16 Dos16SetFileInfo(USHORT hf, USHORT level, PVOID buf, USHORT cb);

    OS2EXPORT APIRET16 OS2API16 Dos16SetMaxFH(USHORT cfh);

    OS2EXPORT APIRET16 OS2API16 Dos16NewSize(USHORT hf, ULONG size);

    OS2EXPORT APIRET16 OS2API16 Dos16QVerify(PUSHORT enabled);

    OS2EXPORT APIRET16 OS2API16 Dos16RmDir(PSZ path, ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16SetVerify(USHORT enabled);

    OS2EXPORT APIRET16 OS2API16 Dos16QFSAttach(PSZ device, USHORT ordinal, USHORT level,
                                               PVOID buffer, PUSHORT size, ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16ForceDelete(PSZ path, ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16QHandType(USHORT handle, PUSHORT ptype, PUSHORT pflags);

    OS2EXPORT APIRET16 OS2API16 Dos16EditName(USHORT level, PSZ source, PSZ edit, PBYTE target,
                                              USHORT capacity);

    OS2EXPORT APIRET16 OS2API16 Dos16Write(USHORT h, PVOID buf, USHORT buflen, PUSHORT actual);

    OS2EXPORT APIRET16 OS2API16 Dos16QCurDisk(PUSHORT drivenum, PULONG drivemap);

    OS2EXPORT APIRET16 OS2API16 Dos16QCurDir(USHORT drivenum, PBYTE dirpath, PUSHORT dirpathlen);

    OS2EXPORT APIRET16 OS2API16 Dos16QFsInfo(USHORT drivenum, USHORT fslevel, PBYTE infobuf,
                                             USHORT infobufsize);

    OS2EXPORT APIRET16 OS2API16 Dos16SetFileMode(PCHAR filepath, USHORT attr, ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16QFileMode(PCHAR filepath, PUSHORT attr, ULONG res);

    OS2EXPORT APIRET16 OS2API16 Dos16Open(PSZ pszFileName, PUSHORT pHf, PUSHORT pusAction,
                                          ULONG cbFile, USHORT ulAttribute, USHORT fsOpenFlags,
                                          USHORT fsOpenMode, ULONG ulReserved);

    OS2EXPORT APIRET16 OS2API16 Dos16Open2(PSZ pszFileName, PUSHORT pHf, PUSHORT pusAction,
                                           ULONG cbFile, USHORT ulAttribute, USHORT fsOpenFlags,
                                           ULONG fsOpenMode, PVOID ea, ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16OpenL(PSZ name, PUSHORT file, PUSHORT action, ULONG high,
                                           ULONG low, USHORT attributes, USHORT flags, ULONG mode,
                                           PVOID ea, ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16FindFirst(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute,
                                               PVOID pfindbuf, USHORT cbBuf, PUSHORT pcFileNames,
                                               ULONG res);

    OS2EXPORT APIRET16 OS2API16 Dos16FindFirst2(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute,
                                                PVOID pfindbuf, USHORT cbBuf, PUSHORT pcFileNames,
                                                USHORT level, ULONG res);

    OS2EXPORT APIRET16 OS2API16 Dos16ChDir(PSZ pszDir, ULONG res);

    OS2EXPORT APIRET16 OS2API16 Dos16Close(USHORT hFile);

    OS2EXPORT APIRET16 OS2API16 Dos16FindNext(USHORT hDir, PVOID pfindbuf, USHORT cbfindbuf,
                                              PUSHORT pcFilenames);

    OS2EXPORT APIRET16 OS2API16 Dos16FindClose(USHORT hDir);

    OS2EXPORT APIRET16 OS2API16 Dos16Delete(PSZ pszFile, ULONG res);

    OS2EXPORT APIRET16 OS2API16 Dos16DupHandle(USHORT hFile, PUSHORT pHfile);

    OS2EXPORT APIRET16 OS2API16 Dos16Read(USHORT hFile, PVOID pBuffer, USHORT cbRead,
                                          PUSHORT pcbActual);

    OS2EXPORT APIRET16 OS2API16 Dos16ChgFilePtr(USHORT handle, LONG distance, USHORT whence,
                                                PULONG newoffset);

    OS2EXPORT APIRET16 OS2API16 Dos16MkDir(PSZ pszDirName, PEAOP2 peaop2);

    OS2EXPORT APIRET16 OS2API16 Dos16MkDir2(PSZ pszDirName, PEAOP2 peaop2);

    OS2EXPORT APIRET16 OS2API16 Dos16Move(PSZ pszOld, PSZ pszNew, ULONG reserved);

    /* INCL_DOSMEMMGR: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16GetSeg(USHORT sel);

    OS2EXPORT APIRET16 OS2API16 Dos16LockSeg(USHORT sel);

    OS2EXPORT APIRET16 OS2API16 Dos16UnlockSeg(USHORT sel);

    OS2EXPORT APIRET16 OS2API16 Dos16HugeShift(PUSHORT pshift);

    OS2EXPORT APIRET16 OS2API16 Dos16HugeIncr(USHORT sel, PULONG psize);

    OS2EXPORT APIRET16 OS2API16 Dos16CreateCSAlias(USHORT selDS, PUSHORT pselCS);

    OS2EXPORT APIRET16 OS2API16 Dos16GetShrSeg(PSZ name, PUSHORT selector);

    OS2EXPORT APIRET16 OS2API16 Dos16SizeSeg(USHORT selector, PULONG size);

    OS2EXPORT APIRET16 OS2API16 Dos16GetHugeShift(PUSHORT pcount);

    OS2EXPORT APIRET16 OS2API16 Dos16AllocSeg(USHORT size, PUSHORT psel, USHORT flags);

    OS2EXPORT APIRET16 OS2API16 Dos16ReallocSeg(USHORT size, USHORT sel);

    OS2EXPORT APIRET16 OS2API16 Dos16SubSet(USHORT selector, USHORT flags, USHORT size);

    OS2EXPORT APIRET16 OS2API16 Dos16SubAlloc(USHORT selector, PUSHORT offset, USHORT size);

    OS2EXPORT APIRET16 OS2API16 Dos16SubFree(USHORT selector, USHORT offset, USHORT size);

    OS2EXPORT APIRET16 OS2API16 Dos16FreeSeg(USHORT sel);

    /* INCL_DOSSEMAPHORES: 16-bit entry points and structures */

    /* The DosSem* RAM semaphore entries are USE16 only. */
    typedef int HSEM16, *PHSEM16;

    /* DOSFSRSEM: USHORT cb, PID pid, TID tid, USHORT cUsage, USHORT client, ULONG sem. */
    typedef PVOID PDOSFSRSEM;

    OS2EXPORT APIRET16 OS2API16 DosSemRequest(PHSEM16 sem, LONG ms);

    OS2EXPORT APIRET16 OS2API16 DosSemClear(PHSEM16 sem);

    OS2EXPORT APIRET16 OS2API16 DosSemWait(PHSEM16 sem, LONG ms);

    OS2EXPORT APIRET16 OS2API16 DosSemSet(PHSEM16 sem);

    OS2EXPORT APIRET16 OS2API16 Dos16FSRamSemRequest(PDOSFSRSEM sem, LONG timeout);

    OS2EXPORT APIRET16 OS2API16 Dos16FSRamSemClear(PDOSFSRSEM sem);

    /* INCL_DOSDATETIME: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16GetDateTime(PDATETIME pdt);

    OS2EXPORT APIRET16 OS2API16 Dos16SetDateTime(PDATETIME datetime);

    /* INCL_DOSMODULEMGR: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16LoadModule(PCHAR failure, USHORT capacity, PSZ name,
                                                PUSHORT module);

    /* The encoded name accepts either a 16:16 string or a selector-zero ordinal. */
    OS2EXPORT APIRET16 OS2API16 Dos16GetProcAddr(USHORT module, ULONG name, PULONG address);

    OS2EXPORT APIRET16 OS2API16 Dos16FreeModule(USHORT module);

    OS2EXPORT APIRET16 OS2API16 Dos16GetModName(USHORT module, USHORT capacity, PCHAR name);

    OS2EXPORT APIRET16 OS2API16 Dos16GetModHandle(PSZ name, PUSHORT module);

    /* INCL_DOSNLS: 16-bit entry points and structures */

    typedef struct
    {
        USHORT country;
        USHORT codepage;
    } COUNTRYCODE16, *PCOUNTRYCODE16;

    typedef struct
    {
        USHORT country;
        USHORT codepage;
        USHORT fsDateFmt;
        CHAR szCurrency[5];
        CHAR szThousandsSeparator[2];
        CHAR szDecimal[2];
        CHAR szDateSeparator[2];
        CHAR szTimeSeparator[2];
        UCHAR fsCurrencyFmt;
        UCHAR cDecimalPlace;
        UCHAR fsTimeFmt;
        USHORT abReserved1[2];
        CHAR szDataSeparator[2];
        USHORT abReserved2[5];
    } COUNTRYINFO16, *PCOUNTRYINFO16;

    OS2EXPORT APIRET16 OS2API16 Dos16GetDBCSEv(USHORT buflen, PCOUNTRYCODE16 pcc, PCHAR buf);

    OS2EXPORT APIRET16 OS2API16 Dos16SetCp(USHORT codepage, USHORT reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16SetProcCp(USHORT codepage, USHORT reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16GetCp(USHORT cb, PUSHORT arCP, PUSHORT pcCP);

    OS2EXPORT APIRET16 OS2API16 Dos16CaseMap(USHORT len, PCOUNTRYCODE16 pcc, PCHAR pch);

    OS2EXPORT APIRET16 OS2API16 Dos16GetCollate(USHORT len, PCOUNTRYCODE16 pcc, PCHAR pch,
                                                PUSHORT dlen);

    OS2EXPORT APIRET16 OS2API16 Dos16GetCtryInfo(USHORT len, PCOUNTRYCODE16 pcc, PCOUNTRYINFO16 pch,
                                                 PUSHORT dlen);

    /* INCL_DOSEXCEPTIONS: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16HoldSignal(USHORT action);

    /* INCL_DOSQUEUES: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16MakePipe(PUSHORT reader, PUSHORT writer, USHORT size);

    OS2EXPORT APIRET16 OS2API16 Dos16ReadQueue(USHORT queue, PULONG request, PUSHORT length,
                                               PULONG buffer, USHORT element, USHORT nowait,
                                               PBYTE priority, ULONG semaphore);

    OS2EXPORT APIRET16 OS2API16 Dos16CloseQueue(USHORT queue);

    OS2EXPORT APIRET16 OS2API16 Dos16CreateQueue(PUSHORT queue, USHORT order, PSZ name);

    /* INCL_DOSMISC: 16-bit entry points and structures */

    typedef struct
    {
        ULONG time;
        ULONG msecs;
        UCHAR hour;
        UCHAR minutes;
        UCHAR seconds;
        UCHAR hundredths;
        USHORT timezone;
        USHORT cusecTimerInterval;
        UCHAR day;
        UCHAR month;
        USHORT year;
        UCHAR weekday;
        UCHAR uchMajorVersion;
        UCHAR uchMinorVersion;
        UCHAR chRevisionLetter;
        UCHAR sgCurrent;
        UCHAR sgMax;
        UCHAR cHugeShift;
        UCHAR fProtectModeOnly;
        USHORT pidForeground;
        UCHAR fDynamicSched;
        UCHAR csecMaxWait;
        USHORT cmsecMinSlice;
        USHORT cmsecMaxSlice;
        USHORT bootdrive;
        UCHAR amecRAS[32];
        UCHAR csgWindowableVioMax;
        UCHAR csgPMMax;
    } GINFOSEG;

    typedef struct
    {
        PID pidCurrent;
        PID pidParent;
        USHORT prtyCurrent;
        TID tidCurrent;
        USHORT sgCurrent;
        UCHAR rfProcStatus;
        UCHAR dummy1;
        BOOL fForeground;
        UCHAR typeProcess;
        UCHAR dummy2;
        SEL selEnvironment;
        USHORT offCmdLine;
        USHORT cbDataSegment;
        USHORT cbStack;
        USHORT cbHeap;
        HMODULE hmod;
        SEL selDS;
    } LINFOSEG;

    OS2EXPORT APIRET16 OS2API16 Dos16Error(USHORT error);

    OS2EXPORT APIRET16 OS2API16 Dos16SearchPath(USHORT flags, PSZ path, PSZ name, PBYTE result,
                                                USHORT capacity);

    OS2EXPORT APIRET16 OS2API16 Dos16SetExtLIBPATH(PSZ path, ULONG flags);

    OS2EXPORT APIRET16 OS2API16 Dos16QueryExtLIBPATH(PCHAR path, ULONG flags);

    OS2EXPORT APIRET16 OS2API16 Dos16GetVersion(PUSHORT pver);

    OS2EXPORT APIRET16 OS2API16 Dos16ScanEnv(PSZ name, PULONG value);

    OS2EXPORT APIRET16 OS2API16 Dos16GetMachineMode(PBYTE pmode);

    OS2EXPORT APIRET16 OS2API16 Dos16GetEnv(PUSHORT psel, PUSHORT pcmdoffset);

    OS2EXPORT APIRET16 OS2API16 Dos16SetVec(USHORT vecnum, PFN routine,
                                            PVOID prevaddress); /* prevaddress is a PFN * */
    OS2EXPORT APIRET16 OS2API16 Dos16SetSigHandler(
        PFN routine, PVOID prevaddress, PUSHORT prevaction, USHORT action,
        USHORT signumber); /* prevaddress is a PFNSIGHANDLER * routine is a PFNSIGHANDLER */
    OS2EXPORT APIRET16 OS2API16 Dos16GetInfoSeg(PSEL globalseg, PSEL localseg);

    OS2EXPORT APIRET16 OS2API16 Dos16PutMessage(USHORT hfile, USHORT cbMsg, PCHAR pBuf);

    OS2EXPORT APIRET16 OS2API16 Dos16TrueGetMessage(PVOID pTable, USHORT cTable, PCHAR pData,
                                                    USHORT cbBuf, USHORT msgnum, PCHAR pFilename,
                                                    PUSHORT pcbMsg, PVOID msgseg);

    /* INCL_DOSSESMGR: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16QAppType(PSZ name, PUSHORT flags);

    OS2EXPORT APIRET16 OS2API16 Dos16SMPMPresent(PUSHORT present);

    OS2EXPORT APIRET16 OS2API16 Dos16SMSetTitle(PSZ title);

    OS2EXPORT APIRET16 OS2API16 Dos16StopSession(USHORT scope, USHORT session, ULONG reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16SetSession(USHORT session, PSTATUSDATA status);

    /* STARTDATA embeds encoded 16:16 pointers and is decoded by this entry. */
    OS2EXPORT APIRET16 OS2API16 Dos16StartSession(ULONG data, PUSHORT session, PUSHORT pid);

    /* INCL_DOSDEVICES: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16DevConfig(PVOID pdevinfo, USHORT item, USHORT reserved);

    OS2EXPORT APIRET16 OS2API16 Dos16DevIOCtl(PVOID pdata, PVOID pparms, USHORT fun,
                                              USHORT category, USHORT hdev);

    OS2EXPORT APIRET16 OS2API16 Dos16DevIOCtl2(PVOID data, USHORT data_size, PVOID parameters,
                                               USHORT parameter_size, USHORT function,
                                               USHORT category, USHORT device);

    /* INCL_DOSPROFILE: 16-bit entry points and structures */

    OS2EXPORT APIRET16 OS2API16 Dos16QProcStatus(PVOID buffer, USHORT size);

#ifdef __cplusplus
}
#endif

#endif
