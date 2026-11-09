/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_BSEDOS_H_
#define _INCL_BSEDOS_H_

#include "os2/include/os2def.h"
#include "os2/include/bsememf.h"
#include "os2/include/bsetib.h"
#include "os2/include/bsexcpt.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Path limits */

#define CCHMAXPATH 260

#define CCHMAXPATHCOMP 256

    /* INCL_DOSPROCESS: threads, processes, exit lists and priorities */

    enum
    {
        EXIT_THREAD,
        EXIT_PROCESS
    };

    typedef VOID APIENTRY FNEXITLIST(ULONG);

    typedef FNEXITLIST *PFNEXITLIST;

    enum
    {
        TC_EXIT,
        TC_HARDERROR,
        TC_TRAP,
        TC_KILLPROCESS,
        TC_EXCEPTION
    };

    enum
    {
        EXLST_ADD = 1,
        EXLST_REMOVE,
        EXLST_EXIT
    };

    enum
    {
        PRTYS_PROCESS = 0,
        PRTYS_PROCESSTREE = 1,
        PRTYS_THREAD = 2
    };

    enum
    {
        PRTYC_NOCHANGE = 0,
        PRTYC_IDLETIME = 1,
        PRTYC_REGULAR = 2,
        PRTYC_TIMECRITICAL = 3,
        PRTYC_FOREGROUNDSERVER = 4
    };

    enum
    {
        PRTYD_MINIMUM = -31,
        PRTYD_MAXIMUM = 31
    };

    enum
    {
        CREATE_READY,
        CREATE_SUSPENDED
    };

    enum
    {
        STACK_SPARSE = 0,
        STACK_COMMITTED = 2
    };

    typedef VOID APIENTRY FNTHREAD(ULONG);

    typedef FNTHREAD *PFNTHREAD;

    typedef struct
    {
        ULONG codeTerminate;
        ULONG codeResult;
    } RESULTCODES, *PRESULTCODES;

    enum
    {
        EXEC_SYNC,
        EXEC_ASYNC,
        EXEC_ASYNCRESULT,
        EXEC_TRACE,
        EXEC_BACKGROUND,
        EXEC_LOAD,
        EXEC_ASYNCRESULTDB
    };

    enum
    {
        DCWW_WAIT,
        DCWW_NOWAIT
    };

    enum
    {
        DCWA_PROCESS,
        DCWA_PROCESSTREE
    };

    OS2EXPORT APIRET OS2API DosSetPriority(ULONG scope, ULONG priority_class, LONG delta,
                                           ULONG target);

    OS2EXPORT APIRET OS2API DosEnterCritSec(VOID);

    OS2EXPORT APIRET OS2API DosExitCritSec(VOID);

    OS2EXPORT APIRET OS2API DosGetInfoBlocks(PTIB *pptib, PPIB *pppib);

    OS2EXPORT VOID OS2API DosExit(ULONG action, ULONG result);

    OS2EXPORT APIRET OS2API DosExitList(ULONG ordercode, PFNEXITLIST pfn);

    OS2EXPORT APIRET OS2API DosSuspendThread(TID id);

    OS2EXPORT APIRET OS2API DosResumeThread(TID id);

    OS2EXPORT APIRET OS2API DosCreateThread(PTID ptid, PFNTHREAD pfn, ULONG param, ULONG flag,
                                            ULONG cbStack);

    OS2EXPORT APIRET OS2API DosExecPgm(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg,
                                       PSZ pEnv, PRESULTCODES pRes, PSZ pName);

    OS2EXPORT APIRET OS2API DosKillProcess(ULONG action, PID process);

    OS2EXPORT APIRET OS2API DosWaitChild(ULONG action, ULONG option, PRESULTCODES pres, PPID ppid,
                                         PID pid);

    OS2EXPORT APIRET OS2API DosKillThread(TID id);

    OS2EXPORT APIRET OS2API DosWaitThread(PTID ptid, ULONG option);

    OS2EXPORT APIRET OS2API DosSleep(ULONG msec);

    OS2EXPORT APIRET OS2API DosAllocThreadLocalMemory(ULONG cb, PULONG *p);

    OS2EXPORT APIRET OS2API DosFreeThreadLocalMemory(ULONG *p);

    OS2EXPORT APIRET OS2API DosBeep(ULONG freq, ULONG dur);

    /* INCL_DOSFILEMGR: files, directories, file systems and handles */

    typedef struct
    {
        LONG lOffset;
        LONG lRange;
    } FILELOCK, *PFILELOCK;

    typedef void *PEAOP2;

    enum
    {
        FILE_EXISTED = 1,
        FILE_CREATED,
        FILE_TRUNCATED
    };

    enum
    {
        FILE_OPEN = 0x01,
        FILE_TRUNCATE = 0x02,
        FILE_CREATE = 0x10
    };

    enum
    {
        FILE_NORMAL = 0x00,
        FILE_READONLY = 0x01,
        FILE_HIDDEN = 0x02,
        FILE_SYSTEM = 0x04,
        FILE_DIRECTORY = 0x10,
        FILE_ARCHIVED = 0x20
    };

    enum
    {
        OPEN_ACTION_FAIL_IF_EXISTS = 0x0000,
        OPEN_ACTION_OPEN_IF_EXISTS = 0x0001,
        OPEN_ACTION_REPLACE_IF_EXISTS = 0x0002,
        OPEN_ACTION_FAIL_IF_NEW = 0x0000,
        OPEN_ACTION_CREATE_IF_NEW = 0x0010,
    };

    enum
    {
        OPEN_ACCESS_READONLY = 0x0000,
        OPEN_ACCESS_WRITEONLY = 0x0001,
        OPEN_ACCESS_READWRITE = 0x0002,
        OPEN_SHARE_DENYREADWRITE = 0x0010,
        OPEN_SHARE_DENYWRITE = 0x0020,
        OPEN_SHARE_DENYREAD = 0x0030,
        OPEN_SHARE_DENYNONE = 0x0040,
        OPEN_FLAGS_NOINHERIT = 0x0080,
        OPEN_FLAGS_NO_LOCALITY = 0x0000,
        OPEN_FLAGS_SEQUENTIAL = 0x0100,
        OPEN_FLAGS_RANDOM = 0x0200,
        OPEN_FLAGS_RANDOMSEQUENTIAL = 0x0300,
        OPEN_FLAGS_NO_CACHE = 0x1000,
        OPEN_FLAGS_FAIL_ON_ERROR = 0x2000,
        OPEN_FLAGS_WRITE_THROUGH = 0x4000,
        OPEN_FLAGS_DASD = 0x8000,
        OPEN_FLAGS_NONSPOOLED = 0x00040000,
        OPEN_FLAGS_PROTECTED_HANDLE = 0x40000000
    };

    enum
    {
        FILE_BEGIN,
        FILE_CURRENT,
        FILE_END
    };

    enum
    {
        HDIR_CREATE = -1,
        HDIR_SYSTEM = 1
    };

    enum
    {
        DCPY_EXISTING = 0x0001,
        DCPY_APPEND = 0x0002,
        DCPY_FAILEAS = 0x0004
    };

    enum
    {
        SEARCH_PATH = 0x0000,
        SEARCH_CUR_DIRECTORY = 0x0001,
        SEARCH_ENVIRONMENT = 0x0002,
        SEARCH_IGNORENETERRS = 0x0004
    };

    enum
    {
        FIL_STANDARD = 1,
        FIL_QUERYEASIZE = 2,
        FIL_QUERYEASFROMLIST = 3,
        FIL_QUERYFULLNAME = 5,
        FIL_STANDARDL = 11,
        FIL_QUERYEASIZEL = 12
    };

#pragma pack(push, 1)

    typedef struct
    {
        USHORT day : 5;
        USHORT month : 4;
        USHORT year : 7;
    } FDATE, *PFDATE;

    typedef struct
    {
        USHORT twosecs : 5;
        USHORT minutes : 6;
        USHORT hours : 5;
    } FTIME, *PFTIME;

#pragma pack(pop)

    typedef struct
    {
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
    } FILESTATUS3, *PFILESTATUS3;

    typedef struct
    {
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
        ULONG cbList;
    } FILESTATUS4, *PFILESTATUS4;

    enum
    {
        DSPI_WRTTHRU = 0x10,
    };

    enum
    {
        MUST_HAVE_READONLY = ((FILE_READONLY << 8) | FILE_READONLY),
        MUST_HAVE_HIDDEN = ((FILE_HIDDEN << 8) | FILE_HIDDEN),
        MUST_HAVE_SYSTEM = ((FILE_SYSTEM << 8) | FILE_SYSTEM),
        MUST_HAVE_DIRECTORY = ((FILE_DIRECTORY << 8) | FILE_DIRECTORY),
        MUST_HAVE_ARCHIVED = ((FILE_ARCHIVED << 8) | FILE_ARCHIVED)
    };

    typedef struct
    {
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        USHORT attrFile;
        UCHAR cchName;
        CHAR achName[CCHMAXPATHCOMP];
    } FILEFINDBUF, *PFILEFINDBUF;

    typedef struct
    {
        ULONG oNextEntryOffset;
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
        UCHAR cchName;
        CHAR achName[CCHMAXPATHCOMP];
    } FILEFINDBUF3, *PFILEFINDBUF3;

    typedef struct
    {
        ULONG oNextEntryOffset;
        FDATE fdateCreation;
        FTIME ftimeCreation;
        FDATE fdateLastAccess;
        FTIME ftimeLastAccess;
        FDATE fdateLastWrite;
        FTIME ftimeLastWrite;
        ULONG cbFile;
        ULONG cbFileAlloc;
        ULONG attrFile;
        ULONG cbList;
        UCHAR cchName;
        CHAR achName[CCHMAXPATHCOMP];
    } FILEFINDBUF4, *PFILEFINDBUF4;

    enum
    {
        FSAIL_QUERYNAME = 1,
        FSAIL_DEVNUMBER = 2,
        FSAIL_DRVNUMBER = 3
    };

    enum
    {
        FSAT_CHARDEV = 1,
        FSAT_PSEUDODEV = 2,
        FSAT_LOCALDRV = 3,
        FSAT_REMOTEDRV = 4
    };

    typedef struct
    {
        USHORT iType;
        USHORT cbName;
        USHORT cbFSDName;
        USHORT cbFSAData;
        UCHAR szName[1];
        UCHAR szFSDName[1];
        UCHAR rgFSAData[1];
    } FSQBUFFER2, *PFSQBUFFER2;

    enum
    {
        FSIL_ALLOC = 1,
        FSIL_VOLSER = 2
    };

#pragma pack(push, 1)

    typedef struct _VOLUMELABEL
    {
        BYTE cch;
        CHAR szVolLabel[12];
    } VOLUMELABEL, *PVOLUMELABEL;

    typedef struct _FSINFO
    {
        FDATE fdateCreation;
        FTIME ftimeCreation;
        VOLUMELABEL vol;
    } FSINFO, *PFSINFO;

    typedef struct _FSALLOCATE
    {
        ULONG idFileSystem;
        ULONG cSectorUnit;
        ULONG cUnit;
        ULONG cUnitAvail;
        USHORT cbSector;
    } FSALLOCATE, *PFSALLOCATE;

#pragma pack(pop)

    enum
    {
        FHT_DISKFILE = 0x0000,
        FHT_CHRDEV = 0x0001,
        FHT_PIPE = 0x0002
    };

    enum
    {
        ENUMEA_LEVEL_NO_VALUE = 1
    };

    enum
    {
        ENUMEA_REFTYPE_FHANDLE = 0,
        ENUMEA_REFTYPE_PATH = 1
    };

    OS2EXPORT APIRET OS2API DosDupHandle(HFILE hFile, PHFILE pHfile);

    OS2EXPORT APIRET OS2API DosWrite(HFILE hFile, PVOID pBuffer, ULONG cbWrite, PULONG pcbActual);

    OS2EXPORT APIRET OS2API DosSetRelMaxFH(PLONG pcbReqCount, PULONG pcbCurMaxFH);

    OS2EXPORT APIRET OS2API DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr);

    OS2EXPORT APIRET OS2API DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile,
                                    ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode,
                                    PEAOP2 peaop2);

    OS2EXPORT APIRET OS2API DosSetFilePtr(HFILE hFile, LONG ib, ULONG method, PULONG ibActual);

    OS2EXPORT APIRET OS2API DosRead(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual);

    OS2EXPORT APIRET OS2API DosClose(HFILE hFile);

    OS2EXPORT APIRET OS2API DosQueryPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf,
                                             ULONG cbInfoBuf);

    OS2EXPORT APIRET OS2API DosSetFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfoBuf,
                                           ULONG cbInfoBuf);

    OS2EXPORT APIRET OS2API DosSetDefaultDisk(ULONG disknum);

    OS2EXPORT APIRET OS2API DosDeleteDir(PSZ pszDir);

    OS2EXPORT APIRET OS2API DosCopy(PSZ pszOld, PSZ pszNew, ULONG option);

    OS2EXPORT APIRET OS2API DosMove(PSZ pszOld, PSZ pszNew);

    OS2EXPORT APIRET OS2API DosQueryFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo,
                                             ULONG cbInfoBuf);

    OS2EXPORT APIRET OS2API DosDelete(PSZ pszFile);

    OS2EXPORT APIRET OS2API DosForceDelete(PSZ pszFile);

    OS2EXPORT APIRET OS2API DosQueryCurrentDir(ULONG disknum, PBYTE pBuf, PULONG pcbBuf);

    OS2EXPORT APIRET OS2API DosSetPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf,
                                           ULONG cbInfoBuf, ULONG flOptions);

    OS2EXPORT APIRET OS2API DosOpenL(PSZ pszFileName, PHFILE pHf, PULONG pulAction, LONGLONG cbFile,
                                     ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode,
                                     PEAOP2 peaop2);

    OS2EXPORT APIRET OS2API DosFindFirst(PSZ pszFileSpec, PHDIR phdir, ULONG flAttribute,
                                         PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames,
                                         ULONG ulInfoLevel);

    OS2EXPORT APIRET OS2API DosFindNext(HDIR hDir, PVOID pfindbuf, ULONG cbfindbuf,
                                        PULONG pcFilenames);

    OS2EXPORT APIRET OS2API DosFindClose(HDIR hDir);

    OS2EXPORT APIRET OS2API DosQueryCurrentDisk(PULONG pdisknum, PULONG plogical);

    OS2EXPORT APIRET OS2API DosResetBuffer(HFILE hFile);

    OS2EXPORT APIRET OS2API DosEditName(ULONG level, PSZ source, PSZ edit, PBYTE target,
                                        ULONG capacity);

    OS2EXPORT APIRET OS2API DosEnumAttribute(ULONG reference_type, PVOID reference, ULONG first,
                                             PVOID buffer, ULONG capacity, PULONG count,
                                             ULONG level);

    OS2EXPORT APIRET OS2API DosShutdown(ULONG reserved);

    OS2EXPORT APIRET OS2API DosQueryFHState(HFILE hFile, PULONG pMode);

    OS2EXPORT APIRET OS2API DosSetMaxFH(ULONG cFH);

    OS2EXPORT APIRET OS2API DosQueryFSInfo(ULONG drive, ULONG level, PVOID data, ULONG capacity);

    OS2EXPORT APIRET OS2API DosSetFSInfo(ULONG drive, ULONG level, PVOID data, ULONG size);

    OS2EXPORT APIRET OS2API DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal,
                                             ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb,
                                             PULONG pcbBuffLength);

    OS2EXPORT APIRET OS2API DosSetFileSize(HFILE hFile, ULONG cbSize);

    OS2EXPORT APIRET OS2API DosSetFileLocks(HFILE hFile, PFILELOCK unlock, PFILELOCK lock,
                                            ULONG timeout, ULONG flags);

    OS2EXPORT APIRET OS2API DosSetCurrentDir(PSZ pszName);

    OS2EXPORT APIRET OS2API DosCreateDir(PSZ pszDirName, PEAOP2 peaop2);

    /* INCL_DOSMEMMGR: private, shared and suballocated memory */

    OS2EXPORT APIRET OS2API DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag);

    OS2EXPORT APIRET OS2API DosGetNamedSharedMem(PPVOID ppb, PSZ pszName, ULONG flag);

    OS2EXPORT APIRET OS2API DosAllocSharedMem(PPVOID ppb, PSZ pszName, ULONG cb, ULONG flag);

    OS2EXPORT APIRET OS2API DosGiveSharedMem(PVOID pb, PID pid, ULONG flag);

    OS2EXPORT APIRET OS2API DosSubUnsetMem(PVOID pbBase);

    OS2EXPORT APIRET OS2API DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb);

    OS2EXPORT APIRET OS2API DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb);

    OS2EXPORT APIRET OS2API DosSetMem(PVOID pb, ULONG cb, ULONG flag);

    OS2EXPORT APIRET OS2API DosQueryMem(PVOID pb, PULONG pcb, PULONG pflag);

    OS2EXPORT APIRET OS2API DosFreeMem(PVOID pb);

    OS2EXPORT APIRET OS2API DosSubFreeMem(PVOID pbBase, PVOID pb, ULONG cb);

    OS2EXPORT APIRET OS2API DosGetSharedMem(PVOID address, ULONG flags);

    /* INCL_DOSSEMAPHORES: event, mutex and muxwait semaphores */

    enum
    {
        DC_SEM_SHARED = 1
    };

    enum
    {
        DCMW_WAIT_ANY = (1 << 1),
        DCMW_WAIT_ALL = (1 << 2)
    };

    enum
    {
        SEM_INDEFINITE_WAIT = -1,
        SEM_IMMEDIATE_RETURN = 0
    };

    typedef struct
    {
        ULONG hsemCur;
        ULONG ulUser;
    } SEMRECORD, *PSEMRECORD;

    OS2EXPORT APIRET OS2API DosCreateMuxWaitSem(PSZ name, PHMUX output, ULONG count,
                                                PSEMRECORD records, ULONG flags);

    OS2EXPORT APIRET OS2API DosOpenMuxWaitSem(PSZ name, PHMUX output);

    OS2EXPORT APIRET OS2API DosCloseMuxWaitSem(HMUX handle);

    OS2EXPORT APIRET OS2API DosWaitMuxWaitSem(HMUX handle, ULONG timeout, PULONG user);

    OS2EXPORT APIRET OS2API DosAddMuxWaitSem(HMUX handle, PSEMRECORD record);

    OS2EXPORT APIRET OS2API DosDeleteMuxWaitSem(HMUX handle, ULONG semaphore);

    OS2EXPORT APIRET OS2API DosQueryMuxWaitSem(HMUX handle, PULONG count, PSEMRECORD records,
                                               PULONG flags);

    OS2EXPORT APIRET OS2API DosCreateEventSem(PSZ pszName, PHEV phev, ULONG flAttr, BOOL32 fState);

    OS2EXPORT APIRET OS2API DosCreateMutexSem(PSZ pszName, PHMTX phmtx, ULONG flAttr,
                                              BOOL32 fState);

    OS2EXPORT APIRET OS2API DosOpenMutexSem(PSZ pszName, PHMTX phmtx);

    OS2EXPORT APIRET OS2API DosRequestMutexSem(HMTX hmtx, ULONG ulTimeout);

    OS2EXPORT APIRET OS2API DosReleaseMutexSem(HMTX hmtx);

    OS2EXPORT APIRET OS2API DosResetEventSem(HEV hev, PULONG pulPostCt);

    OS2EXPORT APIRET OS2API DosPostEventSem(HEV hev);

    OS2EXPORT APIRET OS2API DosCloseEventSem(HEV hev);

    OS2EXPORT APIRET OS2API DosWaitEventSem(HEV hev, ULONG ulTimeout);

    OS2EXPORT APIRET OS2API DosOpenEventSem(PSZ pszName, PHEV phev);

    OS2EXPORT APIRET OS2API DosQueryEventSem(HEV hev, PULONG pulPostCt);

    OS2EXPORT APIRET OS2API DosCloseMutexSem(HMTX hmtx);

    OS2EXPORT APIRET OS2API DosQueryMutexSem(HMTX hmtx, PPID ppid, PTID ptid, PULONG pulCount);

    /* INCL_DOSDATETIME: date, time and timers */

    typedef struct
    {
        UCHAR hours;
        UCHAR minutes;
        UCHAR seconds;
        UCHAR hundredths;
        UCHAR day;
        UCHAR month;
        USHORT year;
        SHORT timezone;
        UCHAR weekday;
    } DATETIME, *PDATETIME;

    typedef HANDLE HTIMER, *PHTIMER;

    OS2EXPORT APIRET OS2API DosGetDateTime(PDATETIME pdt);

    OS2EXPORT APIRET OS2API DosSetDateTime(PDATETIME pdt);

    OS2EXPORT APIRET OS2API DosAsyncTimer(ULONG msec, HSEM hsem, PHTIMER phtimer);

    OS2EXPORT APIRET OS2API DosStartTimer(ULONG msec, HSEM hsem, PHTIMER phtimer);

    OS2EXPORT APIRET OS2API DosStopTimer(HTIMER htimer);

    /* INCL_DOSMODULEMGR: module loading and queries */

    OS2EXPORT APIRET OS2API DosQueryModuleName(HMODULE hmod, ULONG cbName, PCHAR pch);

    OS2EXPORT APIRET OS2API DosQueryModFromEIP(PHMODULE phmod, PULONG pulObject, ULONG cbModName,
                                               PCHAR pszModName, PULONG pulOffset, ULONG ulEIP);

    OS2EXPORT APIRET OS2API DosQueryModuleHandle(PSZ pszModname, PHMODULE phmod);

    OS2EXPORT APIRET OS2API DosQueryProcAddr(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN *ppfn);

    OS2EXPORT APIRET OS2API DosLoadModule(PSZ pszName, ULONG cbName, PSZ pszModname,
                                          PHMODULE phmod);

    OS2EXPORT APIRET OS2API DosFreeModule(HMODULE hmod);

    OS2EXPORT APIRET OS2API DosQueryProcType(HMODULE module, ULONG ordinal, PSZ name, PULONG type);

    /* INCL_DOSRESOURCES: resources */

    OS2EXPORT APIRET OS2API DosGetResource(HMODULE module, ULONG type, ULONG name, PPVOID output);

    OS2EXPORT APIRET OS2API DosFreeResource(PVOID resource);

    OS2EXPORT APIRET OS2API DosQueryResourceSize(HMODULE module, ULONG type, ULONG name,
                                                 PULONG size);

    OS2EXPORT APIRET OS2API Dos32QueryResourceSize(HMODULE module, ULONG type, ULONG name,
                                                   PULONG size);

    /* INCL_DOSNLS: national language support */

    typedef struct
    {
        ULONG country;
        ULONG codepage;
    } COUNTRYCODE, *PCOUNTRYCODE;

    typedef struct
    {
        ULONG country;
        ULONG codepage;
        ULONG fsDateFmt;
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
    } COUNTRYINFO, *PCOUNTRYINFO;

    OS2EXPORT APIRET OS2API DosQueryDBCSEnv(ULONG cb, PCOUNTRYCODE pcc, PCHAR pBuf);

    OS2EXPORT APIRET OS2API DosQueryCp(ULONG cb, PULONG arCP, PULONG pcCP);

    OS2EXPORT APIRET OS2API DosSetProcessCp(ULONG cp);

    OS2EXPORT APIRET OS2API DosMapCase(ULONG cb, PCOUNTRYCODE pcc, PCHAR pch);

    OS2EXPORT APIRET OS2API DosQueryCtryInfo(ULONG cb, PCOUNTRYCODE pcc, PCOUNTRYINFO pci,
                                             PULONG pcbActual);

    OS2EXPORT APIRET OS2API DosQueryCollate(ULONG len, PCOUNTRYCODE pcc, PCHAR pch, PULONG dlen);

    /* INCL_DOSEXCEPTIONS: exception handlers and signals */

    OS2EXPORT APIRET OS2API DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD pERegRec);

    OS2EXPORT APIRET OS2API DosUnsetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD pERegRec);

    OS2EXPORT APIRET OS2API DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes);

    OS2EXPORT APIRET OS2API DosEnterMustComplete(PULONG pulNesting);

    OS2EXPORT APIRET OS2API DosExitMustComplete(PULONG pulNesting);

    OS2EXPORT APIRET OS2API DosAcknowledgeSignalException(ULONG ulSignalNum);

    OS2EXPORT APIRET OS2API DosRaiseException(PEXCEPTIONREPORTRECORD pexcept);

    OS2EXPORT APIRET OS2API DosUnwindException(PEXCEPTIONREGISTRATIONRECORD phandler,
                                               PVOID pTargetIP, PEXCEPTIONREPORTRECORD pERepRec);

    OS2EXPORT APIRET OS2API DosQueryThreadContext(TID tid, ULONG level, PCONTEXTRECORD pcxt);

    /* INCL_DOSQUEUES: queues */

    typedef struct
    {
        PID pid;
        ULONG ulData;
    } REQUESTDATA, *PREQUESTDATA;

    enum
    {
        QUE_FIFO = 0,
        QUE_LIFO = 1,
        QUE_PRIORITY = 2,
        QUE_NOCONVERT_ADDRESS = 0,
        QUE_CONVERT_ADDRESS = 4
    };

    OS2EXPORT APIRET OS2API DosCreateQueue(PHQUEUE phq, ULONG priority, PSZ pszName);

    OS2EXPORT APIRET OS2API DosCloseQueue(HQUEUE hq);

    OS2EXPORT APIRET OS2API DosOpenQueue(PPID ppid, PHQUEUE phq, PSZ pszName);

    OS2EXPORT APIRET OS2API DosPeekQueue(HQUEUE hq, PREQUESTDATA pRequest, PULONG pcbData,
                                         PPVOID ppbuf, PULONG element, BOOL32 nowait,
                                         PBYTE ppriority, HEV hsem);

    OS2EXPORT APIRET OS2API DosPurgeQueue(HQUEUE hq);

    OS2EXPORT APIRET OS2API DosQueryQueue(HQUEUE hq, PULONG pcbEntries);

    OS2EXPORT APIRET OS2API DosReadQueue(HQUEUE hq, PREQUESTDATA pRequest, PULONG pcbData,
                                         PPVOID ppbuf, ULONG element, BOOL32 wait, PBYTE ppriority,
                                         HEV hsem);

    OS2EXPORT APIRET OS2API DosWriteQueue(HQUEUE hq, ULONG request, ULONG cbData, PVOID pbData,
                                          ULONG priority);

    /* INCL_DOSMISC: messages, system information, environment and errors */

    enum
    {
        QSV_MAX_PATH_LENGTH = 1,
        QSV_MAX_TEXT_SESSIONS,
        QSV_MAX_PM_SESSIONS,
        QSV_MAX_VDM_SESSIONS,
        QSV_BOOT_DRIVE,
        QSV_DYN_PRI_VARIATION,
        QSV_MAX_WAIT,
        QSV_MIN_SLICE,
        QSV_MAX_SLICE,
        QSV_PAGE_SIZE,
        QSV_VERSION_MAJOR,
        QSV_VERSION_MINOR,
        QSV_VERSION_REVISION,
        QSV_MS_COUNT,
        QSV_TIME_LOW,
        QSV_TIME_HIGH,
        QSV_TOTPHYSMEM,
        QSV_TOTRESMEM,
        QSV_TOTAVAILMEM,
        QSV_MAXPRMEM,
        QSV_MAXSHMEM,
        QSV_TIMER_INTERVAL,
        QSV_MAX_COMP_LENGTH,
        QSV_FOREGROUND_FS_SESSION,
        QSV_FOREGROUND_PROCESS
    };

    enum
    {
        BEGIN_LIBPATH = 1,
        END_LIBPATH = 2,
        LIBPATHSTRICT = 3
    };

    OS2EXPORT APIRET OS2API DosQuerySysInfo(ULONG iStart, ULONG iLast, PVOID pBuf, ULONG cbBuf);

    OS2EXPORT APIRET OS2API DosScanEnv(PSZ pszName, PSZ *ppszValue);

    OS2EXPORT APIRET OS2API DosSearchPath(ULONG flag, PSZ pszPathOrName, PSZ pszFilename,
                                          PBYTE pBuf, ULONG cbBuf);

    OS2EXPORT APIRET OS2API DosError(ULONG error);

    OS2EXPORT APIRET OS2API DosSetExtLIBPATH(PSZ path, ULONG flags);

    OS2EXPORT APIRET OS2API DosQueryExtLIBPATH(PSZ pszExtLIBPATH, ULONG flags);

    OS2EXPORT APIRET OS2API DosInsertMessage(PVOID table, ULONG count, PCHAR message,
                                             ULONG message_size, PCHAR output, ULONG capacity,
                                             PULONG actual);

    OS2EXPORT APIRET OS2API DosPutMessage(HFILE hfile, ULONG cbMsg, PCHAR pBuf);

    /* MSGSEG32 passes a hidden word before seven arguments; the bridge removes only that word with ret 4. */
    OS2EXPORT __attribute__((stdcall)) APIRET OS2API DosTrueGetMessage(PVOID msgseg);

    /* INCL_DOSSESMGR: sessions */

    enum
    {
        FAPPTYP_NOTSPEC = 0x0000,
        FAPPTYP_NOTWINDOWCOMPAT = 0x0001,
        FAPPTYP_WINDOWCOMPAT = 0x0002,
        FAPPTYP_WINDOWAPI = 0x0003,
        FAPPTYP_BOUND = 0x0008,
        FAPPTYP_DLL = 0x0010,
        FAPPTYP_DOS = 0x0020,
        FAPPTYP_PHYSDRV = 0x0040,
        FAPPTYP_VIRTDRV = 0x0080,
        FAPPTYP_PROTDLL = 0x0100,
        FAPPTYP_WINDOWSREAL = 0x0200,
        FAPPTYP_WINDOWSPROT = 0x0400,
        FAPPTYP_WINDOWSPROT31 = 0x1000,
        FAPPTYP_32BIT = 0x4000
    };

    enum
    {
        SSF_RELATED_INDEPENDENT = 0,
        SSF_RELATED_CHILD = 1
    };

    enum
    {
        SSF_FGBG_FORE = 0,
        SSF_FGBG_BACK = 1
    };

    enum
    {
        SSF_TRACEOPT_NONE = 0,
        SSF_TRACEOPT_TRACE = 1,
        SSF_TRACEOPT_TRACEALL = 2
    };

    enum
    {
        SSF_INHERTOPT_SHELL = 0,
        SSF_INHERTOPT_PARENT = 1
    };

    enum
    {
        SSF_TYPE_DEFAULT = 0,
        SSF_TYPE_FULLSCREEN = 1,
        SSF_TYPE_WINDOWABLEVIO = 2,
        SSF_TYPE_PM = 3,
        SSF_TYPE_VDM = 4,
        SSF_TYPE_GROUP = 5,
        SSF_TYPE_DLL = 6,
        SSF_TYPE_WINDOWEDVDM = 7,
        SSF_TYPE_PDD = 8,
        SSF_TYPE_VDD = 9
    };

    typedef struct
    {
        USHORT Length;
        USHORT Related;
        USHORT FgBg;
        USHORT TraceOpt;
        PSZ PgmTitle;
        PSZ PgmName;
        PBYTE PgmInputs;
        PBYTE TermQ;
        PBYTE Environment;
        USHORT InheritOpt;
        USHORT SessionType;
        PSZ IconFile;
        ULONG PgmHandle;
        USHORT PgmControl;
        USHORT InitXPos;
        USHORT InitYPos;
        USHORT InitXSize;
        USHORT InitYSize;
        USHORT Reserved;
        PSZ ObjectBuffer;
        ULONG ObjectBuffLen;
    } STARTDATA, *PSTARTDATA;

    typedef struct
    {
        USHORT Length, SelectInd, BondInd;
    } STATUSDATA, *PSTATUSDATA;

/* STATUSDATA SelectInd and BondInd values. */
#define SET_SESSION_UNCHANGED 0
#define SET_SESSION_SELECTABLE 1
#define SET_SESSION_NON_SELECTABLE 2
#define SET_SESSION_BOND 1
#define SET_SESSION_NO_BOND 2

    OS2EXPORT APIRET OS2API DosQueryAppType(PSZ pszName, PULONG pFlags);

    OS2EXPORT APIRET OS2API DosStartSession(PSTARTDATA psd, PULONG pidSession, PPID ppid);

    OS2EXPORT APIRET OS2API DosSelectSession(ULONG session);

    OS2EXPORT APIRET OS2API DosSetSession(ULONG session, PSTATUSDATA status);

    OS2EXPORT APIRET OS2API DosStopSession(ULONG scope, ULONG session);

    /* INCL_DOSDEVICES: devices and IOCtl */

    enum
    {
        DEVINFO_PRINTER,
        DEVINFO_RS232,
        DEVINFO_FLOPPY,
        DEVINFO_COPROCESSOR,
        DEVINFO_SUBMODEL,
        DEVINFO_MODEL,
        DEVINFO_ADAPTER
    };

    OS2EXPORT APIRET OS2API DosDevConfig(PVOID pdevinfo, ULONG item);

    OS2EXPORT APIRET OS2API DosDevIOCtl(HFILE device, ULONG category, ULONG function,
                                        PVOID parameters, ULONG parameter_max,
                                        PULONG parameter_size, PVOID data, ULONG data_max,
                                        PULONG data_size);

    /* INCL_DOSNMPIPES: named pipes */

    OS2EXPORT APIRET OS2API DosWaitNPipe(PSZ pszName, ULONG ulTimeOut);

    /* INCL_DOSPROFILE: timers for profiling */

    /* Undocumented DOSCALLS entry used by Java runtimes. */
    OS2EXPORT APIRET OS2API DosQuerySysState(ULONG func, ULONG arg1, ULONG pid, ULONG _res_,
                                             PVOID buf, ULONG bufsz);

    OS2EXPORT APIRET OS2API DosTmrQueryFreq(PULONG frequency);

    OS2EXPORT APIRET OS2API DosTmrQueryTime(PVOID ticks);

#ifdef __cplusplus
}
#endif

#endif
